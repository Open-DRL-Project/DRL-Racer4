#include "ec_abi.h"
#include <stdint.h>
#include "osd_chip.h"

typedef void (*uart_write_t)(const uint8_t *, uint32_t);

#ifdef BRIDGE_HOST_TEST
extern uint8_t test_osd_object[32];
extern volatile uint8_t test_osd_debug;
extern volatile uint8_t test_osd_previous;
extern volatile uint8_t test_uart3_lock;
extern void test_uart_write(const uint8_t *, uint32_t);
extern void test_send_led_color(uint8_t);
extern void test_send_led_advance(void);
extern void test_send_led_remote_console(const char *, int);
#define EC_OSD_OBJECT ((void *)test_osd_object)
#define EC_OSD_DEBUG test_osd_debug
#define EC_OSD_PREVIOUS test_osd_previous
#define EC_UART3_LOCK test_uart3_lock
static const uart_write_t uart_write = test_uart_write;
#define send_led_color test_send_led_color
#define send_led_advance test_send_led_advance
#define send_led_remote_console test_send_led_remote_console
#else
#define EC_OSD_OBJECT ((void *)EC_OSD_OBJECT_ADDR)
#define EC_OSD_DEBUG (*(volatile uint8_t *)EC_OSD_MODE)
#define EC_OSD_PREVIOUS (*(volatile uint8_t *)EC_OSD_PREVIOUS_MODE)
#define EC_UART3_LOCK (*(volatile uint8_t *)EC_UART_MUX_LOCK)

static const uart_write_t uart_write = (uart_write_t)EC_FC_UART_WRITE;

static void (*const send_led_color)(uint8_t) = (void (*)(uint8_t))EC_LED_COLOR_SEND;
static void (*const send_led_advance)(void) = (void (*)(void))EC_LED_ADVANCE_SEND;
static void (*const send_led_remote_console)(const char *,
                                             int) = (void (*)(const char *,
                                                              int))EC_LED_CONSOLE_SEND;
#endif

static void set_mode(uint8_t mode)
{
    // Keep these mode bytes in sync or the stock OSD task clears the Betaflight display.
    EC_OSD_PREVIOUS = mode;
    EC_OSD_DEBUG = mode;
}

void reply_payload(const uint8_t *payload, uint8_t size)
{
    uint8_t reply[70];
    reply[0] = '$';
    reply[1] = 'M';
    reply[2] = '>';
    reply[3] = size;
    reply[4] = 182;
    uint8_t crc = size ^ 182;
    for (uint8_t i = 0; i < size; ++i) {
        reply[5 + i] = payload[i];
        crc ^= payload[i];
    }
    reply[5 + size] = crc;
    uart_write(reply, size + 6);
}

void bridge_clear(void *object)
{
    if (chip_read(0x80) == 0xff)
        return;
    chip_write(4, 4);
    for (unsigned i = 0; i < 1000; ++i) {
        if (!(chip_read(0x84) & 4))
            break;
    }
    ((uint8_t *)object)[0x1c] = 0;
}

static void clear_screen(void)
{
    bridge_clear(EC_OSD_OBJECT);
}

static int equal_name(const uint8_t *name, unsigned size, const char *expected)
{
    unsigned i = 0;
    while (expected[i]) {
        if (i >= size || name[i] != (uint8_t)expected[i])
            return 0;
        ++i;
    }
    return i == size;
}

static int allowed_led_setting(const uint8_t *text, unsigned size)
{
    static const char *const names[] = {
        "animation_index", "custom_color",    "custom_animation", "gif_animation_index",
        "gif_post_blank",  "render_from_sd",  "animation_state",  "animation_frame_rate",
        "led_brightness",  "gif_frame_delay", "led_color_map"};
    if (size < 7 || size > 62 || text[0] != 's' || text[1] != 'e' || text[2] != 't' ||
        text[3] != ' ')
        return 0;
    unsigned end = 4;
    while (end < size && text[end] != ' ')
        ++end;
    if (end == 4 || end + 1 >= size)
        return 0;
    int known = 0;
    for (unsigned n = 0; n < sizeof(names) / sizeof(names[0]); ++n) {
        if (equal_name(text + 4, end - 4, names[n]))
            known = 1;
    }
    if (!known)
        return 0;
    for (unsigned i = end + 1; i < size; ++i) {
        const uint8_t ch = text[i];
        if (!((ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '.' || ch == ','))
            return 0;
    }
    return 1;
}

void bridge_process(const uint8_t *frame)
{
    const uint8_t size = frame[1];
    if (!size || size > 64) {
        return;
    }
    const uint8_t *payload = frame + 6;
    if (payload[0] == 143 || payload[0] == 144) {
        extern void receiver_request(const uint8_t *, unsigned);
        receiver_request(payload, size);
        return;
    }
    if (size && (payload[0] >= 140 && payload[0] <= 142)) {
        extern void sa_request(const uint8_t *, unsigned);
        sa_request(payload, size);
        return;
    }
    const uint8_t subcommand = payload[0];
    if (subcommand == 136) {
        extern void control_forward_request(const uint8_t *, unsigned);
        control_forward_request(payload, size);
        return;
    }
    if (subcommand == 135) {
        extern void file_forward_request(const uint8_t *, unsigned);
        file_forward_request(payload, size);
        return;
    }

    if (subcommand == 126 && size == 1) {
        const uint8_t answer[] = {127, 6, EC_OSD_DEBUG, EC_OSD_PREVIOUS};
        reply_payload(answer, sizeof(answer));
        return;
    }

    if (subcommand >= 130 && subcommand <= 134) {
        uint8_t status = 1;
        if (subcommand == 133 && size == 1) {
            const uint8_t answer[] = {133, 0, 1, EC_UART3_LOCK};
            reply_payload(answer, sizeof(answer));
            return;
        }
        if (subcommand == 130 && size == 2) {
            status = EC_UART3_LOCK ? 2 : 0;
            if (!status)
                send_led_color(payload[1]);
        } else if (subcommand == 131 && size == 2 && payload[1] >= 1 && payload[1] <= 5) {
            static const char command[] = "set led_overlay 0";
            char selected[sizeof(command)];
            for (unsigned i = 0; i < sizeof(command); ++i)
                selected[i] = command[i];
            selected[16] = (char)('0' + payload[1]);
            status = EC_UART3_LOCK ? 2 : 0;
            if (!status)
                send_led_remote_console(selected, 17);
        } else if (subcommand == 132 && size == 1) {
            status = EC_UART3_LOCK ? 2 : 0;
            if (!status)
                send_led_advance();
        } else if (subcommand == 134 && allowed_led_setting(payload + 1, size - 1)) {
            status = EC_UART3_LOCK ? 2 : 0;
            if (!status)
                send_led_remote_console((const char *)(payload + 1), size - 1);
        }
        const uint8_t answer[] = {subcommand, status};
        reply_payload(answer, sizeof(answer));
        return;
    }

    if (subcommand == 120 && size == 1) {
        const uint8_t answer[] = {
            120, 0, chip_read(0x80), chip_read(0x82), chip_read(0x83), chip_read(0xa0)};
        reply_payload(answer, sizeof(answer));
        return;
    }
    if ((subcommand == 121 && size == 2) || (subcommand == 122 && size == 56)) {
        uint8_t answer[57];
        for (unsigned i = 0; i < sizeof(answer); ++i)
            answer[i] = 0;
        answer[0] = subcommand;
        answer[1] = 1;
        answer[2] = payload[1];
        uint8_t vm0 = chip_read(0x80);

        if (vm0 != 0xff && chip_wait_ready()) {
            chip_write(0, vm0 & ~8u);
            answer[1] = !(subcommand == 121 ? font_read(payload[1], answer + 3)
                                            : font_write(payload[1], payload + 2));
            chip_write(0, vm0);
        }
        reply_payload(answer, subcommand == 121 ? 57 : 3);
        return;
    }
    if (subcommand == 123 && size == 4) {
        if (payload[1] > 1 || payload[2] > 63 || payload[3] > 31)
            return;

        chip_configure(payload[1], payload[2], payload[3]);
        return;
    }

    if (subcommand == 0 || subcommand == 2 || subcommand == 3 || subcommand == 4) {

        set_mode(0);
    }

    if (subcommand == 1) {
        set_mode(2);
        clear_screen();
        return;
    }
    if (subcommand == 2) {
        clear_screen();
        return;
    }
    if (subcommand != 3 || size < 5) {
        return;
    }

    const uint8_t row = payload[1];
    const uint8_t col = payload[2];

    if (row >= 16 || col >= 30) {
        return;
    }
    uint8_t count = (uint8_t)(size - 4);
    if (count > 30 - col) {
        count = (uint8_t)(30 - col);
    }
    draw_glyphs(payload + 4, count, col, row);
}
