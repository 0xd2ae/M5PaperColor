/* Portable music-box / celesta wavetable synthesizer and ES8311 + AW8737A speaker driver.
 * All automatic sound triggers (hourly chime, boot chime, key clicks, anniversary music) default to OFF. */
#include "audio.h"
#include "buttons.h"
#include <math.h>
#include <string.h>

/* MIDI notes: C4=60, D4=62, E4=64, F4=65, F#4=66, G4=67, G#4=68, A4=69, Bb4=70, B4=71,
 *             C5=72, C#5=73, D5=74, D#5=75, E5=76, F5=77, F#5=78, G5=79, G#5=80, A5=81, Bb5=82, B5=83,
 *             C6=84, C#6=85, D6=86, E6=88, F6=89, F#6=90, G6=91, A6=93, B6=95, C7=96. */
enum {
    R = 0,
    G3 = 55, A3 = 57, B3 = 59,
    C4 = 60, CS4 = 61, D4 = 62, DS4 = 63, E4 = 64, F4 = 65, FS4 = 66, G4 = 67, GS4 = 68, A4 = 69, AS4 = 70, B4 = 71,
    C5 = 72, CS5 = 73, D5 = 74, DS5 = 75, E5 = 76, F5 = 77, FS5 = 78, G5 = 79, GS5 = 80, A5 = 81, AS5 = 82, B5 = 83,
    C6 = 84, CS6 = 85, D6 = 86, DS6 = 87, E6 = 88, F6 = 89, FS6 = 90, G6 = 91, A6 = 93, B6 = 95, C7 = 96
};

/* Short UI feedback sounds */
static const pc_note_t NOTES_KEY_PREV[] = {{E6, 0, 45}};
static const pc_note_t NOTES_KEY_NEXT[] = {{A6, 0, 45}};
static const pc_note_t NOTES_KEY_TODAY[] = {{C6, E5, 55}, {G6, C6, 95}};
static const pc_note_t NOTES_KEY_PAGE[] = {{C6, 0, 55}, {E6, 0, 55}, {G6, C5, 110}};
static const pc_note_t NOTES_KEY_SETUP[] = {{C6, 0, 55}, {E6, 0, 55}, {G6, 0, 55}, {C7, C6, 130}};
static const pc_note_t NOTES_BOOT[] = {{C5, C4, 90}, {G5, E4, 90}, {E6, G4, 110}, {C6, C5, 260}};

/* 1. Classic dual-tone hourly chime */
static const pc_note_t NOTES_BELL[] = {
    {C6, C5, 140}, {C7, E6, 420}
};

/* 2. Westminster Quarters (Big Ben chime) */
static const pc_note_t NOTES_WESTMINSTER[] = {
    {E5, C4, 360}, {C5, G4, 360}, {D5, G4, 360}, {G4, B3, 720},
    {G4, B3, 360}, {D5, G4, 360}, {E5, C4, 360}, {C5, E4, 800}
};

/* 3. Cuckoo clock chime */
static const pc_note_t NOTES_CUCKOO[] = {
    {G6, C5, 180}, {E6, G4, 320}, {R, 0, 120},
    {G6, C5, 180}, {E6, C5, 420}
};

/* 4. Happy Birthday to You (祝你生日快乐) */
static const pc_note_t NOTES_BIRTHDAY[] = {
    {G4, 0, 210}, {G4, 0, 110}, {A4, F4, 320}, {G4, E4, 320}, {C5, E4, 320}, {B4, D4, 620},
    {G4, 0, 210}, {G4, 0, 110}, {A4, F4, 320}, {G4, D4, 320}, {D5, F4, 320}, {C5, E4, 620},
    {G4, 0, 210}, {G4, 0, 110}, {G5, B4, 320}, {E5, C5, 320}, {C5, E4, 320}, {B4, G4, 320}, {A4, F4, 520},
    {F5, A4, 210}, {F5, A4, 110}, {E5, G4, 320}, {C5, E4, 320}, {D5, F4, 320}, {C5, E4, 720}
};

/* 5. Jasmine Flower (茉莉花 - 好一朵美丽的茉莉花) */
static const pc_note_t NOTES_JASMINE[] = {
    {E5, C4, 300}, {E5, G4, 150}, {G5, C5, 150}, {A5, E5, 150}, {C6, E5, 150}, {C6, A4, 150}, {A5, E5, 150},
    {G5, C4, 300}, {G5, E4, 150}, {A5, G4, 150}, {G5, C5, 580},
    {E5, C4, 300}, {E5, G4, 150}, {G5, C5, 150}, {A5, E5, 150}, {C6, E5, 150}, {C6, A4, 150}, {A5, E5, 150},
    {G5, C4, 300}, {G5, E4, 150}, {A5, G4, 150}, {G5, C5, 580},
    {G5, E4, 300}, {G5, G4, 300}, {G5, C5, 300}, {E5, G4, 150}, {G5, C5, 150},
    {A5, D4, 300}, {A5, A4, 300}, {G5, B4, 580},
    {E5, C4, 300}, {D5, G4, 150}, {E5, C5, 150}, {G5, G4, 300}, {E5, E4, 150}, {D5, G4, 150},
    {C5, C4, 300}, {C5, E4, 150}, {D5, G4, 150}, {C5, C4, 680}
};

/* 6. Happy New Year (新年好) */
static const pc_note_t NOTES_NEWYEAR[] = {
    {C5, E4, 180}, {C5, E4, 180}, {C5, G4, 360}, {G4, E4, 360},
    {E5, G4, 180}, {E5, G4, 180}, {E5, C5, 360}, {C5, E4, 360},
    {C5, E4, 180}, {E5, G4, 180}, {G5, C5, 360}, {G5, B4, 180}, {F5, A4, 180}, {E5, G4, 360}, {D5, F4, 520},
    {D5, F4, 180}, {E5, G4, 180}, {F5, A4, 360}, {F5, A4, 360},
    {E5, G4, 180}, {D5, F4, 180}, {E5, G4, 360}, {C5, E4, 360},
    {C5, E4, 180}, {E5, G4, 180}, {D5, F4, 360}, {G4, B3, 360}, {B4, D4, 180}, {D5, F4, 180}, {C5, E4, 680}
};

/* 7. Jingle Bells (铃儿响叮当) */
static const pc_note_t NOTES_JINGLE[] = {
    {E5, C4, 210}, {E5, G4, 210}, {E5, C5, 420},
    {E5, C4, 210}, {E5, G4, 210}, {E5, C5, 420},
    {E5, C4, 210}, {G5, E4, 210}, {C5, G4, 310}, {D5, B4, 110}, {E5, C5, 760},
    {F5, F4, 210}, {F5, A4, 210}, {F5, C5, 310}, {F5, A4, 110},
    {F5, F4, 210}, {E5, C4, 210}, {E5, G4, 210}, {E5, C5, 105}, {E5, 0, 105},
    {G5, G4, 210}, {G5, B4, 210}, {F5, D5, 210}, {D5, G4, 210}, {C5, E4, 720}
};

/* 8. Auld Lang Syne (友谊地久天长) */
static const pc_note_t NOTES_AULD[] = {
    {C5, G4, 300}, {F5, A4, 450}, {F5, C5, 150}, {F5, A4, 300}, {A5, C5, 300},
    {G5, C5, 450}, {F5, A4, 150}, {G5, C5, 300}, {A5, F4, 150}, {G5, 0, 150},
    {F5, A4, 450}, {F5, C5, 150}, {A5, F5, 300}, {C6, F5, 300}, {D6, F5, 750},
    {D6, F5, 300}, {C6, F5, 450}, {A5, C5, 150}, {A5, F5, 300}, {F5, A4, 300},
    {G5, C5, 450}, {F5, A4, 150}, {G5, C5, 300}, {A5, F4, 150}, {G5, 0, 150},
    {F5, A4, 450}, {D5, F4, 150}, {D5, G4, 300}, {C5, E4, 300}, {F5, A4, 780}
};

/* 9. Pachelbel's Canon in D (帕赫贝尔 D大调卡农) */
static const pc_note_t NOTES_CANON[] = {
    {FS6, D4, 300}, {A5, A4, 300}, {D6, FS5, 300}, {CS6, A4, 300},
    {B5, B3, 300}, {FS5, FS4, 300}, {B5, D5, 300}, {A5, FS4, 300},
    {G5, G3, 300}, {D5, D4, 300}, {G5, B4, 300}, {FS5, D4, 300},
    {E5, A3, 300}, {A5, E4, 300}, {G5, A4, 300}, {FS5, CS5, 300},
    {D5, D4, 240}, {FS5, A4, 240}, {A5, D5, 240}, {G5, B4, 240},
    {FS5, FS4, 240}, {D5, B4, 240}, {FS5, D5, 240}, {E5, CS5, 240},
    {D5, G4, 240}, {B4, D4, 240}, {D5, G4, 240}, {A5, FS4, 240},
    {G5, E4, 240}, {B5, G4, 240}, {A5, CS5, 240}, {G5, A4, 240},
    {FS5, D4, 760}
};

/* 10. Bridal Chorus / Wedding March (婚礼进行曲) */
static const pc_note_t NOTES_WEDDING[] = {
    {C5, G4, 300}, {F5, A4, 220}, {F5, A4, 100}, {F5, A4, 620},
    {C5, G4, 300}, {G5, AS4, 220}, {E5, G4, 100}, {F5, A4, 620},
    {C5, A4, 300}, {F5, A4, 220}, {A5, C5, 100}, {C6, F5, 320}, {A5, C5, 320},
    {G5, B4, 220}, {F5, A4, 100}, {E5, G4, 320}, {F5, A4, 240}, {G5, B4, 480},
    {C5, G4, 300}, {F5, A4, 220}, {A5, C5, 100}, {G5, C5, 320}, {E5, G4, 320},
    {D5, F4, 220}, {E5, G4, 100}, {F5, A4, 760}
};

/* 11. Beethoven's Ode to Joy (贝多芬 欢乐颂) */
static const pc_note_t NOTES_ODE[] = {
    {E5, C4, 260}, {E5, G4, 260}, {F5, A4, 260}, {G5, E4, 260},
    {G5, D4, 260}, {F5, G4, 260}, {E5, C5, 260}, {D5, G4, 260},
    {C5, C4, 260}, {C5, E4, 260}, {D5, G4, 260}, {E5, C5, 260},
    {E5, G4, 390}, {D5, F4, 130}, {D5, B3, 520},
    {E5, C4, 260}, {E5, G4, 260}, {F5, A4, 260}, {G5, E4, 260},
    {G5, D4, 260}, {F5, G4, 260}, {E5, C5, 260}, {D5, G4, 260},
    {C5, C4, 260}, {C5, E4, 260}, {D5, G4, 260}, {E5, G4, 260},
    {D5, G4, 390}, {C5, E4, 130}, {C5, C4, 680}
};

/* 12. Beethoven's Für Elise (贝多芬 致爱丽丝) */
static const pc_note_t NOTES_ELISE[] = {
    {E6, 0, 175}, {DS6, 0, 175}, {E6, 0, 175}, {DS6, 0, 175}, {E6, 0, 175},
    {B5, 0, 175}, {D6, 0, 175}, {C6, 0, 175}, {A5, A3, 350},
    {C5, E4, 175}, {E5, A4, 175}, {A5, C5, 175}, {B5, E4, 350},
    {E5, GS4, 175}, {GS5, B4, 175}, {B5, D5, 175}, {C6, A4, 350},
    {E5, A4, 175}, {E6, 0, 175}, {DS6, 0, 175}, {E6, 0, 175}, {DS6, 0, 175}, {E6, 0, 175},
    {B5, 0, 175}, {D6, 0, 175}, {C6, 0, 175}, {A5, A3, 350},
    {C5, E4, 175}, {E5, A4, 175}, {A5, C5, 175}, {B5, E4, 350},
    {E5, GS4, 175}, {C6, A4, 175}, {B5, G4, 175}, {A5, A4, 680}
};

/* 13. Brahms' Lullaby / Clair de Lune (勃拉姆斯 摇篮曲 / 温柔夜曲) */
static const pc_note_t NOTES_LULLABY[] = {
    {E5, C4, 220}, {E5, G4, 220}, {G5, E4, 580},
    {E5, C4, 220}, {E5, G4, 220}, {G5, E4, 580},
    {E5, C4, 220}, {G5, E4, 220}, {C6, E5, 420}, {B5, D5, 240}, {A5, C5, 360}, {A5, F4, 320}, {G5, E4, 360},
    {D5, G4, 220}, {E5, B4, 220}, {F5, D5, 420}, {D5, G4, 220}, {D5, G4, 220}, {E5, B4, 220}, {F5, D5, 520},
    {D5, G4, 220}, {F5, B4, 220}, {B5, D5, 220}, {A5, C5, 220}, {G5, B4, 360}, {B5, D5, 360}, {C6, E5, 760}
};

/* 14. Castle in the Sky / Carrying You (天空之城 伴着你) */
static const pc_note_t NOTES_CASTLE[] = {
    {A5, 0, 200}, {B5, 0, 200}, {C6, A4, 600}, {B5, E4, 200}, {C6, A4, 400}, {E6, C5, 400}, {B5, G4, 800},
    {E5, 0, 200}, {E5, 0, 200}, {A5, F4, 600}, {G5, C4, 200}, {A5, F4, 400}, {C6, A4, 400}, {G5, E4, 800},
    {E5, 0, 200}, {E5, 0, 200}, {F5, D4, 600}, {E5, A4, 200}, {F5, D4, 200}, {C6, A4, 600}, {E5, G4, 600},
    {R, 0, 150}, {C6, E4, 200}, {C6, E4, 200}, {C6, E4, 200},
    {B5, E4, 600}, {FS5, DS4, 200}, {FS5, D4, 400}, {B5, GS4, 400}, {B5, E4, 800}
};

#define ITEM(id_, name_, cat_, arr_) {id_, name_, cat_, arr_, sizeof(arr_) / sizeof(arr_[0])}
static const pc_melody_info_t MELODIES[] = {
    ITEM("bell",           "清脆双音报时",         "chime",   NOTES_BELL),
    ITEM("westminster",    "威斯敏斯特大本钟",     "chime",   NOTES_WESTMINSTER),
    ITEM("cuckoo",         "森林布谷鸟钟",         "chime",   NOTES_CUCKOO),
    ITEM("birthday",       "祝你生日快乐",         "classic", NOTES_BIRTHDAY),
    ITEM("jasmine",        "茉莉花",               "classic", NOTES_JASMINE),
    ITEM("newyear",        "新年好",               "classic", NOTES_NEWYEAR),
    ITEM("jingle",         "铃儿响叮当",           "classic", NOTES_JINGLE),
    ITEM("auld_lang_syne", "友谊地久天长",         "classic", NOTES_AULD),
    ITEM("canon",          "帕赫贝尔 · D大调卡农", "classic", NOTES_CANON),
    ITEM("wedding",        "婚礼进行曲",           "classic", NOTES_WEDDING),
    ITEM("ode_to_joy",     "贝多芬 · 欢乐颂",      "classic", NOTES_ODE),
    ITEM("fur_elise",      "贝多芬 · 致爱丽丝",    "classic", NOTES_ELISE),
    ITEM("clair_de_lune",  "勃拉姆斯 · 摇篮曲",    "classic", NOTES_LULLABY),
    ITEM("castle_sky",     "天空之城 · 伴着你",    "classic", NOTES_CASTLE),
    ITEM("_key_prev",      "按键 A",               "sfx",     NOTES_KEY_PREV),
    ITEM("_key_next",      "按键 B",               "sfx",     NOTES_KEY_NEXT),
    ITEM("_key_today",     "按键 C",               "sfx",     NOTES_KEY_TODAY),
    ITEM("_key_page",      "切换页面",             "sfx",     NOTES_KEY_PAGE),
    ITEM("_key_setup",     "进入配置",             "sfx",     NOTES_KEY_SETUP),
    ITEM("_boot",          "开机提示音",           "sfx",     NOTES_BOOT),
};

const pc_melody_info_t *pc_melody_lookup(const char *id)
{
    if (!id || !*id || !strcmp(id, "off")) return NULL;
    for (size_t i = 0; i < sizeof(MELODIES) / sizeof(MELODIES[0]); i++) {
        if (!strcmp(MELODIES[i].id, id)) return &MELODIES[i];
    }
    return NULL;
}
size_t pc_melody_count(void) { return sizeof(MELODIES) / sizeof(MELODIES[0]); }
const pc_melody_info_t *pc_melody_at(size_t index)
{
    return index < pc_melody_count() ? &MELODIES[index] : NULL;
}
bool pc_chime_hour_allowed(const pc_audio_config_t *cfg, int hour)
{
    if (!cfg || hour < 0 || hour > 23 || !pc_melody_lookup(cfg->chime)) return false;
    int s = cfg->chime_start, e = cfg->chime_end;
    if (s < 0 || s > 23) s = 8;
    if (e < 0 || e > 23) e = 21;
    return s <= e ? (hour >= s && hour <= e) : (hour >= s || hour <= e);
}

static float midi_freq(uint8_t note)
{
    return 440.0f * powf(2.0f, ((int)note - 69) / 12.0f);
}

#if defined(__GNUC__) && !defined(__clang__)
#define PC_AUDIO_FAST __attribute__((optimize("O2")))
#else
#define PC_AUDIO_FAST
#endif

/* Synthesizes a realistic music-box tine pluck (fundamental + decaying 2nd & 3rd harmonics + bell strike).
 * Uses a single-precision damped-phasor recurrence so ESP32-S3's hardware FPU renders a full song in <10 ms
 * instead of calling software-emulated double sin()/exp() millions of times. */
static PC_AUDIO_FAST void add_tine(int16_t *out, size_t max_samples, size_t start, uint8_t midi, uint16_t step_ms, float gain)
{
    if (!midi || gain <= 0.0f || start >= max_samples) return;
    float freq = midi_freq(midi);
    /* Music-box tines ring longer than the step interval, overlapping softly with the next note. */
    size_t ring_samples = (size_t)(PC_AUDIO_SAMPLE_RATE * ((float)step_ms * 1.45f + 140.0f) / 1000.0f);
    if (start + ring_samples > max_samples) ring_samples = max_samples - start;
    float w1 = 2.0f * 3.14159265358979323846f * freq / (float)PC_AUDIO_SAMPLE_RATE;
    float w2 = w1 * 2.0f, w3 = w1 * 4.0f;
    /* Keep harmonics below Nyquist. */
    float h2 = (freq * 2.0f < 7200.0f) ? 0.28f : 0.0f;
    float h3 = (freq * 4.0f < 7200.0f) ? 0.10f : 0.0f;
    size_t attack = PC_AUDIO_SAMPLE_RATE * 3 / 1000; /* 3 ms de-clicked attack */
    float inv_attack = 1.0f / (float)attack;
    float inv_release = 1.0f / 240.0f;
    float tau1 = (float)PC_AUDIO_SAMPLE_RATE * (0.24f + (float)step_ms * 0.00055f);
    float tau2 = tau1 * 0.35f, tau3 = (float)PC_AUDIO_SAMPLE_RATE * 0.025f;
    float r1 = expf(-1.0f / tau1), c1 = r1 * cosf(w1), s1 = r1 * sinf(w1);
    float r2 = h2 ? expf(-1.0f / tau2) : 0.0f, c2 = r2 * cosf(w2), s2 = r2 * sinf(w2);
    float r3 = h3 ? expf(-1.0f / tau3) : 0.0f, c3 = r3 * cosf(w3), s3 = r3 * sinf(w3);
    float amp = gain * 18500.0f;
    float x1 = amp, y1 = 0.0f;
    float x2 = amp * h2, y2 = 0.0f;
    float x3 = amp * h3, y3 = 0.0f;
    int16_t *dst = out + start;
    for (size_t i = 0; i < ring_samples; i++) {
        float env = (i < attack) ? ((float)i * inv_attack) : 1.0f;
        /* Smooth fade-out at the end of the ring window so there is never a truncation click. */
        if (i + 240 > ring_samples) env *= (float)(ring_samples - i) * inv_release;
        float sample = env * (y1 + y2 + y3);
        int32_t v = (int32_t)dst[i] + (int32_t)sample;
        dst[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        float nx1 = x1 * c1 - y1 * s1;
        y1 = x1 * s1 + y1 * c1;
        x1 = nx1;
        if (h2) {
            float nx2 = x2 * c2 - y2 * s2;
            y2 = x2 * s2 + y2 * c2;
            x2 = nx2;
        }
        if (h3) {
            float nx3 = x3 * c3 - y3 * s3;
            y3 = x3 * s3 + y3 * c3;
            x3 = nx3;
        }
    }
}

PC_AUDIO_FAST size_t pc_synth_render(const char *id, int volume_percent, int16_t *out, size_t max_samples)
{
    const pc_melody_info_t *m = pc_melody_lookup(id);
    if (!m || !m->count) return 0;
    size_t total_ms = 0;
    for (size_t i = 0; i < m->count; i++) total_ms += m->notes[i].ms;
    size_t needed = (size_t)((uint64_t)(total_ms + 220) * PC_AUDIO_SAMPLE_RATE / 1000);
    if (!out) return needed;
    size_t n = needed < max_samples ? needed : max_samples;
    memset(out, 0, n * sizeof(int16_t));
    int vol = volume_percent < 0 ? 0 : volume_percent > 100 ? 100 : volume_percent;
    float scale = (float)vol / 100.0f;
    size_t cursor = 0;
    for (size_t i = 0; i < m->count; i++) {
        const pc_note_t *note = &m->notes[i];
        if (note->lead) add_tine(out, n, cursor, note->lead, note->ms, 0.72f * scale);
        if (note->bass) add_tine(out, n, cursor, note->bass, note->ms, 0.36f * scale);
        cursor += (size_t)((uint64_t)note->ms * PC_AUDIO_SAMPLE_RATE / 1000);
    }
    return n;
}

#ifndef PC_HOST_CHECKS
#include "board.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio";
static i2s_chan_handle_t tx_chan;
static portMUX_TYPE req_lock = portMUX_INITIALIZER_UNLOCKED;
static char req_id[24];
static int req_vol = 60;
static volatile uint32_t req_gen, done_gen;
static volatile bool playing, shutting_down;

static void delay_ms(unsigned ms)
{
    /* With CONFIG_FREERTOS_HZ=100 (10 ms/tick), +1 guarantees at least `ms` elapsed even near a tick boundary. */
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

static esp_err_t es8311_setup(void)
{
    /* Slave DAC, 16-bit I2S, MCLK = 256*Fs = 4.096 MHz. Register values follow Espressif's
     * es8311 driver (Apache-2.0) for the 4096000 Hz / 16000 Hz coefficient row, with 0x32=0xcf
     * matching M5Unified on PaperColor to compensate for the 150 kOhm AW8737A input resistors. */
    static const uint8_t init[][2] = {
        {0x44, 0x08}, {0x44, 0x08},
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
        {0x0b, 0x00}, {0x0c, 0x00}, {0x10, 0x1f}, {0x11, 0x7f},
        {0x00, 0x80}, /* CSM on, slave */
        {0x01, 0x3f}, /* use MCLK, clocks on */
        {0x02, 0x08}, /* pre_div=1, pre_multi=1 */
        {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x20}, {0x07, 0x00}, {0x08, 0xff}, {0x06, 0x04},
        {0x09, 0x0c}, {0x0a, 0x0c}, /* 16-bit I2S */
        {0x13, 0x10}, {0x1b, 0x0a}, {0x1c, 0x6a}, {0x44, 0x08},
        {0x17, 0xbf}, {0x0e, 0x02}, {0x12, 0x00}, {0x14, 0x1a}, {0x0d, 0x01}, {0x15, 0x40},
        {0x37, 0x08}, {0x45, 0x00},
        {0x32, 0xcf}, /* DAC volume (+8 dB for PaperColor's 150 kOhm AW8737A input network) */
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); i++)
        ESP_RETURN_ON_ERROR(pc_board_codec_write(init[i][0], init[i][1]), TAG, "es8311 reg 0x%02x", init[i][0]);
    return ESP_OK;
}

static void power_off_hw(bool *powered, bool *i2s_on)
{
    if (*powered) {
        pc_board_audio_power(true, false); /* mute AW8737A PA first to avoid pop */
        delay_ms(10);
        pc_board_codec_write(0x32, 0x00);
        pc_board_codec_write(0x00, 0x1f);
    }
    if (*i2s_on) {
        i2s_channel_disable(tx_chan);
        *i2s_on = false;
    }
    pc_board_audio_power(false, false);
    *powered = false;
}

static void audio_task(void *arg)
{
    (void)arg;
    bool powered = false, i2s_on = false;
    int64_t idle_since = 0;
    const size_t max_pcm = PC_AUDIO_SAMPLE_RATE * 12; /* up to 12 seconds in PSRAM */
    int16_t *pcm = heap_caps_malloc(max_pcm * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t stereo[256 * 2];
    if (!pcm) {
        ESP_LOGE(TAG, "PSRAM audio buffer allocation failed");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        if (shutting_down) {
            power_off_hw(&powered, &i2s_on);
            playing = false;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        uint32_t g;
        char id[24];
        int vol;
        portENTER_CRITICAL(&req_lock);
        g = req_gen;
        strcpy(id, req_id);
        vol = req_vol;
        portEXIT_CRITICAL(&req_lock);

        if (g == done_gen) {
            playing = false;
            /* Keep amplifier warm for 2.5 s so key clicks and 800 ms debounced page melodies don't cycle power. */
            if (powered && esp_timer_get_time() - idle_since > 2500000) power_off_hw(&powered, &i2s_on);
            vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
            continue;
        }
        done_gen = g;
        if (!id[0] || !strcmp(id, "off")) {
            playing = false;
            continue;
        }
        playing = true;
        if (!powered) {
            /* Turn on CODEC_3V3_L3B (G45) and let the 10 uF rail + I2C hot-plug switch settle while rendering. */
            pc_board_audio_power(true, false);
            delay_ms(30);
        }
        size_t samples = pc_synth_render(id, vol, pcm, max_pcm);
        if (!samples) {
            playing = false;
            continue;
        }
        if (!powered) {
            if (!i2s_on && i2s_channel_enable(tx_chan) == ESP_OK) i2s_on = true;
            if (!i2s_on || es8311_setup() != ESP_OK) {
                ESP_LOGW(TAG, "ES8311 / I2S init failed");
                power_off_hw(&powered, &i2s_on);
                playing = false;
                continue;
            }
            memset(stereo, 0, sizeof(stereo));
            size_t written = 0;
            i2s_channel_write(tx_chan, stereo, sizeof(stereo), &written, 100);
            pc_board_audio_power(true, true);
            /* Pre-fill ~48 ms of silence so ES8311 VMID/DACVREF and AW8737A soft-start finish before note attack. */
            for (int w = 0; w < 3; w++)
                i2s_channel_write(tx_chan, stereo, sizeof(stereo), &written, 100);
            powered = true;
        }
        size_t offset = 0;
        while (offset < samples && req_gen == g && !shutting_down) {
            size_t chunk = samples - offset;
            if (chunk > 256) chunk = 256;
            for (size_t i = 0; i < chunk; i++) {
                stereo[i * 2] = pcm[offset + i];
                stereo[i * 2 + 1] = pcm[offset + i];
            }
            size_t written = 0;
            i2s_channel_write(tx_chan, stereo, chunk * 2 * sizeof(int16_t), &written, 100);
            offset += chunk;
        }
        /* Flush trailing silence (>= 96 ms > 90 ms DMA ring) so DMA fully drains to the speaker before returning. */
        memset(stereo, 0, sizeof(stereo));
        size_t written = 0;
        int flush_chunks = (req_gen == g && !shutting_down) ? 6 : 1;
        for (int w = 0; w < flush_chunks; w++)
            i2s_channel_write(tx_chan, stereo, sizeof(stereo), &written, 100);
        idle_since = esp_timer_get_time();
        if (req_gen == g) playing = false;
    }
}

esp_err_t pc_audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, NULL), TAG, "i2s channel");
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PC_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = 42,
            .bclk = 40,
            .ws = 41,
            .dout = 38,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "i2s std mode");
    if (xTaskCreate(audio_task, "audio", 5120, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void pc_audio_play(const char *id, int volume_percent)
{
    if (!tx_chan || shutting_down || !id) return;
    portENTER_CRITICAL(&req_lock);
    snprintf(req_id, sizeof(req_id), "%s", id);
    req_vol = volume_percent < 10 ? 10 : volume_percent > 100 ? 100 : volume_percent;
    req_gen++;
    playing = true;
    portEXIT_CRITICAL(&req_lock);
}

void pc_audio_key(unsigned actions, const pc_audio_config_t *cfg)
{
    if (!cfg || !cfg->keys) return;
    const char *id = NULL;
    if (actions & PC_ACTION_SETUP) id = "_key_setup";
    else if (actions & PC_ACTION_PAGE) id = "_key_page";
    else if (actions & PC_ACTION_TODAY) id = "_key_today";
    else if (actions & PC_ACTION_NEXT) id = "_key_next";
    else if (actions & PC_ACTION_PREVIOUS) id = "_key_prev";
    if (id) pc_audio_play(id, cfg->volume);
}

void pc_audio_stop(void)
{
    if (!tx_chan) return;
    portENTER_CRITICAL(&req_lock);
    req_id[0] = 0;
    req_gen++;
    portEXIT_CRITICAL(&req_lock);
}

bool pc_audio_busy(void)
{
    return playing || req_gen != done_gen;
}

void pc_audio_shutdown(void)
{
    if (!tx_chan) return;
    /* Wait for an in-flight key click, chime or anniversary melody to finish before cutting power. */
    int64_t deadline = esp_timer_get_time() + 15000000;
    while (pc_audio_busy() && esp_timer_get_time() < deadline) vTaskDelay(pdMS_TO_TICKS(20));
    shutting_down = true;
    pc_audio_stop();
    delay_ms(40);
    pc_board_audio_power(false, false);
}
void pc_audio_resume(void)
{
    shutting_down = false;
}
#endif
