#pragma once
#include "model.h"
#define PC_ANNIV_MAX 32
#define PC_ANNIV_MAX_JSON 24000
#define PC_PACKED_BYTES (PC_PIXELS / 2)

typedef enum { PC_ANNIV_COUNTDOWN, PC_ANNIV_ELAPSED } pc_anniv_mode_t;
typedef struct {
    char id[17], name[97];
    pc_date_t date;
    pc_anniv_mode_t mode;
    bool yearly, lunar, image, overlay_top;
    int lunar_month, lunar_day;
    pc_color_t band;
} pc_anniversary_t;
typedef struct { int count; pc_anniversary_t items[PC_ANNIV_MAX]; } pc_anniversaries_t;

typedef enum { PC_ANNIV_UPCOMING, PC_ANNIV_TODAY, PC_ANNIV_PASSED, PC_ANNIV_SINCE } pc_anniv_state_t;
typedef struct {
    pc_anniv_state_t state;
    int days;       /* always >= 0; meaning depends on state */
    int years;      /* anniversary number of `next`, 0 when not yearly */
    pc_date_t next; /* next yearly occurrence on or after today */
    bool celebrate; /* show the page automatically today */
} pc_anniv_status_t;

bool pc_anniversary_id_valid(const char *id);
bool pc_anniversary_parse_item(const char *json, pc_anniversary_t *out, char *error, size_t capacity);
bool pc_anniversaries_parse(const char *json, pc_anniversaries_t *out, char *error, size_t capacity);
char *pc_anniversaries_json(const pc_anniversaries_t *list);
int pc_anniversaries_find(const pc_anniversaries_t *list, const char *id);
bool pc_anniversaries_upsert(pc_anniversaries_t *list, const pc_anniversary_t *item);
bool pc_anniversaries_remove(pc_anniversaries_t *list, const char *id);
bool pc_anniversary_status(const pc_anniversary_t *item, pc_date_t today, pc_anniv_status_t *out);
/* Index of the first anniversary to celebrate today, or -1. */
int pc_anniversaries_today(const pc_anniversaries_t *list, pc_date_t today);

/* Packed image: two pixels per byte, high nibble first, pc_color_t values. */
bool pc_packed_valid(const uint8_t *packed);
void pc_packed_unpack(const uint8_t *packed, uint8_t *pixels);
void pc_packed_pack(const uint8_t *pixels, uint8_t *packed);

/* Draft preview request body: u16 little-endian JSON length, the item JSON (at most 2048 bytes),
 * then nothing or exactly one packed image. *image points into body or is NULL. */
#define PC_DRAFT_MAX_JSON 2048
#define PC_DRAFT_MAX_BODY (2 + PC_DRAFT_MAX_JSON + PC_PACKED_BYTES)
bool pc_draft_parse(const uint8_t *body, size_t length, pc_anniversary_t *item, const uint8_t **image,
                    char *error, size_t capacity);
