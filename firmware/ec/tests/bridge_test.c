void sa_request(const unsigned char *p, unsigned n) { (void)p; (void)n; }
void receiver_request(const unsigned char *p, unsigned n) { (void)p; (void)n; }
#include <assert.h>
#include <stdint.h>
#include <string.h>
uint8_t test_osd_object[32];
volatile uint8_t test_osd_debug = 2, test_osd_previous = 2;
static uint8_t registers[256], nvm[256][54], shadow[54], display[480], ack[70];
static unsigned ack_size, clears, write_count;
static int stuck_busy, corrupt, power_off, reset_busy, clear_stuck;
static unsigned programs;
volatile uint8_t test_uart3_lock;
uint8_t test_ec_address = 0x40;
static uint8_t file_packet[64];
static unsigned file_size, file_calls;
static int file_send_failed;
static int file_busy, file_wait_success = 1;
int test_file_tx_busy(void)
{
    return file_busy;
}

int test_file_wait_reply(const uint8_t *p)
{
    assert(test_uart3_lock == 1 && p[0] == 135);
    return file_wait_success;
}

int test_file_send(const uint8_t *p, unsigned size)
{
    assert(size + 5 <= sizeof(file_packet));
    memcpy(file_packet, p, size + 5);
    file_size = size;
    ++file_calls;
    return file_send_failed ? -1 : 0;
}

void file_forward_response(void *, const uint8_t *);
static uint8_t led_color;
static unsigned led_color_calls, led_advance_calls, led_console_calls;
static char led_console[64];
void test_send_led_color(uint8_t value)
{
    led_color = value;
    ++led_color_calls;
}

void test_send_led_advance(void)
{
    ++led_advance_calls;
}

void test_send_led_remote_console(const char *text, int length)
{
    assert(length > 0 && length < (int)sizeof(led_console));
    memcpy(led_console, text, length);
    led_console[length] = 0;
    ++led_console_calls;
}

void chip_delay(uint32_t ms)
{
    assert(ms == 1 || ms == 20);
}

void bridge_process(const uint8_t *frame);
uint8_t chip_read(uint8_t reg)
{
    if (power_off)
        return 0xff;
    if (reset_busy && reg == 0xa0)
        return 0x40;
    if (reg == 0xa0)
        return stuck_busy ? 0x20 : 2;
    if (reg == 0xc0)
        return shadow[registers[10]];
    return registers[reg & 0x7f];
}

void chip_write(uint8_t reg, uint8_t value)
{
    if (power_off)
        return;
    registers[reg] = value;
    if (reg == 4 && value == 4) {
        ++clears;
        if (!clear_stuck)
            registers[4] = 0;
    }
    if (reg == 11)
        shadow[registers[10]] = value;
    if (reg == 8 && value == 0x50)
        memcpy(shadow, nvm[registers[9]], 54);
    if (reg == 8 && value == 0xa0) {
        ++programs;
        memcpy(nvm[registers[9]], shadow, 54);
        if (corrupt) {
            nvm[registers[9]][0] ^= 1;
            if (corrupt > 0)
                --corrupt;
        }
    }
    if (reg == 7) {
        unsigned address = registers[5] * 256 + registers[6];
        assert(address < 480);
        display[address] = value;
        ++write_count;
    }
}

void test_osd_clear(void *object)
{
    assert(object == test_osd_object);
    ++clears;
}

void test_uart_write(const uint8_t *data, uint32_t size)
{
    assert(size <= sizeof(ack));
    memcpy(ack, data, size);
    ack_size = size;
    unsigned crc = 0;
    for (unsigned i = 3; i < size; ++i)
        crc ^= ack[i];
    assert(crc == 0);
}

static void send(const uint8_t *payload, unsigned size)
{
    uint8_t frame[70] = {0};
    frame[1] = size;
    frame[3] = 182;
    memcpy(frame + 6, payload, size);
    bridge_process(frame);
}

int main(void)
{
    registers[0] = 12;
    send((uint8_t[]){133}, 1);
    assert(ack_size == 10 && ack[5] == 133 && ack[6] == 0 && ack[7] == 1 && ack[8] == 0);
    send((uint8_t[]){130, 69}, 2);
    assert(ack[5] == 130 && ack[6] == 0 && led_color_calls == 1 && led_color == 69);
    send((uint8_t[]){131, 1}, 2);
    assert(ack[5] == 131 && ack[6] == 0 && led_console_calls == 1 &&
           !strcmp(led_console, "set led_overlay 1"));
    send((uint8_t[]){131, 3}, 2);
    assert(led_console_calls == 2 && !strcmp(led_console, "set led_overlay 3"));
    send((uint8_t[]){132}, 1);
    assert(ack[5] == 132 && ack[6] == 0 && led_advance_calls == 1);
    const char setting[] = "set gif_animation_index 2";
    uint8_t setting_payload[64] = {134};
    memcpy(setting_payload + 1, setting, sizeof(setting) - 1);
    send(setting_payload, sizeof(setting));
    assert(ack[5] == 134 && ack[6] == 0 && led_console_calls == 3 && !strcmp(led_console, setting));
    const char rejected[] = "flash_repartition";
    memcpy(setting_payload + 1, rejected, sizeof(rejected) - 1);
    send(setting_payload, sizeof(rejected));
    assert(ack[5] == 134 && ack[6] == 1 && led_console_calls == 3);
    memcpy(setting_payload + 1, setting, sizeof(setting) - 1);
    send((uint8_t[]){131, 0}, 2);
    assert(ack[6] == 1 && led_console_calls == 3);
    test_uart3_lock = 1;
    send((uint8_t[]){130, 1}, 2);
    assert(ack[6] == 2 && led_color_calls == 1);
    send((uint8_t[]){131, 5}, 2);
    assert(ack[6] == 2 && led_console_calls == 3);
    send((uint8_t[]){132}, 1);
    assert(ack[6] == 2 && led_advance_calls == 1);
    send(setting_payload, sizeof(setting));
    assert(ack[6] == 2 && led_console_calls == 3);
    test_uart3_lock = 0;
    uint8_t file_request[46] = {135, 1, 0x51, 0x73, 0, 7, 0, 0, 32, 0};
    ack_size = 0;
    send(file_request, 14);
    assert(!ack_size && file_calls == 1 && file_size == 13);
    assert(file_packet[2] == 0xe0 && file_packet[3] == 0x50 && file_packet[4] == 0x40);
    assert(!memcmp(file_packet + 5, file_request + 1, 13));
    uint8_t file_response[53] = {0, 48, 0xe1, 0x40, 0x50, 1, 0x51, 0x73, 0, 7};
    file_forward_response(0, file_response);
    assert(ack_size == 55 && ack[5] == 135 && !memcmp(ack + 6, file_response + 5, 48));
    ack_size = 0;
    file_response[4] = 0x51;
    file_forward_response(0, file_response);
    assert(!ack_size);
    file_response[4] = 0x50;
    file_response[1] = 49;
    file_forward_response(0, file_response);
    assert(!ack_size);
    test_uart3_lock = 1;
    send(file_request, 14);
    assert(ack[6] == 2 && file_calls == 1);
    test_uart3_lock = 0;
    file_send_failed = 1;
    send(file_request, 14);
    assert(ack[6] == 2 && file_calls == 2);
    file_send_failed = 0;
    file_request[8] = 33;
    send(file_request, 14);
    assert(ack[6] == 1 && file_calls == 2);
    file_request[8] = 32;
    file_request[4] = 2;
    ack_size = 0;
    send(file_request, 46);
    assert(!ack_size && file_calls == 3 && file_size == 45);
    send(file_request, 45);
    assert(ack[6] == 1 && file_calls == 3);
    file_request[4] = 1;
    file_request[6] = 1;
    send(file_request, 46);
    assert(ack[6] == 1 && file_calls == 3);
    file_request[6] = 0;
    file_busy = 1;
    send(file_request, 46);
    assert(ack[6] == 2 && file_calls == 3);
    file_busy = 0;
    file_wait_success = 0;
    send(file_request, 46);
    assert(ack[6] == 3 && file_calls == 4 && test_uart3_lock == 0);
    file_wait_success = 1;
    ack_size = 0;
    send(file_request, 46);
    assert(!ack_size && file_calls == 5 && test_uart3_lock == 0);
    send((uint8_t[]){3, 1, 2, 0, 0, 255, 6, 0x9b}, 8);
    assert(!memcmp(display + 32, (uint8_t[]){0, 255, 6, 0x9b}, 4));
    assert(test_osd_debug == 0 && test_osd_previous == 0 && write_count == 4);
    send((uint8_t[]){3, 16, 2, 0, 'X'}, 5);
    assert(write_count == 4);
    send((uint8_t[]){3, 0, 29, 0, 'A', 'B'}, 6);
    assert(display[29] == 'A' && write_count == 5);
    send((uint8_t[]){126}, 1);
    assert(ack_size == 10 && ack[6] == 6);
    send((uint8_t[]){123, 0, 32, 16}, 4);
    assert(registers[2] == 32 && registers[3] == 16);
    uint8_t data[56] = {122, 42};
    for (unsigned i = 0; i < 54; ++i)
        data[i + 2] = i * 3;
    send(data, 56);
    assert(ack_size == 9 && ack[6] == 0 && ack[7] == 42 && registers[0] == 12);
    send((uint8_t[]){121, 42}, 2);
    assert(ack_size == 63 && ack[6] == 0);
    assert(!memcmp(ack + 8, data + 2, 54));
    corrupt = 1;
    unsigned before = programs;
    send(data, 56);
    assert(ack[6] == 0 && programs == before + 2);
    corrupt = -1;
    before = programs;
    send(data, 56);
    assert(ack[6] == 1 && programs == before + 3);
    corrupt = 0;
    stuck_busy = 1;
    send(data, 56);
    assert(ack[6] == 1 && registers[0] == 12);
    stuck_busy = 0;
    send((uint8_t[]){1}, 1);
    assert(clears == 1 && test_osd_debug == 2 && test_osd_previous == 2);
    unsigned nvm_before = programs;
    power_off = 1;
    registers[0] = 0;
    send((uint8_t[]){123, 0, 32, 16}, 4);
    assert(registers[0] == 0);
    power_off = 0;
    reset_busy = 1;
    send((uint8_t[]){123, 0, 32, 16}, 4);
    assert(registers[0] == 0);
    reset_busy = 0;
    send((uint8_t[]){123, 0, 32, 16}, 4);
    assert(registers[0] == 12 && registers[1] == 0x4c);
    for (unsigned i = 0; i < 16; i++)
        assert(registers[0x10 + i] == 2);
    send((uint8_t[]){123, 1, 34, 17}, 4);
    assert(registers[0] == 0x4c && registers[2] == 34 && registers[3] == 17);
    registers[0] = 0;
    send((uint8_t[]){123, 0, 32, 16}, 4);
    assert(registers[0] == 12 && programs == nvm_before);
    clear_stuck = 1;
    send((uint8_t[]){2}, 1);
    assert(clears == 2);
    power_off = 1;
    send((uint8_t[]){2}, 1);
    assert(clears == 2);
}
