#include <assert.h>
#include <string.h>
#define RECEIVER_HOST_TEST
#include "../src/receiver_tx.c"

receiver_state_t receiver_test_state;
static unsigned count, starts;
static int busy, configured = 1;
uint32_t receiver_now(void) { return 100; }
int receiver_configure(void) { S->ticks = 286; return configured; }
int receiver_tx_busy(void) { return busy; }
int receiver_start(unsigned n) { count = n; ++starts; return 1; }
void reply_payload(const uint8_t *p, uint8_t n) { (void)p; (void)n; }

static void send_frame(const uint8_t *frame, unsigned n)
{
    uint8_t p[64] = {143, 7, n, 0};
    for (unsigned offset = 0; offset < n; offset += 60) {
        unsigned bytes = n - offset > 60 ? 60 : n - offset;
        p[3] = offset;
        memcpy(p + 4, frame + offset, bytes);
        receiver_request(p, bytes + 4);
    }
}

int main(void)
{
    uint8_t port[128];
    memset(port, 0xa5, sizeof(port));
    port[0x36] = 0x22;
    receiver_route_pin(port);
    assert(port[0x36] == 0x52 && port[0x4d] == 1);
    for (unsigned i = 0; i < sizeof(port); ++i)
        if (i != 0x36 && i != 0x4d) assert(port[i] == 0xa5);
    uint8_t frame[64] = {0xc8, 62, 0x29};
    for (unsigned seed = 0; seed < 256; ++seed) {
        for (unsigned i = 3; i < 63; ++i) frame[i] = seed + i;
        frame[63] = frame_crc(frame + 2, 61);
        send_frame(frame, sizeof(frame));
        assert(starts == seed + 1 && count == 642);
        for (unsigned i = 0; i < 64; ++i) {
            assert(S->bits[i * 10] == 0);
            unsigned decoded = 0;
            for (unsigned bit = 0; bit < 8; ++bit) {
                unsigned pulse = S->bits[i * 10 + bit + 1];
                assert(pulse == 0 || pulse == 286);
                if (pulse) decoded |= 1u << bit;
            }
            assert(decoded == frame[i] && S->bits[i * 10 + 9] == 286);
        }
        assert(S->bits[640] == 286 && S->bits[641] == 286);
    }
    busy = 1;
    send_frame(frame, 64);
    assert(starts == 256 && S->rejected == 1);
    busy = 0;
    frame[63] ^= 1;
    send_frame(frame, 64);
    assert(starts == 256 && S->rejected == 2);
    uint8_t fragment[] = {143, 8, 64, 60, 1, 2, 3, 4};
    receiver_request(fragment, sizeof(fragment));
    assert(starts == 256 && S->rejected == 3);
    uint8_t short_frame[] = {0xc8, 2, 0x21, 0};
    short_frame[3] = frame_crc(short_frame + 2, 1);
    send_frame(short_frame, 4);
    assert(starts == 257 && count == 42);
    configured = 0;
    send_frame(short_frame, 4);
    assert(starts == 257 && S->rejected == 4);
    return 0;
}
