#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "cpu.h"
}
#include "ff.h"
#include "jack_wave1_audio.h"
#include "jack_wave1_runtime.h"
#include "jack_wave1_perf.h"
#include "pico/stdlib.h"
#include "tama_port.h"
#include "tama_buzzer_audio.h"

namespace {
constexpr uint16_t kBackground = 0x0842;
constexpr uint16_t kBezel = 0x2d6b;
constexpr uint16_t kLcdOff = 0x4630;
constexpr uint16_t kLcdOn = 0x0862;
constexpr uint32_t kSaveIntervalUs = 60000000u;

bool g_ui_initialized = false;
uint8_t g_previous_lcd[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
uint8_t g_previous_icons[TAMA_ICON_COUNT];

struct InterruptSave {
    uint8_t factor;
    uint8_t mask;
    uint8_t triggered;
    uint8_t vector;
};

struct TamaSave {
    char magic[8];
    uint16_t pc;
    uint16_t x;
    uint16_t y;
    uint8_t a;
    uint8_t b;
    uint8_t np;
    uint8_t sp;
    uint8_t flags;
    uint8_t prog_timer_enabled;
    uint8_t prog_timer_data;
    uint8_t prog_timer_rld;
    uint8_t cpu_halted;
    uint32_t tick_counter;
    uint32_t timers[9];
    uint32_t call_depth;
    InterruptSave interrupts[INT_SLOT_NUM];
    uint8_t memory[MEM_BUFFER_SIZE];
};

uint16_t g_program[TAMA_ROM_WORD_COUNT];
char g_save_path[192];

TamaBuzzerAudio g_buzzer{};
int16_t g_audio[128u * 2u]{};
uint32_t g_audio_pending = 0;
uint32_t g_buzzer_events = 0;
bool g_audio_started = false;

void service_audio()
{
    if (!g_audio_started) return;
    uint32_t due = tama_buzzer_samples_due(&g_buzzer, *cpu_get_state()->tick_counter);
    while (due) {
        const uint32_t room = 128u - g_audio_pending;
        const uint32_t count = due < room ? due : room;
        tama_buzzer_render(&g_buzzer, g_audio + g_audio_pending * 2u, count);
        g_audio_pending += count;
        due -= count;
        if (g_audio_pending == 128u) {
            jack_wave1_audio_submit(g_audio, 128u);
            g_audio_pending = 0;
            (void)jack_wave1_audio_wait();
        }
    }
}

void __attribute__((noinline)) play_output_test()
{

    TamaBuzzerAudio probe{};
    const uint32_t lengths[] = {1536, 8820, 4410, 8820, 1470};
    printf("TAMA_AUDIO_TEST begin output=HDMI channels=both tones=660,880Hz\n");
    for (unsigned part = 0; part < 5; ++part) {
        probe.enabled = part == 1 || part == 3;
        probe.frequency_decihz = part == 3 ? 8800u : 6600u;
        probe.phase = 0;
        uint32_t remaining = lengths[part];
        while (remaining) {
            const uint32_t count = remaining < 128u ? remaining : 128u;
            tama_buzzer_render(&probe, g_audio, count);
            jack_wave1_audio_submit(g_audio, count);
            (void)jack_wave1_audio_wait();
            jack_wave1_usb_service();
            remaining -= count;
        }
    }
    printf("TAMA_AUDIO_TEST end\n");
}

void report_audio()
{
    static uint32_t report_start = 0;
    const uint32_t now = time_us_32();
    if (now - report_start < 5000000u) return;
    report_start = now;
    jack_wave1_audio_report();
    printf("TAMA_BUZZER events=%lu frequency_decihz=%lu enabled=%lu tick=%lu pending=%lu\n",
           (unsigned long)g_buzzer_events, (unsigned long)g_buzzer.frequency_decihz,
           (unsigned long)g_buzzer.enabled, (unsigned long)g_buzzer.last_tick,
           (unsigned long)g_audio_pending);
}

uint32_t backend_timestamp(void *) { return time_us_32(); }

void backend_sleep(void *, uint32_t deadline)
{
    static uint32_t next_usb_service = 0;
    while (static_cast<int32_t>(deadline - time_us_32()) > 0) {
        const uint32_t now = time_us_32();
        if (static_cast<int32_t>(now - next_usb_service) >= 0) {
            jack_wave1_usb_service();
            next_usb_service = now + 750u;
        }
        tight_loop_contents();
    }
}

void backend_present(void *,
                     const uint8_t matrix[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH],
                     const uint8_t icons[TAMA_ICON_COUNT])
{
    if (!g_ui_initialized) {
        jack_wave1_clear(kBackground);
        jack_wave1_fill_rect(24, 40, 272, 152, kBezel);
        jack_wave1_draw_text(8, 8, "PAD A/B/X  KEY A/B/X = LEFT/MID/RIGHT",
                             0x7fff, kBackground);
    }
    for (unsigned y = 0; y < TAMA_LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < TAMA_LCD_WIDTH; ++x) {
            if (!g_ui_initialized || matrix[y][x] != g_previous_lcd[y][x]) {
                jack_wave1_fill_rect(32u + x * 8u, 48u + y * 8u, 8, 8,
                                     matrix[y][x] ? kLcdOn : kLcdOff);
                g_previous_lcd[y][x] = matrix[y][x];
            }
        }
    }
    for (unsigned icon = 0; icon < TAMA_ICON_COUNT; ++icon) {
        if (!g_ui_initialized || icons[icon] != g_previous_icons[icon]) {
            jack_wave1_fill_rect(48u + icon * 28u, 204, 16, 16,
                                 icons[icon] ? kLcdOn : kBezel);
            g_previous_icons[icon] = icons[icon];
        }
    }
    g_ui_initialized = true;
}

void backend_buzzer(void *, uint32_t frequency_decihz, int enabled)
{
    // Flush audio before changing the emulated buzzer state.
    service_audio();
    if (frequency_decihz != g_buzzer.frequency_decihz ||
        static_cast<uint32_t>(enabled != 0) != g_buzzer.enabled) ++g_buzzer_events;
    if (enabled && !g_buzzer.enabled) g_buzzer.phase = 0;
    g_buzzer.frequency_decihz = frequency_decihz;
    g_buzzer.enabled = enabled != 0;
}

void backend_log(void *, int level, const char *line)
{

    if (level != LOG_ERROR) return;
    printf("TAMA_LOG level=%d %s", level, line);
}

bool wait_for_next_video_frame()
{
    uint32_t frame = jack_wave1_frame_counter();
    while (!jack_wave1_wait_frame_boundary(frame)) frame = jack_wave1_frame_counter();
    return true;
}

void backend_halt(void *) { printf("TAMA_HALT\n"); }

bool load_rom(const char *path)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return false;
    const FSIZE_t size = f_size(&file);
    memset(g_program, 0, sizeof(g_program));
    bool ok = false;
    unsigned loaded_words = 0;
    if (size == TAMA_ROM_WORD_COUNT * 2u) {
        ok = true;
        for (unsigned i = 0; i < TAMA_ROM_WORD_COUNT && ok; ++i) {
            uint8_t bytes[2];
            UINT count = 0;
            ok = f_read(&file, bytes, sizeof(bytes), &count) == FR_OK && count == 2u;
            if (ok) g_program[i] = static_cast<uint16_t>(((bytes[0] << 8) | bytes[1]) & 0x0fffu);
        }
        loaded_words = ok ? TAMA_ROM_WORD_COUNT : 0u;
    } else if (size == 12288u) {
        // P1 ROM words are big-endian 16-bit containers, not packed 12-bit data.
        constexpr unsigned kP1RomWordCount = 6144u;
        ok = true;
        for (unsigned i = 0; i < kP1RomWordCount && ok; ++i) {
            uint8_t bytes[2];
            UINT count = 0;
            ok = f_read(&file, bytes, sizeof(bytes), &count) == FR_OK && count == 2u;
            if (ok) g_program[i] = static_cast<uint16_t>(((bytes[0] & 0x0fu) << 8) | bytes[1]);
        }
        loaded_words = ok ? kP1RomWordCount : 0u;
    }
    f_close(&file);
    printf("TAMA_ROM path=%s size=%lu format=%s words=%u op0100=%03x op01fe=%03x\n", path,
           static_cast<unsigned long>(size),
           size == 16384u ? "be12x8192" : size == 12288u ? "be12x6144" : "unsupported",
           loaded_words, g_program[0x0100], g_program[0x01fe]);
    return ok;
}

void make_save_path(const char *rom_path)
{
    const char *base = strrchr(rom_path, '/');
    base = base == nullptr ? rom_path : base + 1;
    char name[96];
    strncpy(name, base, sizeof(name) - 1u);
    name[sizeof(name) - 1u] = '\0';
    char *dot = strrchr(name, '.');
    if (dot != nullptr) *dot = '\0';
    snprintf(g_save_path, sizeof(g_save_path), "/SAVES/TAMAGOTCHI/%s.sav", name);
}

void capture_state(TamaSave *save)
{
    state_t *state = cpu_get_state();
    memcpy(save->magic, "JTAMA02", 8);
    save->pc = *state->pc; save->x = *state->x; save->y = *state->y;
    save->a = *state->a; save->b = *state->b; save->np = *state->np;
    save->sp = *state->sp; save->flags = *state->flags;
    save->tick_counter = *state->tick_counter;
    uint32_t *sources[9] = {
        state->clk_timer_2hz_timestamp, state->clk_timer_4hz_timestamp,
        state->clk_timer_8hz_timestamp, state->clk_timer_16hz_timestamp,
        state->clk_timer_32hz_timestamp, state->clk_timer_64hz_timestamp,
        state->clk_timer_128hz_timestamp, state->clk_timer_256hz_timestamp,
        state->prog_timer_timestamp,
    };
    for (unsigned i = 0; i < 9u; ++i) save->timers[i] = *sources[i];
    save->prog_timer_enabled = *state->prog_timer_enabled;
    save->prog_timer_data = *state->prog_timer_data;
    save->prog_timer_rld = *state->prog_timer_rld;
    save->call_depth = *state->call_depth;
    save->cpu_halted = *state->cpu_halted;
    for (unsigned i = 0; i < INT_SLOT_NUM; ++i) {
        save->interrupts[i] = {
            static_cast<uint8_t>(state->interrupts[i].factor_flag_reg),
            static_cast<uint8_t>(state->interrupts[i].mask_reg),
            static_cast<uint8_t>(state->interrupts[i].triggered),
            static_cast<uint8_t>(state->interrupts[i].vector),
        };
    }
    memcpy(save->memory, state->memory, MEM_BUFFER_SIZE);
}

void restore_state(const TamaSave *save)
{
    state_t *state = cpu_get_state();
    *state->pc = save->pc; *state->x = save->x; *state->y = save->y;
    *state->a = save->a; *state->b = save->b; *state->np = save->np;
    *state->sp = save->sp; *state->flags = save->flags;
    *state->tick_counter = save->tick_counter;
    uint32_t *destinations[9] = {
        state->clk_timer_2hz_timestamp, state->clk_timer_4hz_timestamp,
        state->clk_timer_8hz_timestamp, state->clk_timer_16hz_timestamp,
        state->clk_timer_32hz_timestamp, state->clk_timer_64hz_timestamp,
        state->clk_timer_128hz_timestamp, state->clk_timer_256hz_timestamp,
        state->prog_timer_timestamp,
    };
    for (unsigned i = 0; i < 9u; ++i) *destinations[i] = save->timers[i];
    *state->prog_timer_enabled = save->prog_timer_enabled;
    *state->prog_timer_data = save->prog_timer_data;
    *state->prog_timer_rld = save->prog_timer_rld;
    *state->call_depth = save->call_depth;
    *state->cpu_halted = save->cpu_halted;
    for (unsigned i = 0; i < INT_SLOT_NUM; ++i) {
        state->interrupts[i].factor_flag_reg = save->interrupts[i].factor;
        state->interrupts[i].mask_reg = save->interrupts[i].mask;
        state->interrupts[i].triggered = save->interrupts[i].triggered;
        state->interrupts[i].vector = save->interrupts[i].vector;
    }
    memcpy(state->memory, save->memory, MEM_BUFFER_SIZE);
    cpu_sync_ref_timestamp();
    cpu_refresh_hw();
}

bool save_state()
{
    TamaSave save{};
    capture_state(&save);
    (void)f_mkdir("/SAVES");
    (void)f_mkdir("/SAVES/TAMAGOTCHI");
    FIL file;
    if (f_open(&file, g_save_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    UINT written = 0;
    const bool ok = f_write(&file, &save, sizeof(save), &written) == FR_OK &&
                    written == sizeof(save) && f_sync(&file) == FR_OK;
    f_close(&file);
    printf("TAMA_SAVE path=%s ok=%d bytes=%u\n", g_save_path, ok, written);
    return ok;
}

bool load_state()
{
    FIL file;
    if (f_open(&file, g_save_path, FA_READ) != FR_OK) return false;
    TamaSave save{};
    UINT read = 0;
    const bool ok = f_read(&file, &save, sizeof(save), &read) == FR_OK &&
                    read == sizeof(save) && memcmp(save.magic, "JTAMA02", 8) == 0;
    f_close(&file);
    if (ok) restore_state(&save);
    printf("TAMA_LOAD_SAVE path=%s ok=%d\n", g_save_path, ok);
    return ok;
}
}

int main()
{
    if (!jack_wave1_init("Tamagotchi / TamaLib")) for (;;) tight_loop_contents();
    if (!jack_wave1_mount_sd()) {
        jack_wave1_show_error("TAMAGOTCHI", "SD mount failed");
        for (;;) tight_loop_contents();
    }
    char rom_path[192];
    if (!jack_wave1_select_file("SELECT TAMAGOTCHI ROM", "/roms/TAMAGOTCHI",
                                ".b .bin .rom", rom_path, sizeof(rom_path)) ||
        !load_rom(rom_path)) {
        jack_wave1_show_error("TAMAGOTCHI", "Need 12K or 16K BE ROM");
        for (;;) tight_loop_contents();
    }

    const tama_port_backend_t backend = {
        nullptr, backend_timestamp, backend_sleep, backend_present,
        backend_buzzer, backend_log, backend_halt,
    };
    if (tama_port_attach(&backend) != 0 ||
        tama_port_start(g_program, TAMA_ROM_WORD_COUNT) != 0) {
        jack_wave1_show_error("TAMAGOTCHI", "TamaLib start failed");
        for (;;) tight_loop_contents();
    }
    make_save_path(rom_path);
    (void)load_state();
    tama_port_present_now();
    if (!jack_wave1_audio_begin(44100u)) {
        jack_wave1_show_error("TAMAGOTCHI", "HDMI audio init failed");
        for (;;) tight_loop_contents();
    }
    play_output_test();
    g_buzzer.last_tick = *cpu_get_state()->tick_counter;
    g_buzzer.tick_remainder = 0;
    g_buzzer.phase = 0;
    g_audio_started = true;
    cpu_sync_ref_timestamp();

    uint32_t last_present = time_us_32();
    uint32_t last_save = last_present;
    uint16_t previous = 0;
    JackWave1Perf perf;
    perf.begin();
    uint32_t core_us = 0;
    printf("TAMA_BOOT version=0.4.0-fruit-jam-candidate rate=44100 channels=2 tick_rate=32768\n");
    for (;;) {
        const uint32_t core_start = time_us_32();
        if (!tama_port_step()) {
            jack_wave1_audio_mute(true);
            jack_wave1_show_error("TAMAGOTCHI", "Emulated CPU halted");
            for (;;) tight_loop_contents();
        }
        service_audio();
        const uint32_t now = time_us_32();
        core_us += now - core_start;
        if (static_cast<uint32_t>(now - last_present) >= 16667u) {
            const uint16_t buttons = jack_wave1_poll_buttons();
            uint8_t tama_buttons = 0;
            const uint16_t keyboard_tama = buttons &
                (JACK_KEYBOARD_A | JACK_KEYBOARD_B | JACK_KEYBOARD_X);
            if (keyboard_tama != 0u) {
                if ((keyboard_tama & JACK_KEYBOARD_A) != 0u) tama_buttons |= TAMA_BUTTON_A;
                if ((keyboard_tama & JACK_KEYBOARD_B) != 0u) tama_buttons |= TAMA_BUTTON_B;
                if ((keyboard_tama & JACK_KEYBOARD_X) != 0u) tama_buttons |= TAMA_BUTTON_C;
            } else {
                if ((buttons & JACK_BUTTON_A) != 0u) tama_buttons |= TAMA_BUTTON_A;
                if ((buttons & JACK_BUTTON_B) != 0u) tama_buttons |= TAMA_BUTTON_B;
                if ((buttons & (JACK_BUTTON_X | JACK_BUTTON_Y)) != 0u) tama_buttons |= TAMA_BUTTON_C;
            }
            tama_port_set_buttons(tama_buttons);
#ifndef JACK_WAVE1_QUIET_INPUT_LOG
            if (buttons != previous) {
                jack_wave1_usb_status_t status{};
                jack_wave1_usb_get_status(&status);
                printf("TAMA_INPUT buttons=%04x tama=%02x reports=%lu\n",
                       buttons, tama_buttons,
                       static_cast<unsigned long>(status.report_count));
            }
#endif
            if ((buttons & (JACK_BUTTON_SELECT | JACK_BUTTON_START)) ==
                    (JACK_BUTTON_SELECT | JACK_BUTTON_START) &&
                (previous & (JACK_BUTTON_SELECT | JACK_BUTTON_START)) !=
                    (JACK_BUTTON_SELECT | JACK_BUTTON_START)) {
                (void)save_state();
            }
            previous = buttons;
            const uint32_t wait_start = time_us_32();
            (void)wait_for_next_video_frame();
            const uint32_t wait_end = time_us_32();
            const uint32_t draw_start = wait_end;
            tama_port_present_now();
            const uint32_t draw_end = time_us_32();
            perf.sample("TAMA", core_us, draw_end - draw_start,
                        wait_end - wait_start, buttons,
                        "running", jack_wave1_perf_stdout);
            core_us = 0;
            last_present = now;
            report_audio();
        }
        if (static_cast<uint32_t>(now - last_save) >= kSaveIntervalUs) {
            (void)save_state();
            last_save = now;
        }
    }
}
