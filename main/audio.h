#pragma once
#include "model.h"
#ifndef PC_HOST_CHECKS
#include "esp_err.h"
#endif

#define PC_AUDIO_SAMPLE_RATE 16000

typedef struct {
    uint8_t lead;  /* MIDI note number (60 = C4), 0 for rest */
    uint8_t bass;  /* Accompaniment MIDI note number, 0 for none */
    uint16_t ms;   /* Note step duration in milliseconds */
} pc_note_t;

typedef struct {
    const char *id;
    const char *name;
    const char *category; /* "sfx", "chime", "classic" */
    const pc_note_t *notes;
    size_t count;
} pc_melody_info_t;

/* Portable music-box / celesta synthesizer & score library (also compiled into host_checks). */
const pc_melody_info_t *pc_melody_lookup(const char *id);
const pc_melody_info_t *pc_melody_at(size_t index);
size_t pc_melody_count(void);
bool pc_chime_hour_allowed(const pc_audio_config_t *cfg, int hour);
/* Render a melody into 16 kHz mono 16-bit PCM. Returns total samples written (or needed when out is NULL). */
size_t pc_synth_render(const char *id, int volume_percent, int16_t *out, size_t max_samples);

#ifndef PC_HOST_CHECKS
esp_err_t pc_audio_init(void);
void pc_audio_play(const char *id, int volume_percent);
void pc_audio_key(unsigned actions, const pc_audio_config_t *cfg);
void pc_audio_stop(void);
bool pc_audio_busy(void);
void pc_audio_shutdown(void);
/* Sleep was aborted after shutdown; allow the player to power the codec again. */
void pc_audio_resume(void);
#endif
