#ifndef JACK_TAMA_PORT_H
#define JACK_TAMA_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAMA_LCD_WIDTH 32u
#define TAMA_LCD_HEIGHT 16u
#define TAMA_ICON_COUNT 8u
#define TAMA_ROM_WORD_COUNT 8192u

enum {
    TAMA_BUTTON_A = 1u << 0,
    TAMA_BUTTON_B = 1u << 1,
    TAMA_BUTTON_C = 1u << 2,
};

typedef struct {
    void *context;
    uint32_t (*get_timestamp_us)(void *context);
    void (*sleep_until_us)(void *context, uint32_t deadline_us);
    void (*present_lcd)(void *context,
                        const uint8_t matrix[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH],
                        const uint8_t icons[TAMA_ICON_COUNT]);
    void (*set_buzzer)(void *context, uint32_t frequency_decihz, int enabled);
    void (*log_line)(void *context, int level, const char *line);
    void (*halt)(void *context);
} tama_port_backend_t;

int tama_port_attach(const tama_port_backend_t *backend);
int tama_port_start(const uint16_t *program_words, size_t word_count);
void tama_port_release(void);

int tama_port_step(void);
void tama_port_request_stop(void);
int tama_port_stop_requested(void);

void tama_port_set_buttons(uint8_t pressed_mask);
void tama_port_present_now(void);
const uint8_t (*tama_port_lcd_matrix(void))[TAMA_LCD_WIDTH];
const uint8_t *tama_port_lcd_icons(void);
uint32_t tama_port_buzzer_frequency_decihz(void);
int tama_port_buzzer_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
