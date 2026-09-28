#include "tama_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "tamalib.h"

_Static_assert(sizeof(u12_t) == sizeof(uint16_t), "TamaLib u12_t must be 16-bit");
_Static_assert(sizeof(timestamp_t) == sizeof(uint32_t), "TamaLib timestamp must wrap at 32-bit");

static tama_port_backend_t g_backend;
static uint8_t g_lcd[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
static uint8_t g_icons[TAMA_ICON_COUNT];
static uint8_t g_buttons;
static uint32_t g_buzzer_frequency_decihz;
static int g_buzzer_enabled;
static int g_attached;
static int g_running;
static int g_stop_requested;

static void *port_malloc(u32_t size)
{
    return malloc((size_t)size);
}

static void port_free(void *ptr)
{
    free(ptr);
}

static void port_halt(void)
{
    g_running = 0;
    if (g_backend.halt != NULL) {
        g_backend.halt(g_backend.context);
    }
}

static bool_t port_is_log_enabled(log_level_t level)
{
    return (g_backend.log_line != NULL && level == LOG_ERROR) ? 1u : 0u;
}

static void port_log(log_level_t level, char *format, ...)
{
    char line[192];
    va_list args;

    if (g_backend.log_line == NULL) {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    line[sizeof(line) - 1u] = '\0';
    g_backend.log_line(g_backend.context, (int)level, line);
}

static void port_sleep_until(timestamp_t deadline)
{
    g_backend.sleep_until_us(g_backend.context, (uint32_t)deadline);
}

static timestamp_t port_get_timestamp(void)
{
    return (timestamp_t)g_backend.get_timestamp_us(g_backend.context);
}

static void port_update_screen(void)
{
    if (g_backend.present_lcd != NULL) {
        g_backend.present_lcd(g_backend.context, g_lcd, g_icons);
    }
}

static void port_set_lcd_matrix(u8_t x, u8_t y, bool_t value)
{
    if (x < TAMA_LCD_WIDTH && y < TAMA_LCD_HEIGHT) {
        g_lcd[y][x] = value ? 1u : 0u;
    }
}

static void port_set_lcd_icon(u8_t icon, bool_t value)
{
    if (icon < TAMA_ICON_COUNT) {
        g_icons[icon] = value ? 1u : 0u;
    }
}

static void port_set_frequency(u32_t frequency_decihz)
{
    g_buzzer_frequency_decihz = (uint32_t)frequency_decihz;
    if (g_backend.set_buzzer != NULL) {
        g_backend.set_buzzer(g_backend.context,
                             g_buzzer_frequency_decihz,
                             g_buzzer_enabled);
    }
}

static void port_play_frequency(bool_t enabled)
{
    g_buzzer_enabled = enabled ? 1 : 0;
    if (g_backend.set_buzzer != NULL) {
        g_backend.set_buzzer(g_backend.context,
                             g_buzzer_frequency_decihz,
                             g_buzzer_enabled);
    }
}

static int port_handler(void)
{
    return g_stop_requested;
}

static hal_t g_port_hal = {
    .malloc = port_malloc,
    .free = port_free,
    .halt = port_halt,
    .is_log_enabled = port_is_log_enabled,
    .log = port_log,
    .sleep_until = port_sleep_until,
    .get_timestamp = port_get_timestamp,
    .update_screen = port_update_screen,
    .set_lcd_matrix = port_set_lcd_matrix,
    .set_lcd_icon = port_set_lcd_icon,
    .set_frequency = port_set_frequency,
    .play_frequency = port_play_frequency,
    .handler = port_handler,
};

int tama_port_attach(const tama_port_backend_t *backend)
{
    if (backend == NULL ||
        backend->get_timestamp_us == NULL ||
        backend->sleep_until_us == NULL) {
        return -1;
    }

    g_backend = *backend;
    memset(g_lcd, 0, sizeof(g_lcd));
    memset(g_icons, 0, sizeof(g_icons));
    g_buttons = 0u;
    g_buzzer_frequency_decihz = 0u;
    g_buzzer_enabled = 0;
    g_running = 0;
    g_stop_requested = 0;
    tamalib_register_hal(&g_port_hal);
    g_attached = 1;
    return 0;
}

int tama_port_start(const uint16_t *program_words, size_t word_count)
{
    if (!g_attached || program_words == NULL || word_count != TAMA_ROM_WORD_COUNT) {
        return -1;
    }

    g_stop_requested = 0;
    g_running = tamalib_init((const u12_t *)program_words, NULL, 1000000u) == 0;
    return g_running ? 0 : -1;
}

void tama_port_release(void)
{
    if (g_attached) {
        tamalib_release();
    }
    g_running = 0;
}

int tama_port_step(void)
{
    if (!g_running || g_stop_requested) {
        return 0;
    }
    tamalib_step();
    return g_running;
}

void tama_port_request_stop(void)
{
    g_stop_requested = 1;
}

int tama_port_stop_requested(void)
{
    return g_stop_requested;
}

void tama_port_set_buttons(uint8_t pressed_mask)
{
    const uint8_t changed = (uint8_t)(g_buttons ^ pressed_mask);

    if (changed & TAMA_BUTTON_A) {
        tamalib_set_button(BTN_LEFT,
                           (pressed_mask & TAMA_BUTTON_A) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    }
    if (changed & TAMA_BUTTON_B) {
        tamalib_set_button(BTN_MIDDLE,
                           (pressed_mask & TAMA_BUTTON_B) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    }
    if (changed & TAMA_BUTTON_C) {
        tamalib_set_button(BTN_RIGHT,
                           (pressed_mask & TAMA_BUTTON_C) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    }
    g_buttons = pressed_mask;
}

void tama_port_present_now(void)
{
    port_update_screen();
}

const uint8_t (*tama_port_lcd_matrix(void))[TAMA_LCD_WIDTH]
{
    return g_lcd;
}

const uint8_t *tama_port_lcd_icons(void)
{
    return g_icons;
}

uint32_t tama_port_buzzer_frequency_decihz(void)
{
    return g_buzzer_frequency_decihz;
}

int tama_port_buzzer_enabled(void)
{
    return g_buzzer_enabled;
}
