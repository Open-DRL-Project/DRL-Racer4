#include <stdint.h>
#include "ec_abi.h"

extern void reply_payload(const uint8_t *, uint8_t);
extern int receiver_tx_busy(void);
extern void receiver_tx_restore_pin(void);

enum { SA_IDLE, SA_TX, SA_RX, SA_DONE, SA_FAILED };
typedef struct {
    uint32_t magic, started, received_at;
    uint16_t baud, error;
    uint8_t phase, sequence, sent, tx_size, rx_size;
    uint8_t tx[32], rx[48];
} sa_state_t;

_Static_assert(sizeof(sa_state_t) <= 0xe0, "SmartAudio state exceeds reserved RAM");

#define SA_MAGIC 0x53413031u
#ifdef SA_HOST_TEST
extern volatile sa_state_t sa_test_state;
#define S (&sa_test_state)
extern uint32_t sa_now(void);
extern int sa_configure(unsigned baud);
extern unsigned sa_flags(void);
extern unsigned sa_status(void);
extern unsigned sa_read(void);
extern void sa_write(unsigned byte);
extern void sa_tx_start(void);
extern void sa_rx_start(void);
extern void sa_stop(void);
extern void sa_clear_error(void);
#else
// Startup clears this reservation and the stock heap starts above it.
#define S ((volatile sa_state_t *)EC_SMARTAUDIO_STATE)
#define R8(a) (*(volatile uint8_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
#define U 0x43000000u
static uint32_t sa_now(void) { return ((uint32_t (*)(void))(EC_MILLIS | 1u))(); }
static unsigned sa_flags(void) { return R8(U + 0x18); }
static unsigned sa_status(void) { return R16(U + 0x1a); }
static unsigned sa_read(void) { return R16(U + 0x28); }
static void sa_write(unsigned byte) { R16(U + 0x28) = byte; }
static void sa_clear_error(void) { R16(U + 0x1a) = 7; R8(U + 0x18) = 0x80; }
static int sa_sync(void)
{
    for (unsigned i = 0; i < 100000; ++i)
        if (!R32(U + 0x1c)) return 1;
    return 0;
}
static void sa_stop(void)
{
    R8(U + 0x14) = 0xff;
    R32(U + 4) = 1u << 6;
    sa_sync();
}
static int sa_configure(unsigned baud)
{
    if (S->baud == baud) return 1;
    // Retain the stock clock setup and baud calculation, then receive on TX's PAD0.
    ((void (*)(void *, unsigned, unsigned))EC_UART_BEGIN)((void *)EC_CTM_UART_OBJECT, baud, 0x413);
    R8(U + 0x14) = 0xff;
    R32(U) &= ~2u;
    if (!sa_sync()) return 0;
    R32(U) &= ~(3u << 20);
    R32(U + 4) = 1u << 6;
    if (!sa_sync()) return 0;
    // PB13 is no longer the receive pin. PB12 remains peripheral C with input enabled.
    R8(0x41008080u + 0x40 + 13) = 2;
    R8(0x41008080u + 0x40 + 12) = 3;
    R32(0x41008080u + 4) = (1u << 12) | (1u << 13);
    R32(U) |= 2;
    if (!sa_sync()) return 0;
    S->baud = baud;
    receiver_tx_restore_pin();
    return 1;
}
static void sa_tx_start(void)
{
    sa_clear_error();
    R8(U + 0x18) = 2;
    R32(U + 4) = (1u << 6) | (1u << 16);
    if (!sa_sync()) { S->phase = SA_FAILED; return; }
    R8(U + 0x16) = 1;
}
static void sa_rx_start(void)
{
    R8(U + 0x14) = 3;
    // TXC includes both stop bits. Release PB12 before listening for the VTX reply.
    R32(U + 4) = (1u << 6) | (1u << 17);
    if (!sa_sync()) { S->phase = SA_FAILED; return; }
    R8(U + 0x18) = 2;
    R8(U + 0x16) = 0x84;
}
#endif

void sa_irq(void)
{
    if (S->magic != SA_MAGIC) {
#ifndef SA_HOST_TEST
        ((void (*)(void *))EC_UART_IRQ)((void *)EC_CTM_UART_OBJECT);
#endif
        return;
    }
    if (S->phase != SA_TX && S->phase != SA_RX) {
#ifndef SA_HOST_TEST
        R8(U + 0x14) = 0xff;
#endif
        return;
    }
    unsigned flags = sa_flags();
    if (flags & 0x80) {
        unsigned status = sa_status();
        S->error |= status;
        if (flags & 4) (void)sa_read();
        sa_clear_error();
        // A framing error while releasing the single wire must not discard the later reply.
        if (S->phase != SA_RX || (status & 4)) {
            sa_stop();
            S->phase = SA_FAILED;
        }
        return;
    }
    if (S->phase == SA_TX) {
        if ((flags & 1) && S->sent < S->tx_size) {
            sa_write(S->tx[S->sent++]);
#ifndef SA_HOST_TEST
            if (S->sent == S->tx_size) {
                R8(U + 0x14) = 1;
                R8(U + 0x16) = 2;
            }
#endif
        } else if ((flags & 2) && S->sent == S->tx_size) {
            S->phase = SA_RX;
            sa_rx_start();
        }
    } else if ((flags & 4) && S->phase == SA_RX) {
        unsigned byte = sa_read();
        if (S->rx_size == sizeof(S->rx)) {
            sa_stop();
            S->phase = SA_FAILED;
        } else {
            S->rx[S->rx_size++] = byte;
            S->received_at = sa_now();
        }
    }
}

void sa_request(const uint8_t *p, unsigned n)
{
#ifndef SA_HOST_TEST
    if (n == 1 && p[0] == 142) {
        uint32_t values[] = {sa_now(), S->started, R32(U), R32(U + 4),
            R16(U + 12), sa_status(), sa_flags(), R8(U + 0x16),
            S->phase | (S->sent << 8) | (S->rx_size << 16) | (S->sequence << 24),
            S->error, R32(0x410080a0), R8(0x410080cc)};
        uint8_t answer[49] = {142};
        for (unsigned i = 0; i < sizeof(values); ++i)
            answer[i + 1] = ((uint8_t *)values)[i];
        reply_payload(answer, sizeof(answer));
        return;
    }
#endif
    if (n < 2) return;
    uint8_t answer[52] = {141, p[1], 2, 0};
    if (S->magic != SA_MAGIC) {
        volatile uint8_t *bytes = (volatile uint8_t *)S;
        for (unsigned i = 0; i < sizeof(*S); ++i) bytes[i] = 0;
        S->magic = SA_MAGIC;
    }
    uint32_t now = sa_now();
    if (S->phase == SA_RX && S->rx_size && (uint32_t)(now - S->received_at) >= 8) {
        sa_stop();
        S->phase = SA_DONE;
    } else if ((S->phase == SA_TX || S->phase == SA_RX) &&
               (uint32_t)(now - S->started) >= 100) {
        sa_stop();
        S->phase = SA_FAILED;
    }
    if (p[0] == 140) {
        if (n < 6 || p[4] == 0 || p[4] > 30 || n != (unsigned)p[4] + 5) goto reply;
        unsigned baud = p[2] | ((unsigned)p[3] << 8);
        if (baud < 4800 || baud > 4950) goto reply;
        if (S->phase == SA_TX || S->phase == SA_RX) goto reply;
        if (S->baud != baud && receiver_tx_busy()) goto reply;
        sa_stop();
        if (!sa_configure(baud)) goto reply;
        S->error = 0;
        S->sequence = p[1];
        S->tx[0] = 0;
        for (unsigned i = 0; i < p[4]; ++i) S->tx[i + 1] = p[i + 5];
        S->tx_size = p[4] + 1;
        S->sent = S->rx_size = 0;
        S->started = now;
        S->phase = SA_TX;
        sa_tx_start();
    } else if (p[0] != 141 || n != 2) goto reply;
    if (p[1] != S->sequence) goto reply;
    answer[2] = S->phase == SA_DONE ? 1 : S->phase == SA_FAILED ? 2 : 0;
    if (answer[2] == 1) {
        answer[3] = S->rx_size;
        for (unsigned i = 0; i < answer[3]; ++i) answer[i + 4] = S->rx[i];
        S->phase = SA_IDLE;
    }
reply:
    reply_payload(answer, answer[3] + 4);
}
