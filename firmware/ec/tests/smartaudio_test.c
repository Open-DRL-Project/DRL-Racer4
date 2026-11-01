#include <assert.h>
#include <string.h>
#define SA_HOST_TEST
#include "../src/smartaudio.c"

volatile sa_state_t sa_test_state;
static uint32_t clock_ms;
static unsigned flags, incoming, tx_count, rx_enabled, stops, status = 2;
static uint8_t transmitted[64], response[64];
static unsigned response_size;
int receiver_tx_busy(void) { return 0; }
uint32_t sa_now(void) { return clock_ms; }
int sa_configure(unsigned baud) { return baud >= 4800 && baud <= 4950; }
unsigned sa_flags(void) { return flags; }
unsigned sa_status(void) { return status; }
unsigned sa_read(void) { flags &= ~4u; return incoming; }
void sa_write(unsigned byte) { transmitted[tx_count++] = byte; }
void sa_tx_start(void) { rx_enabled = 0; }
void sa_rx_start(void) { rx_enabled = 1; }
void sa_stop(void) { ++stops; rx_enabled = 0; }
void sa_clear_error(void) { flags &= ~0x80u; }
void reply_payload(const uint8_t *p, uint8_t n)
{
    memcpy(response, p, n);
    response_size = n;
}
static void send_frame(void)
{
    while (S->sent < S->tx_size) { flags = 1; sa_irq(); }
    assert(!rx_enabled);
    flags = 2;
    sa_irq();
    assert(rx_enabled);
}
int main(void)
{
    const uint8_t request[] = {140, 7, 0xc0, 0x12, 5, 0xaa, 0x55, 3, 0, 0x9f};
    const uint8_t poll[] = {141, 7};
    sa_request(request, sizeof(request));
    assert(response[2] == 0 && S->phase == SA_TX);
    send_frame();
    assert(tx_count == 6 && transmitted[0] == 0 && transmitted[1] == 0xaa);
    const uint8_t reply[] = {0xaa, 0x55, 9, 5, 24, 27, 16, 0x16, 0x6c, 0};
    for (unsigned i = 0; i < sizeof(reply); ++i) {
        clock_ms += 2;
        incoming = reply[i]; flags = 4; sa_irq();
    }
    sa_request(poll, sizeof(poll));
    assert(response[2] == 0);
    clock_ms += 8;
    sa_request(poll, sizeof(poll));
    assert(response[2] == 1 && response_size == sizeof(reply) + 4);
    assert(!memcmp(response + 4, reply, sizeof(reply)) && !rx_enabled);

    sa_request(request, sizeof(request));
    unsigned before = tx_count;
    sa_request(request, sizeof(request));
    assert(response[2] == 2 && tx_count == before);
    clock_ms += 100;
    sa_request(poll, sizeof(poll));
    assert(response[2] == 2 && S->phase == SA_FAILED);

    tx_count = 0;
    sa_request(request, sizeof(request));
    send_frame();
    flags = 0x84; sa_irq();
    assert(S->phase == SA_RX && rx_enabled && S->error == 2);
    for (unsigned i = 0; i < sizeof(reply); ++i) {
        clock_ms += 2; incoming = reply[i]; flags = 4; sa_irq();
    }
    clock_ms += 8;
    sa_request(poll, sizeof(poll));
    assert(response[2] == 1 && !memcmp(response + 4, reply, sizeof(reply)));

    tx_count = 0;
    sa_request(request, sizeof(request));
    send_frame();
    status = 4; flags = 0x80; sa_irq();
    assert(S->phase == SA_FAILED && !rx_enabled);

    tx_count = 0;
    clock_ms = 0xfffffff0u;
    sa_request(request, sizeof(request));
    send_frame();
    clock_ms += 101;
    sa_request(poll, sizeof(poll));
    assert(response[2] == 2);

    uint8_t malformed[sizeof(request)];
    memcpy(malformed, request, sizeof(request));
    malformed[4] = 31;
    sa_request(malformed, sizeof(malformed));
    assert(response[2] == 2);
    assert(stops);
    return 0;
}
