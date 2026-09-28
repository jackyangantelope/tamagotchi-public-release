#pragma once

#include <stdint.h>

struct TamaBuzzerAudio {
    uint32_t last_tick = 0;
    uint32_t tick_remainder = 0;
    uint32_t phase = 0;
    uint32_t frequency_decihz = 0;
    uint32_t enabled = 0;
};

// TamaLib ticks stay at 32768 Hz even when its CPU clock changes.
__attribute__((noinline)) inline uint32_t tama_buzzer_samples_due(
    TamaBuzzerAudio *audio, uint32_t tick)
{
    const uint64_t scaled = static_cast<uint64_t>(tick - audio->last_tick) * 44100u +
                            audio->tick_remainder;
    audio->last_tick = tick;
    audio->tick_remainder = static_cast<uint32_t>(scaled & 32767u);
    return static_cast<uint32_t>(scaled >> 15u);
}

// The HAL reports frequency in tenths of a hertz.
__attribute__((noinline)) inline void tama_buzzer_render(
    TamaBuzzerAudio *audio, int16_t *pcm, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t sample = 0;
        if (audio->enabled && audio->frequency_decihz) {
            sample = audio->phase < 220500u ? 6000 : -6000;
            audio->phase += audio->frequency_decihz;
            if (audio->phase >= 441000u) audio->phase -= 441000u;
        }
        pcm[i * 2u] = pcm[i * 2u + 1u] = sample;
    }
}
