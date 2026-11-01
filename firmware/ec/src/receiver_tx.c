#include <stdint.h>
#include "ec_abi.h"

extern void reply_payload(const uint8_t *, uint8_t);

typedef struct {
    uint16_t control, count;
    uint32_t source, destination, next;
} dma_descriptor_t;

typedef struct {
    dma_descriptor_t descriptors[32], writeback[32];
    uint32_t bits[642];
    uint32_t started, sent, rejected;
    uint16_t ticks;
    uint8_t ready, fault, sequence, size, received;
    uint8_t frame[64];
} receiver_state_t;

_Static_assert(sizeof(receiver_state_t) <= 0x1000, "Receiver state exceeds reserved RAM");

static void receiver_route_pin(volatile uint8_t *port)
{
    // PMUX starts at 0x30. Preserve PB12's SmartAudio selection in the low nibble.
    port[0x30 + 13 / 2] = (port[0x30 + 13 / 2] & 15) | 0x50;
    port[0x40 + 13] = 1;
}

#ifdef RECEIVER_HOST_TEST
extern receiver_state_t receiver_test_state;
#define S (&receiver_test_state)
extern uint32_t receiver_now(void);
extern int receiver_configure(void);
extern int receiver_tx_busy(void);
extern int receiver_start(unsigned count);
#else
#define S ((volatile receiver_state_t *)EC_RECEIVER_STATE)
#define R8(a) (*(volatile uint8_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
#define TIMER 0x42001000u
#define DMA 0x4100a000u
#define PORTB 0x41008080u

static uint32_t receiver_now(void)
{
    return ((uint32_t (*)(void))(EC_MILLIS | 1u))();
}

static int timer_sync(void)
{
    for (unsigned i = 0; i < 100000; ++i)
        if (!R32(TIMER + 8)) return 1;
    S->fault = 2;
    return 0;
}

void receiver_tx_restore_pin(void)
{
    if (!S->ready) return;
    receiver_route_pin((volatile uint8_t *)PORTB);
}

static int receiver_configure(void)
{
    if (S->ready) return 1;
    if (S->fault) return 0;
    R32(0x40000810u) |= 1u << 9;
    R32(0x4000081cu) |= 1u << 4;
    // Stock v25 does not use DMAC or TCC3. Refuse to take them from another owner.
    if (R16(DMA) || R32(DMA + 0x34) || R32(TIMER) ||
        R8(0x40000805u) != 1 || R32(EC_CORE_CLOCK) != 120000000u) {
        S->fault = 1;
        return 0;
    }
    uint32_t clock = R32(0x40001cf4u);
    if ((clock & 0xc0) && (clock & 15)) {
        S->fault = 1;
        return 0;
    }
    R32(0x40001cf4u) = 0x40; // TCC3 uses GCLK0 at 120 MHz.
    unsigned wait;
    for (wait = 0; wait < 100000; ++wait)
        if (R32(0x40001cf4u) & 0x40) break;
    if (wait == 100000) { S->fault = 2; return 0; }
    R32(TIMER) = 1;
    if (!timer_sync()) return 0;
    S->ticks = 286; // 419580 baud, within 0.1 percent of CRSF's 420000.
    R32(TIMER + 0x3c) = 2;
    if (!timer_sync()) return 0;
    R32(TIMER + 0x40) = S->ticks - 1;
    if (!timer_sync()) return 0;
    R32(TIMER + 0x48) = S->ticks;
    if (!timer_sync()) return 0;
    R32(DMA + 0x34) = (uint32_t)S->descriptors;
    R32(DMA + 0x38) = (uint32_t)S->writeback;
    R16(DMA) = 0x102;
    R32(PORTB + 0x18) = 1u << 13;
    R32(PORTB + 8) = 1u << 13;
    R32(TIMER) = 2;
    if (!timer_sync()) return 0;
    S->ready = 1;
    receiver_tx_restore_pin();
    return 1;
}

int receiver_tx_busy(void)
{
    if (!S->ready) return 0;
    if (R8(DMA + 0x4e) & 1) S->fault = 3;
    // Allow the final buffered stop bit to reach the pin before reusing the buffer.
    return (R32(DMA + 0x40) & 2) || (uint32_t)(receiver_now() - S->started) < 3;
}

static int receiver_start(unsigned count)
{
    if (S->fault) return 0;
    volatile dma_descriptor_t *d = &S->descriptors[0];
    d->control = 0x401 | (2u << 8);
    d->count = count;
    d->source = (uint32_t)(S->bits + count);
    d->destination = TIMER + 0x74;
    d->next = 0;
    R8(DMA + 0x4e) = 7;
    __asm volatile ("dmb" ::: "memory");
    // One buffered compare value per overflow gives one complete UART bit per period.
    R32(DMA + 0x40) = (38u << 8) | (2u << 20) | 2;
    return 1;
}
#endif

static uint8_t frame_crc(const volatile uint8_t *p, unsigned n)
{
    uint8_t crc = 0;
    while (n--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc & 0x80) ? 0xd5 : 0);
    }
    return crc;
}

void receiver_request(const uint8_t *p, unsigned n)
{
    if (n == 1 && p[0] == 144) {
        uint32_t values[] = {S->sent, S->rejected, S->fault, S->ready, receiver_tx_busy()};
        uint8_t reply[21] = {144};
        for (unsigned i = 0; i < sizeof(values); ++i) reply[i + 1] = ((uint8_t *)values)[i];
        reply_payload(reply, sizeof(reply));
        return;
    }
    if (n < 5 || n > 64 || p[0] != 143 || p[2] < 4 || p[2] > 64 ||
        p[3] > p[2] || n - 4 > (unsigned)(p[2] - p[3])) goto reject;
    if (!p[3]) {
        S->sequence = p[1];
        S->size = p[2];
        S->received = 0;
    }
    if (p[1] != S->sequence || p[2] != S->size || p[3] != S->received) goto reject;
    for (unsigned i = 4; i < n; ++i) S->frame[S->received++] = p[i];
    if (S->received != S->size) return;
    S->received = 0;
    if (S->frame[1] + 2 != S->size ||
        frame_crc(S->frame + 2, S->size - 3) != S->frame[S->size - 1] ||
        !receiver_configure() || receiver_tx_busy()) goto reject;
    unsigned count = 0;
    for (unsigned i = 0; i < S->size; ++i) {
        unsigned byte = S->frame[i];
        S->bits[count++] = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            S->bits[count++] = (byte & (1u << bit)) ? S->ticks : 0;
        S->bits[count++] = S->ticks;
    }
    S->bits[count++] = S->ticks;
    S->bits[count++] = S->ticks;
    if (!receiver_start(count)) goto reject;
    S->started = receiver_now();
    ++S->sent;
    return;
reject:
    S->received = 0;
    ++S->rejected;
}
