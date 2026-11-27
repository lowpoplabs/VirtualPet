/*
 * The critter on screen.
 *
 * Everything is plain LVGL shapes, drawn smooth: that is the pet's style,
 * not a stand-in for sprites. Each life stage and form is one row of
 * numbers (colour, size, antennae, ears); a form with a look of its own,
 * such as the Homebody's highland cow or the Singer's fennec fox, adds its
 * features on top. The eyes and mouth are drawn by code so they can
 * blink, squint, chew and yawn without a frame per expression.
 */
#include "pet_view.h"

#define EYE_W       46
#define EYE_GAP     34
#define EYE_OPEN_H  66
#define EYE_SHUT_H  6
#define PUPIL_W     22
#define FACE_COLOR  0x10131A
#define TIP_COLOR   0xFFE27A

#define TAP_COOLDOWN_MS 1500

/* ---- Looks ------------------------------------------------------------ */

typedef struct {
    uint32_t color;
    int16_t body_w, body_h;
    int16_t stalk_l, stalk_r;   /* antenna heights; 0 = none */
    int16_t ear;                /* ear diameter; 0 = none */
    bool glow;                  /* belly pulses with light */
    bool shifting;              /* colour drifts through the rainbow */
    uint8_t style;              /* LOOK_PLAIN, or a form with features of its own */
} look_t;

enum {
    LOOK_PLAIN, LOOK_COW, LOOK_FOX, LOOK_BUTTERFLY, LOOK_CRAB, LOOK_HARE, LOOK_OCTOPUS,
    LOOK_UNICORN,
};

/*
 * The Prism: a unicorn, with ears and a horn that is its one antenna. Its
 * body colour drifts through the rainbow; these colours stay as they are.
 */
#define PRISM_PALE   0xFFF6E6
#define PRISM_GOLD   0xFFD36B
#define PRISM_CREAM  0xFFEDB8
#define PRISM_BLUSH  0xFF8FB8
static const uint32_t RAINBOW[6] = {0xFF7B9C, 0xFFB25B, 0xFFE27A, 0x7BE38F, 0x7DB8F2, 0xC79BF2};

/* The Glowbug: a dumbo octopus, whose ears are the two fins on its head. It glows. */
#define OCTO_FIN     0xC2BDFF
#define OCTO_FIN_IN  0xE0DDFF
#define OCTO_GLOW    0x9FFFE8
#define OCTO_BLUSH   0xF29AD2
#define OCTO_ARMS    6

/* The Racer: a hare in the Bluetooth line's pink, with racing stripes. */
#define HARE_PALE    0xFFE6EE
#define HARE_EAR_IN  0xFFC2D6
#define HARE_NOSE    0xB83A66
#define HARE_STRIPE  0xFFFFFF

/* The Scavenger: a crab, coral with a blue crab's blue on its pincers, legs and back. */
#define CRAB_BLUE    0x3F8FE3
#define CRAB_SPECKLE 0x7DB8F2

/* The Roamer: a monarch butterfly. Dark outlines vanish on black, so the wing edge is brown. */
#define FLY_WING     0xF2811D
#define FLY_EDGE     0x9A4A14
#define FLY_SPOT     0xFFF4DC

/* The Singer: a fennec fox in the Bluetooth line's lilac. */
#define FOX_PALE     0xF4EAFF
#define FOX_EAR_IN   0xF6BFE4
#define FOX_NOSE     0x3A2350

/* The Homebody's coat: a highland cow's ginger, with a paler fringe and muzzle. */
#define COW_HAIR_A   0xDE9A52
#define COW_HAIR_B   0xEDB476
#define COW_MUZZLE   0xF3D9B4
#define COW_NOSTRIL  0x7A4420

/* Wi-Fi line: greens and warm tones. Bluetooth line: purples and pinks. */
static const look_t LOOK_HATCHLING = {0x8CF2B0, 184, 168, 34, 34, 0, false, false};
static const look_t LOOKS[FORM_COUNT] = {
    [FORM_NONE]      = {0x8CF2B0, 184, 168, 34, 34, 0, false, false},
    [FORM_GRAZER]    = {0xC6E68C, 200, 182, 50, 50, 0, false, false},   /* an older hatchling, warming */
    [FORM_HUNTER]    = {0xA9BEF7, 190, 186, 44, 44, 0, false, false},   /* an older hatchling, cooling */
    [FORM_HOMEBODY]  = {0xBF7436, 212, 186, 0, 0, 0, false, false, LOOK_COW},   /* highland cow */
    [FORM_ROAMER]    = {0xFFC857, 150, 192, 64, 64, 0, false, false, LOOK_BUTTERFLY},  /* monarch */
    [FORM_SCAVENGER] = {0xE8674C, 230, 178, 72, 44, 0, false, false, LOOK_CRAB},  /* crab */
    [FORM_SINGER]    = {0xC79BF2, 184, 180, 0, 0, 0, false, false, LOOK_FOX},   /* fennec fox */
    [FORM_RACER]     = {0xF48FB1, 190, 184, 0, 0, 0, false, false, LOOK_HARE},  /* hare */
    [FORM_GLOWBUG]   = {0xA9A2FF, 204, 180, 0, 0, 0, true, false, LOOK_OCTOPUS},   /* dumbo octopus */
    [FORM_PRISM]     = {0x8CF2B0, 196, 186, 0, 0, 0, false, true, LOOK_UNICORN},   /* unicorn */
};

#define EGG_W     150
#define EGG_H     190
#define EGG_COLOR 0xF4EBD0

/* ---- State ------------------------------------------------------------ */

static lv_obj_t *s_parent;
static lv_obj_t *s_root;      /* moves as one piece for hops */
static lv_obj_t *s_body;
static lv_obj_t *s_belly;
static lv_obj_t *s_eye[2];
static lv_obj_t *s_pupil[2];
static lv_obj_t *s_stalk[2];
static lv_obj_t *s_tip[2];
static lv_obj_t *s_zzz;
static lv_obj_t *s_tinted[48];    /* every part in the body colour, for forms whose colour moves */
static int s_tinted_count;
static lv_obj_t *s_mouth_curve;   /* smile, frown */
static lv_obj_t *s_mouth_open;    /* open oval; also the flat line when squashed */
static bool s_mouth_busy;         /* munching or yawning: leave the mouth alone */
static int32_t s_mouth_open_w;
static int32_t s_mouth_y;
static lv_timer_t *s_blink_timer;
static lv_obj_t *s_fin[2];        /* the octopus's ear fins, which flap */
static lv_obj_t *s_arm[6];        /* and its arms, which ripple */
static lv_obj_t *s_claw[2];       /* the crab's pincers, which wave */
static lv_obj_t *s_claw_gap[2];   /* and the gap in each, which snaps shut */
static lv_obj_t *s_wing[4];       /* the butterfly's wings, which beat */
static int16_t s_wing_w[4];       /* each one's width when spread */

static bool s_is_egg;
static uint8_t s_style;           /* the LOOK_ of the critter on screen */
static pet_stage_t s_stage = STAGE_HATCHLING;
static pet_form_t s_form = FORM_NONE;
static bool s_underfed;
static pet_mood_t s_mood = PET_MOOD_CONTENT;
static int32_t s_mood_eye_h = EYE_OPEN_H;
static pet_action_t s_action = ACT_IDLE;
static uint32_t s_last_tap;
static void (*s_tap_cb)(void);

/* ---- Size ---------------------------------------------------------------
 * Every size and position in this file is written for a square of
 * PET_AREA_SIZE, the size the pet was drawn at. They are turned into the
 * screen's pixels only here, at the last step, so a bigger pet is drawn
 * smooth at its own size and not enlarged afterwards. At PET_AREA_SIZE
 * nothing changes at all.
 */

static int32_t s_size = PET_AREA_SIZE;

/* A distance or an offset, in the screen's pixels. */
static int32_t px(int32_t v)
{
    if (s_size == PET_AREA_SIZE) {
        return v;
    }
    int32_t n = v * s_size;
    return (n >= 0 ? n + PET_AREA_SIZE / 2 : n - PET_AREA_SIZE / 2) / PET_AREA_SIZE;
}

/* A width or a height: a part never shrinks away to nothing, and a percentage stays one. */
static int32_t len(int32_t v)
{
    if (LV_COORD_IS_PCT(v)) {
        return v;
    }
    int32_t n = px(v);
    return v > 0 && n < 1 ? 1 : n;
}

static void place(lv_obj_t *o, lv_align_t align, int32_t x, int32_t y)
{
    (lv_obj_align)(o, align, px(x), px(y));
}

static void size(lv_obj_t *o, int32_t w, int32_t h)
{
    lv_obj_set_size(o, len(w), len(h));
}

void pet_view_set_size(int32_t pixels)
{
    s_size = pixels > 0 ? pixels : PET_AREA_SIZE;
}

static lv_obj_t *shape(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    size(o, w, h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* A part in the body colour. */
static lv_obj_t *body_part(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color)
{
    lv_obj_t *o = shape(parent, w, h, color);
    if (s_tinted_count < (int)(sizeof(s_tinted) / sizeof(s_tinted[0]))) {
        s_tinted[s_tinted_count++] = o;
    }
    return o;
}

/* Where the eyelids sit when nothing is animating them. */
static int32_t rest_eye_h(void)
{
    return s_action == ACT_NAP ? EYE_SHUT_H : s_mood_eye_h;
}

/* ---- Mouth ------------------------------------------------------------
 * A curve (smile, frown) or an oval (open, or squashed into a flat line).
 * What it shows comes from what the pet is doing and how it feels;
 * munching and yawning briefly take it over.
 */

typedef enum { MOUTH_FLAT, MOUTH_SMILE, MOUTH_GRIN, MOUTH_FROWN, MOUTH_SMALL_O } mouth_t;

static mouth_t mouth_for_now(void)
{
    switch (s_action) {
    case ACT_NAP:     return MOUTH_FLAT;
    case ACT_GRUMBLE: return MOUTH_FROWN;
    case ACT_PLAY:    return MOUTH_GRIN;
    default:          break;
    }
    switch (s_mood) {
    case PET_MOOD_HAPPY:   return MOUTH_GRIN;
    case PET_MOOD_CONTENT: return MOUTH_SMILE;
    case PET_MOOD_HUNGRY:  return MOUTH_SMALL_O;
    case PET_MOOD_QUEASY:  return MOUTH_FROWN;
    default:               return MOUTH_FLAT;   /* bored, sleepy */
    }
}

/* A curve is a slice of a circle: bigger circle and narrower slice = shallower. */
static void show_curve(int32_t diameter, int32_t half_angle, bool up)
{
    /* LVGL angles run clockwise from 3 o'clock, so 90 is the bottom, 270 the top. */
    int32_t mid = up ? 90 : 270;
    size(s_mouth_curve, diameter, diameter);
    lv_arc_set_bg_angles(s_mouth_curve, mid - half_angle, mid + half_angle);
    /* Keep the middle of the curve at the mouth line whatever the circle size. */
    int32_t dy = up ? s_mouth_y + 5 - diameter / 2 : s_mouth_y - 3 + diameter / 2;
    place(s_mouth_curve, LV_ALIGN_CENTER, 0, dy);
    lv_obj_remove_flag(s_mouth_curve, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_mouth_open, LV_OBJ_FLAG_HIDDEN);
}

static void show_open(int32_t w, int32_t h)
{
    size(s_mouth_open, w, h < 2 ? 2 : h);
    place(s_mouth_open, LV_ALIGN_CENTER, 0, s_mouth_y);
    lv_obj_remove_flag(s_mouth_open, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_mouth_curve, LV_OBJ_FLAG_HIDDEN);
}

static void apply_mouth(void)
{
    if (s_is_egg || !s_mouth_curve || s_mouth_busy) {
        return;
    }
    switch (mouth_for_now()) {
    case MOUTH_GRIN:    show_curve(46, 62, true);   break;
    case MOUTH_SMILE:   show_curve(96, 22, true);   break;
    case MOUTH_FROWN:   show_curve(64, 34, false);  break;
    case MOUTH_SMALL_O: show_open(14, 12);          break;
    case MOUTH_FLAT:    show_open(26, 5);           break;
    }
}

static void mouth_open_cb(void *var, int32_t h)
{
    LV_UNUSED(var);
    show_open(s_mouth_open_w, h);
}

static void mouth_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    s_mouth_busy = false;
    apply_mouth();
}

/* Opens and shuts the mouth: quick and repeated for a munch, slow and wide for a yawn. */
static void animate_mouth(int32_t width, int32_t open_h, uint32_t ms, uint32_t hold_ms,
                          uint32_t repeats)
{
    if (s_is_egg || !s_mouth_open) {
        return;
    }
    s_mouth_busy = true;
    s_mouth_open_w = width;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_mouth_open);
    lv_anim_set_exec_cb(&a, mouth_open_cb);
    lv_anim_set_values(&a, 3, open_h);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_playback_delay(&a, hold_ms);
    lv_anim_set_playback_duration(&a, ms);
    lv_anim_set_repeat_count(&a, repeats);
    lv_anim_set_completed_cb(&a, mouth_done_cb);
    lv_anim_start(&a);
}

static void claws_snap(uint32_t ms, uint32_t times, uint32_t rest_ms);

void pet_view_munch(void)
{
    animate_mouth(30, 22, 110, 0, 4);
    claws_snap(110, 4, 0);      /* a crab snaps its pincers as it chews */
}

/* ---- Animation callbacks (one per moving part, so each can be stopped) --
 * A movement is counted in the screen's own pixels: its two ends are turned
 * into them once, with px(), where the animation is started, and the
 * callback uses the value as it comes. So a bigger pet moves a pixel at a
 * time, as the watch's does, and not in jumps. The ones that set a size
 * from a value in the watch's pixels (the eyes, the mouth) or in hundredths
 * (the wings) still work it out at each step.
 */

static void eye_h_cb(void *var, int32_t h)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2 && s_eye[i]; i++) {
        lv_obj_set_height(s_eye[i], len(h));
        /* Pupil hides once the lid is nearly closed. */
        int32_t ph = h - 16;
        if (ph < 0) ph = 0;
        if (ph > 34) ph = 34;
        lv_obj_set_height(s_pupil[i], px(ph));
    }
}

static void bob_cb(void *var, int32_t y)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, y, 0);
}

static void hop_cb(void *var, int32_t y)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, y, 0);
}

static void look_cb(void *var, int32_t x)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2 && s_pupil[i]; i++) {
        lv_obj_set_style_translate_x(s_pupil[i], x, 0);
    }
}

static void sniff_cb(void *var, int32_t x)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2; i++) {
        /* The tips swing further than the stalks, in opposite directions per side. */
        int32_t dir = i == 0 ? 1 : -1;
        if (s_tip[i]) {
            lv_obj_set_style_translate_x(s_tip[i], dir * x, 0);
        }
        if (s_stalk[i]) {
            lv_obj_set_style_translate_x(s_stalk[i], dir * x / 3, 0);
        }
    }
}

static void shake_cb(void *var, int32_t x)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, x, 0);
}

static void zzz_cb(void *var, int32_t v)
{
    lv_obj_t *z = var;
    /* A quarter of v in the watch's pixels, worked out in the screen's so it drifts smoothly. */
    lv_obj_set_style_translate_y(z, -(v * s_size) / (4 * PET_AREA_SIZE), 0);
    lv_obj_set_style_opa(z, (lv_opa_t)(v < 128 ? v * 2 : (255 - v) * 2), 0);
}

static void glow_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void hue_cb(void *var, int32_t hue)
{
    LV_UNUSED(var);
    lv_color_t c = lv_color_hsv_to_rgb((uint16_t)hue, 45, 95);
    for (int i = 0; i < s_tinted_count; i++) {
        lv_obj_set_style_bg_color(s_tinted[i], c, 0);
    }
}

/* The wings close a little and open again; each is pinned at the body, so its outer edge moves. */
static void wing_cb(void *var, int32_t percent)
{
    LV_UNUSED(var);
    for (int i = 0; i < 4 && s_wing[i]; i++) {
        lv_obj_set_width(s_wing[i], len(s_wing_w[i] * percent / 100));
    }
}

/* The octopus's ear fins beat up and down together. */
static void fin_cb(void *var, int32_t y)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2 && s_fin[i]; i++) {
        lv_obj_set_style_translate_y(s_fin[i], y, 0);
    }
}

/* Its arms ripple: each one moves against its neighbours. */
static void arm_cb(void *var, int32_t y)
{
    LV_UNUSED(var);
    for (int i = 0; i < 6 && s_arm[i]; i++) {
        lv_obj_set_style_translate_y(s_arm[i], i % 2 ? y : -y, 0);
    }
}

static void flicker_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/*
 * LVGL's ease in and out, but to the nearest pixel. LVGL's own rounds
 * down, which shows the far end of a movement for one frame only: the pet's
 * breathing touched its lowest point and sprang back, a tick at the bottom.
 * Rounded to the nearest, the two ends each last as long as the other.
 */
static int32_t ease_nearest(const lv_anim_t *a)
{
    uint32_t t = lv_map(a->act_time, 0, a->duration, 0, LV_BEZIER_VAL_MAX);
    int32_t step = lv_cubic_bezier(t, LV_BEZIER_VAL_FLOAT(0.42), LV_BEZIER_VAL_FLOAT(0),
                                   LV_BEZIER_VAL_FLOAT(0.58), LV_BEZIER_VAL_FLOAT(1));
    int32_t n = step * (a->end_value - a->start_value);
    int32_t half = LV_BEZIER_VAL_MAX / 2;
    return a->start_value + (n >= 0 ? n + half : n - half) / LV_BEZIER_VAL_MAX;
}

static void loop_anim(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                      bool back)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    if (back) {
        lv_anim_set_playback_duration(&a, ms);
    }
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    /* The breathing is slow and small enough for the rounding to show. */
    lv_anim_set_path_cb(&a, cb == bob_cb ? ease_nearest : lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* ---- The crab's pincers ----------------------------------------------- */

#define CLAW_GAP_OPEN 12
#define CLAW_GAP_SHUT 2
#define WAVE          14    /* how far a pincer is waved */

static void snap_cb(void *var, int32_t w)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2 && s_claw_gap[i]; i++) {
        lv_obj_set_width(s_claw_gap[i], w);
    }
}

/* One goes up as the other comes down. */
static void wave_cb(void *var, int32_t y)
{
    LV_UNUSED(var);
    if (s_claw[0] && s_claw[1]) {
        lv_obj_set_style_translate_y(s_claw[0], -y, 0);
        lv_obj_set_style_translate_y(s_claw[1], y - px(WAVE), 0);
    }
}

static void claws_idle(void);

static void claws_idle_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    claws_idle();
}

/* Snaps the pincers shut and open `times` times, pausing `rest_ms` between snaps. */
static void claws_snap(uint32_t ms, uint32_t times, uint32_t rest_ms)
{
    if (!s_claw_gap[0]) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_claw_gap);
    lv_anim_set_exec_cb(&a, snap_cb);
    lv_anim_set_values(&a, len(CLAW_GAP_OPEN), len(CLAW_GAP_SHUT));
    lv_anim_set_duration(&a, ms);
    lv_anim_set_playback_duration(&a, ms);
    lv_anim_set_repeat_count(&a, times);
    lv_anim_set_repeat_delay(&a, rest_ms);
    if (times != LV_ANIM_REPEAT_INFINITE) {
        lv_anim_set_completed_cb(&a, claws_idle_cb);    /* then back to the odd idle snap */
    }
    lv_anim_start(&a);
}

/* At rest: a snap every few seconds. */
static void claws_idle(void)
{
    claws_snap(150, LV_ANIM_REPEAT_INFINITE, 3400);
}

static void animate_eyes(int32_t from, int32_t to, uint32_t ms, bool back)
{
    if (s_is_egg) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_eye);
    lv_anim_set_exec_cb(&a, eye_h_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    if (back) {
        lv_anim_set_playback_duration(&a, ms);
    }
    lv_anim_start(&a);
}

static void blink_cb(lv_timer_t *t)
{
    if (s_action != ACT_NAP) {
        animate_eyes(rest_eye_h(), EYE_SHUT_H, 90, true);
    }
    /* Irregular gaps read as alive; a fixed period reads as a machine. */
    lv_timer_set_period(t, 1800 + lv_rand(0, 3500));
}

static void start_hop(uint32_t repeats)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_exec_cb(&a, hop_cb);
    lv_anim_set_values(&a, 0, px(-28));
    lv_anim_set_duration(&a, 160);
    lv_anim_set_playback_duration(&a, 220);
    lv_anim_set_repeat_count(&a, repeats);
    lv_anim_set_repeat_delay(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* An egg cannot hop; it rocks. */
static void start_wobble(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_body);
    lv_anim_set_exec_cb(&a, shake_cb);
    lv_anim_set_values(&a, px(-9), px(9));
    lv_anim_set_duration(&a, 110);
    lv_anim_set_playback_duration(&a, 110);
    lv_anim_set_repeat_count(&a, 4);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void pet_view_set_paused(bool paused)
{
    if (!s_blink_timer) {
        return;
    }
    if (paused) {
        lv_timer_pause(s_blink_timer);
    } else {
        lv_timer_resume(s_blink_timer);
    }
}

/* ---- Taps and hops --------------------------------------------------- */

void pet_view_set_tap_cb(void (*cb)(void))
{
    s_tap_cb = cb;
}

static void (*s_long_cb)(void);

void pet_view_set_long_cb(void (*cb)(void))
{
    s_long_cb = cb;
}

static void long_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_long_cb) {
        /* Letting go afterwards is not a tap. */
        lv_indev_wait_release(lv_indev_active());
        s_long_cb();
    }
}

static void tap_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t now = lv_tick_get();
    if (s_last_tap != 0 && now - s_last_tap < TAP_COOLDOWN_MS) {
        return;
    }
    s_last_tap = now;
    pet_view_hop();
    if (s_tap_cb) {
        s_tap_cb();
    }
}

/* ---- Leaving for the farm, and the egg that arrives ---------------------- */

static void walk_cb(void *var, int32_t x)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, x, 0);
}

static void slide(int32_t from, int32_t to, uint32_t ms, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_exec_cb(&a, walk_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

void pet_view_set_empty(bool empty)
{
    if (s_root && lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN) != empty) {
        lv_obj_update_flag(s_root, LV_OBJ_FLAG_HIDDEN, empty);
    }
}

void pet_view_walk_off(uint32_t ms)
{
    if (!s_root) {
        return;
    }
    /* A happy face and a hop for each step, all the way out of the picture. */
    animate_eyes(rest_eye_h(), 18, 200, true);
    if (!s_is_egg && s_action != ACT_PLAY) {
        start_hop(ms / 440 + 1);
    }
    slide(0, px(PET_AREA_SIZE), ms, lv_anim_path_ease_in);
}

void pet_view_roll_in(uint32_t ms)
{
    if (!s_root) {
        return;
    }
    lv_obj_set_style_translate_x(s_root, px(-PET_AREA_SIZE), 0);
    slide(px(-PET_AREA_SIZE), 0, ms, lv_anim_path_overshoot);
    if (s_is_egg) {
        start_wobble();
    }
}

void pet_view_hop(void)
{
    if (!s_root) {
        return;
    }
    if (s_is_egg) {
        start_wobble();
        return;
    }
    /* Happy squint plus a little hop. A napping pet stirs, then settles back. */
    animate_eyes(rest_eye_h(), 18, 140, true);
    if (s_action != ACT_PLAY) {
        start_hop(1);
    }
}

/* ---- Actions --------------------------------------------------------- */

static void stop_action(void)
{
    lv_anim_delete(s_pupil, look_cb);
    lv_anim_delete(s_tip, sniff_cb);
    if (s_body) {
        lv_anim_delete(s_body, shake_cb);
        lv_obj_set_style_translate_x(s_body, 0, 0);
    }
    if (s_zzz) {
        lv_anim_delete(s_zzz, zzz_cb);
        lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_action == ACT_PLAY && s_root) {
        lv_anim_delete(s_root, hop_cb);
        lv_obj_set_style_translate_y(s_root, 0, 0);
    }
    if (s_action == ACT_PLAY && s_claw[0]) {
        lv_anim_delete(s_claw, wave_cb);
        for (int i = 0; i < 2; i++) {
            lv_obj_set_style_translate_y(s_claw[i], 0, 0);
        }
        claws_idle();
    }
    look_cb(NULL, 0);
    sniff_cb(NULL, 0);
}

static void start_action(pet_action_t before)
{
    switch (s_action) {
    case ACT_LOOK:
        loop_anim(s_pupil, look_cb, px(-10), px(10), 900, true);
        break;
    case ACT_SNIFF:
        /* Ears twitch too, but only antennae have tips to swing; no-op without them. */
        loop_anim(s_tip, sniff_cb, px(-7), px(7), 130, true);
        break;
    case ACT_PLAY:
        start_hop(LV_ANIM_REPEAT_INFINITE);
        if (s_style == LOOK_HARE) {
            loop_anim(s_tip, sniff_cb, px(-18), px(18), 240, true);     /* its ears sway as it bounds */
        }
        if (s_claw[0]) {
            /* A crab at play waves its pincers and snaps them. */
            loop_anim(s_claw, wave_cb, 0, px(WAVE), 220, true);
            claws_snap(120, LV_ANIM_REPEAT_INFINITE, 0);
        }
        break;
    case ACT_GRUMBLE:
        loop_anim(s_body, shake_cb, px(-4), px(4), 70, true);
        break;
    case ACT_NAP:
        lv_obj_remove_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
        loop_anim(s_zzz, zzz_cb, 0, 255, 2600, false);
        break;
    default:
        break;
    }

    /* Eyelids drift shut for a nap and open again after it. */
    if (s_action == ACT_NAP || before == ACT_NAP) {
        int32_t from = before == ACT_NAP ? EYE_SHUT_H : s_mood_eye_h;
        animate_eyes(from, rest_eye_h(), 500, false);
    }
    if (s_action == ACT_NAP && before != ACT_NAP) {
        animate_mouth(24, 28, 600, 500, 1);   /* a yawn first */
    } else {
        apply_mouth();
    }
}

void pet_view_set_action(pet_action_t action)
{
    if (!s_root || action == s_action) {
        return;
    }
    pet_action_t before = s_action;
    if (s_is_egg) {
        s_action = action;   /* remembered for when it hatches; an egg just sits */
        return;
    }
    stop_action();
    s_action = action;
    start_action(before);
}

void pet_view_set_mood(pet_mood_t mood)
{
    switch (mood) {
    case PET_MOOD_HAPPY:   s_mood_eye_h = EYE_OPEN_H;      break;
    case PET_MOOD_CONTENT: s_mood_eye_h = EYE_OPEN_H - 8;  break;
    case PET_MOOD_BORED:   s_mood_eye_h = 36;              break;
    case PET_MOOD_HUNGRY:  s_mood_eye_h = EYE_OPEN_H + 6;  break;
    case PET_MOOD_SLEEPY:  s_mood_eye_h = 20;              break;
    case PET_MOOD_QUEASY:  s_mood_eye_h = 30;              break;
    }
    s_mood = mood;
    if (s_eye[0]) {
        eye_h_cb(NULL, rest_eye_h());
        apply_mouth();
    }
}

/* ---- Construction ---------------------------------------------------- */

static void clear(void)
{
    /* Animations keyed on these arrays, not on an object, do not die with the objects. */
    lv_anim_delete(s_eye, NULL);
    lv_anim_delete(s_pupil, NULL);
    lv_anim_delete(s_tip, NULL);
    lv_anim_delete(s_tinted, NULL);
    lv_anim_delete(s_wing, NULL);
    lv_anim_delete(s_claw, NULL);
    lv_anim_delete(s_fin, NULL);
    lv_anim_delete(s_arm, NULL);
    s_fin[0] = s_fin[1] = NULL;
    for (int i = 0; i < 6; i++) {
        s_arm[i] = NULL;
    }
    lv_anim_delete(s_claw_gap, NULL);
    s_claw[0] = s_claw[1] = s_claw_gap[0] = s_claw_gap[1] = NULL;
    s_wing[0] = s_wing[1] = s_wing[2] = s_wing[3] = NULL;
    s_tinted_count = 0;
    if (s_root) {
        lv_obj_delete(s_root);
    }
    if (s_zzz) {
        lv_obj_delete(s_zzz);
    }
    s_root = s_body = s_belly = s_zzz = s_mouth_curve = s_mouth_open = NULL;
    for (int i = 0; i < 2; i++) {
        s_eye[i] = s_pupil[i] = s_stalk[i] = s_tip[i] = NULL;
    }
    s_mouth_busy = false;
}

static void make_root(int32_t w, int32_t h)
{
    s_root = lv_obj_create(s_parent);
    lv_obj_remove_style_all(s_root);
    size(s_root, w, h);
    lv_obj_center(s_root);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_root, long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_move_to_index(s_root, 0);   /* behind the find line */
}

static void build_egg(void)
{
    make_root(EGG_W + 60, EGG_H + 40);
    s_body = shape(s_root, EGG_W, EGG_H, EGG_COLOR);
    place(s_body, LV_ALIGN_BOTTOM_MID, 0, -10);
    static const int16_t SPOTS[][3] = {{-34, -40, 30}, {30, -8, 38}, {-18, 44, 24}, {26, -62, 18}};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *spot = shape(s_body, SPOTS[i][2], SPOTS[i][2], LOOK_HATCHLING.color);
        place(spot, LV_ALIGN_CENTER, SPOTS[i][0], SPOTS[i][1]);
    }
    loop_anim(s_body, bob_cb, 0, px(3), 1500, true);
}

/*
 * One of the cow's antennae: where a highland cow's horn would be, sweeping
 * out and then up, with the antenna's bright tip at the end. It is a row of
 * overlapping dots, so it curves without any rotated drawing.
 */
static void build_horn(int side, const look_t *k, int32_t body_top)
{
    enum { DOTS = 24, DOT = 16, BOX_W = 96, BOX_H = 96 };
    int i = side < 0 ? 0 : 1;
    lv_obj_t *box = lv_obj_create(s_root);
    lv_obj_remove_style_all(box);
    size(box, BOX_W, BOX_H);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* The horn starts at the box's lower inner corner, on the body's shoulder. */
    int32_t root_dx = side * (k->body_w * 30 / 100 + BOX_W / 2 - DOT / 2);
    int32_t box_top = body_top + 34 - BOX_H;
    place(box, LV_ALIGN_TOP_MID, root_dx, box_top);
    int32_t x = 0, y = 0;
    for (int d = 0; d < DOTS; d++) {
        /* t runs 0..100 along the horn: mostly outward at first, mostly upward at the end. */
        int32_t t = d * 100 / (DOTS - 1);
        x = t * 54 / 100;
        y = t * t * 52 / 10000 + t * 7 / 100;
        lv_obj_t *dot = shape(box, DOT - d / 5, DOT - d / 5, 0xE9DFCB);
        place(dot, side < 0 ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT, side * x, -y);
    }
    s_stalk[i] = box;
    s_tip[i] = shape(s_root, 28, 28, TIP_COLOR);
    place(s_tip[i], LV_ALIGN_TOP_MID,
                 side * (k->body_w * 30 / 100 + x + 2), box_top + BOX_H - y - 30);
}

/* A dot centred at (x, y) inside a box; x is measured from the inner edge, so both sides match. */
static void dot_at(lv_obj_t *box, int side, int32_t x, int32_t y, int32_t d, uint32_t color, bool tinted)
{
    lv_obj_t *dot = tinted ? body_part(box, d, d, color) : shape(box, d, d, color);
    int32_t cx = side < 0 ? (int32_t)(intptr_t)lv_obj_get_user_data(box) - x : x;
    lv_obj_set_pos(dot, px(cx) - len(d) / 2, px(y) - len(d) / 2);
}

static lv_obj_t *part_box(int32_t w, int32_t h)
{
    lv_obj_t *box = lv_obj_create(s_root);
    lv_obj_remove_style_all(box);
    size(box, w, h);
    lv_obj_set_user_data(box, (void *)(intptr_t)w);     /* its width as written, for dot_at() */
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

/*
 * One of the fox's ears: huge, leaning outward, wide at the base and
 * coming to a point. A row of shrinking dots, with a pink row inside.
 */
static void build_fox_ear(int side, const look_t *k, int32_t body_top)
{
    enum { W = 150, H = 176, X0 = 46, Y0 = H - 44, X1 = 104, Y1 = 16, DOTS = 24 };
    lv_obj_t *box = part_box(W, H);
    place(box, LV_ALIGN_TOP_MID, side * (k->body_w * 23 / 100 + W / 2 - X0),
                 body_top + 38 - Y0);
    for (int d = 0; d < DOTS; d++) {
        int32_t t = d * 100 / (DOTS - 1);
        dot_at(box, side, X0 + (X1 - X0) * t / 100, Y0 + (Y1 - Y0) * t / 100, 88 - t * 66 / 100,
               k->color, false);
    }
    for (int d = 2; d < DOTS - 5; d++) {
        int32_t t = d * 100 / (DOTS - 1);
        dot_at(box, side, X0 + (X1 - X0) * t / 100 - 2, Y0 + (Y1 - Y0) * t / 100 + 6,
               52 - t * 46 / 100, FOX_EAR_IN, false);
    }
    s_stalk[side < 0 ? 0 : 1] = box;    /* so the ears twitch when it sniffs */
}

/* The fox's brush: out from the hip and curling up, pale at the end, with the antenna's tip. */
static void build_fox_tail(const look_t *k)
{
    enum { W = 130, H = 150, DOTS = 16 };
    lv_obj_t *box = part_box(W, H);
    int32_t left = k->body_w / 2 - 34, bottom = -26;
    place(box, LV_ALIGN_BOTTOM_MID, left + W / 2, bottom);
    int32_t x = 0, y = 0;
    for (int d = 0; d < DOTS; d++) {
        int32_t t = d * 100 / (DOTS - 1);
        x = 20 + t * 74 / 100;
        y = H - 28 - t * t * 92 / 10000;
        int32_t fat = t < 65 ? 30 + t * 22 / 65 : 52 - (t - 65) * 24 / 35;
        dot_at(box, 1, x, y, fat, d >= DOTS - 5 ? FOX_PALE : k->color, false);
    }
    s_tip[1] = shape(s_root, 24, 24, TIP_COLOR);
    place(s_tip[1], LV_ALIGN_BOTTOM_MID, left + x + 4, bottom - (H - y) - 4);
}

/*
 * The butterfly's wings: on each side a big upper wing and a smaller lower
 * one, behind the body. Each is an orange oval inside a brown one, with
 * pale spots along the outer edge.
 */
static void build_wings(int32_t body_top)
{
    enum { W = 150, H = 214 };
    static const int16_t SIZE[2][2] = {{148, 146}, {110, 106}};     /* upper, lower */
    static const int16_t SPOTS[3][2] = {{12, -34}, {5, 0}, {12, 34}};
    for (int side = -1; side <= 1; side += 2) {
        lv_obj_t *box = part_box(W, H);
        place(box, LV_ALIGN_TOP_MID, side * (30 + W / 2), body_top - 14);
        for (int part = 0; part < 2; part++) {
            int i = (side < 0 ? 0 : 2) + part;
            lv_obj_t *wing = shape(box, SIZE[part][0], SIZE[part][1], FLY_EDGE);
            place(wing, part == 0 ? (side < 0 ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT)
                                         : (side < 0 ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT),
                         0, 0);
            lv_obj_t *inner = shape(wing, lv_pct(80), lv_pct(80), FLY_WING);
            place(inner, LV_ALIGN_CENTER, -side * 4, 0);
            for (int d = 0; d < 3; d++) {
                int32_t y = SPOTS[d][1] * (part == 0 ? 10 : 7) / 10;
                lv_obj_t *spot = shape(wing, 9, 9, FLY_SPOT);
                place(spot, side < 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
                             -side * SPOTS[d][0], y);
            }
            s_wing[i] = wing;
            s_wing_w[i] = SIZE[part][0];
        }
    }
    loop_anim(s_wing, wing_cb, 100, 80, 900, true);
}

static lv_obj_t *bar(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color, int32_t radius)
{
    lv_obj_t *o = shape(parent, w, h, color);
    lv_obj_set_style_radius(o, len(radius), 0);
    return o;
}

/*
 * The crab's pincers, held up at its sides: a big blue claw on a short arm,
 * with the gap between its fingers. The gap closes when it snaps. Below
 * them, two walking legs a side, tipped in the same blue.
 */
static void build_claws(const look_t *k, int32_t body_top)
{
    for (int i = 0; i < 2; i++) {
        int side = i == 0 ? -1 : 1;
        for (int leg = 0; leg < 2; leg++) {
            int32_t reach = k->body_w / 2 + (leg == 0 ? 34 : 8);
            int32_t y = body_top + k->body_h * (leg == 0 ? 77 : 90) / 100;
            lv_obj_t *limb = body_part(s_root, 90, 18, k->color);
            place(limb, LV_ALIGN_TOP_MID, side * (reach - 45), y);
            place(shape(s_root, 22, 22, CRAB_BLUE), LV_ALIGN_TOP_MID, side * (reach - 6), y - 2);
        }
        int32_t y = body_top + k->body_h * 56 / 100;
        lv_obj_t *arm = body_part(s_root, 84, 26, k->color);
        place(arm, LV_ALIGN_TOP_MID, side * (k->body_w / 2 + 6), y);
        s_claw[i] = shape(s_root, 72, 78, CRAB_BLUE);
        place(s_claw[i], LV_ALIGN_TOP_MID, side * (k->body_w / 2 + 46), y - 58);
        /* The screen behind the pet is black, so a black sliver reads as the open pincer. */
        s_claw_gap[i] = bar(s_claw[i], CLAW_GAP_OPEN, 40, 0x000000, 7);
        place(s_claw_gap[i], LV_ALIGN_TOP_MID, side * 5, -6);
    }
    claws_idle();
}

/* Blue speckles across the top of the crab's back. */
static void build_speckles(void)
{
    static const int16_t SPOT[][3] = {
        {-84, -40, 12}, {-48, -68, 14}, {0, -76, 18}, {46, -68, 12}, {84, -42, 14},
    };
    for (int i = 0; i < 5; i++) {
        place(shape(s_body, SPOT[i][2], SPOT[i][2], CRAB_SPECKLE), LV_ALIGN_CENTER,
                     SPOT[i][0], SPOT[i][1]);
    }
}

/*
 * One of the hare's ears: very long, and blown over to one side as if it
 * were running into the wind. A row of dots along a curve, fat in the
 * middle, with a paler row inside.
 */
static void build_hare_ear(int i, const look_t *k, int32_t body_top, int32_t base_dx, int32_t lean,
                           bool striped)
{
    enum { W = 150, H = 200, X0 = W - 40, Y0 = H - 22, LONG = 162, DOTS = 28 };
    lv_obj_t *box = part_box(W, H);
    place(box, LV_ALIGN_TOP_MID, base_dx - W / 2 + 40, body_top + 30 - Y0);
    for (int pass = 0; pass < 2; pass++) {
        for (int d = pass ? 3 : 0; d < DOTS - (pass ? 3 : 0); d++) {
            int32_t t = d * 100 / (DOTS - 1);
            int32_t fat = t < 25 ? 32 + t * 16 / 25 : (t < 65 ? 48 : 48 - (t - 65) * 26 / 35);
            dot_at(box, 1, X0 - lean * t * t / 10000, Y0 - LONG * t / 100, pass ? fat - 24 : fat,
                   pass ? HARE_EAR_IN : k->color, false);
        }
    }
    /* Racing stripes: two bands slanting across the ear, edge to edge. */
    static const int8_t AT[2] = {38, 54};
    for (int n = 0; striped && n < 2; n++) {
        int32_t t = AT[n];
        int32_t reach = 48 / 2 - 8;     /* stops short of the ear's edge */
        for (int step = -4; step <= 4; step++) {
            int32_t dx = step * reach / 4;
            dot_at(box, 1, X0 - lean * t * t / 10000 + dx, Y0 - LONG * t / 100 - dx * 7 / 10, 11,
                   HARE_STRIPE, false);
        }
    }
    s_stalk[i] = box;   /* so the ears twitch when it sniffs and sway when it plays */
}

/* The hare's face: a pale muzzle, a nose and whiskers. */
static void build_hare_face(int32_t eye_y)
{
    lv_obj_t *muzzle = shape(s_body, 92, 58, HARE_PALE);
    place(muzzle, LV_ALIGN_CENTER, 0, eye_y + 70);
    place(shape(muzzle, 18, 12, HARE_NOSE), LV_ALIGN_TOP_MID, 0, 7);
    for (int i = 0; i < 4; i++) {
        int side = i < 2 ? -1 : 1;
        lv_obj_t *whisker = bar(s_body, 30, 3, HARE_PALE, 2);
        place(whisker, LV_ALIGN_CENTER, side * 68, eye_y + 62 + (i % 2) * 12);
    }
}

/*
 * The octopus, behind its body: the two ear-shaped fins at the top of its
 * head, and six arms hanging below in place of feet.
 */
static void build_octopus_back(const look_t *k, int32_t body_top)
{
    /* Each fin is a paddle that tilts up and outward from the side of the head. */
    enum { W = 124, H = 112, X0 = 26, Y0 = H - 34, X1 = 58, Y1 = Y0 - 16, DOTS = 8 };
    for (int i = 0; i < 2; i++) {
        int side = i == 0 ? -1 : 1;
        s_fin[i] = part_box(W, H);
        place(s_fin[i], LV_ALIGN_TOP_MID, side * (k->body_w / 2 - 8 + W / 2 - X0),
                     body_top + 36 - Y0);
        for (int pass = 0; pass < 2; pass++) {
            for (int d = pass ? 3 : 0; d < DOTS - pass; d++) {
                int32_t t = d * 100 / (DOTS - 1);
                int32_t fat = 46 + (t < 70 ? t * 16 / 70 : 16 - (t - 70) * 6 / 30);
                dot_at(s_fin[i], side, X0 + (X1 - X0) * t / 100, Y0 + (Y1 - Y0) * t / 100,
                       pass ? fat - 32 : fat, pass ? OCTO_FIN_IN : OCTO_FIN, false);
            }
        }
        s_stalk[i] = s_fin[i];      /* so they twitch when it sniffs, as ears do */
    }
    loop_anim(s_fin, fin_cb, px(-9), px(7), 620, true);

    /* Long in the middle, short at the sides, so the hem is scalloped. */
    static const int16_t LONG[OCTO_ARMS] = {84, 100, 110, 110, 100, 84};
    for (int i = 0; i < OCTO_ARMS; i++) {
        s_arm[i] = body_part(s_root, 25, LONG[i], k->color);
        place(s_arm[i], LV_ALIGN_TOP_MID, -85 + i * 34, body_top + k->body_h - 62);
    }
    loop_anim(s_arm, arm_cb, px(-5), px(5), 900, true);
}

/* On its body: spots of light that pulse, and a blush under each eye. */
static void build_octopus_front(int32_t eye_y)
{
    static const int16_t SPOT[][3] = {{-56, -64, 12}, {-20, -76, 16}, {22, -74, 12}, {58, -62, 14}};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *spot = shape(s_body, SPOT[i][2], SPOT[i][2], OCTO_GLOW);
        place(spot, LV_ALIGN_CENTER, SPOT[i][0], SPOT[i][1]);
        loop_anim(spot, glow_cb, 60, 255, 1400, true);
    }
    for (int i = 0; i < 2; i++) {
        lv_obj_t *blush = shape(s_body, 30, 16, OCTO_BLUSH);
        lv_obj_set_style_bg_opa(blush, LV_OPA_60, 0);
        place(blush, LV_ALIGN_CENTER, i == 0 ? -62 : 62, eye_y + 44);
    }
}

/* ---- The Prism ---------------------------------------------------------- */

/*
 * A row of dots from (x0, y0) to (x1, y1) in a box, shrinking from d0 to
 * d1, bowed upward by `bow` in the middle. x is measured from the inner
 * edge, so a left and a right part match.
 */
static void chain(lv_obj_t *box, int side, int dots, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  int32_t bow, int32_t d0, int32_t d1, uint32_t color, bool tinted)
{
    for (int d = 0; d < dots; d++) {
        int32_t t = d * 100 / (dots - 1);
        dot_at(box, side, x0 + (x1 - x0) * t / 100,
               y0 + (y1 - y0) * t / 100 - bow * 4 * t * (100 - t) / 10000,
               d0 + (d1 - d0) * t / 100, color, tinted);
    }
}

static void blush(int32_t eye_y, int32_t dx)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = shape(s_body, 30, 16, PRISM_BLUSH);
        lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
        place(b, LV_ALIGN_CENTER, i == 0 ? -dx : dx, eye_y + 44);
    }
}

/* A pale snout with two nostrils; the mouth moves down onto it. */
static void snout(int32_t eye_y, int32_t w)
{
    lv_obj_t *m = shape(s_body, w, 58, PRISM_PALE);
    place(m, LV_ALIGN_CENTER, 0, eye_y + 70);
    for (int i = 0; i < 2; i++) {
        place(shape(m, 10, 8, PRISM_BLUSH), LV_ALIGN_TOP_MID, i == 0 ? -16 : 16, 10);
    }
    s_mouth_y = eye_y + 80;
}

/* Unicorn: ears, a horn that is one antenna, and a mane and tail in every colour. */
static void unicorn_back(const look_t *k, int32_t body_top)
{
    for (int i = 0; i < 3; i++) {       /* tail, fanned out at one hip */
        lv_obj_t *lock = bar(s_root, 74, 24, RAINBOW[i * 2], 12);
        place(lock, LV_ALIGN_BOTTOM_MID, -(k->body_w / 2 + 8) + (i == 1 ? -10 : 0), -28 - i * 20);
    }
    for (int i = 0; i < 6; i++) {       /* mane, down the other side of the head */
        lv_obj_t *lock = bar(s_root, 60, 26, RAINBOW[i], 13);
        place(lock, LV_ALIGN_TOP_MID, k->body_w / 2 - 12 + (i % 2) * 12, body_top + 4 + i * 20);
    }
    for (int side = -1; side <= 1; side += 2) {
        lv_obj_t *box = part_box(90, 100);
        place(box, LV_ALIGN_TOP_MID, side * (k->body_w * 30 / 100 + 15), body_top + 34 - 80);
        chain(box, side, 14, 30, 80, 54, 16, 0, 48, 12, k->color, true);
        chain(box, side, 9, 32, 74, 48, 34, 0, 24, 8, PRISM_BLUSH, false);
        s_stalk[side < 0 ? 0 : 1] = box;
    }
    lv_obj_t *horn = part_box(60, 112);
    place(horn, LV_ALIGN_TOP_MID, 0, body_top + 24 - 104);
    for (int d = 0; d < 15; d++) {
        dot_at(horn, 1, 30, 100 - d * 6, 30 - d * 3 / 2, d % 2 ? PRISM_GOLD : PRISM_CREAM, false);
    }
    s_tip[0] = shape(s_root, 22, 22, TIP_COLOR);
    place(s_tip[0], LV_ALIGN_TOP_MID, 0, body_top + 24 - 104 - 6);
}

static void unicorn_top(int32_t body_top)
{
    static const int16_t LOCK[3][3] = {{20, 26, 50}, {42, 26, 62}, {64, 24, 44}};
    for (int i = 0; i < 3; i++) {       /* forelock, swept to the mane's side */
        lv_obj_t *lock = bar(s_root, LOCK[i][1], LOCK[i][2], RAINBOW[i * 2], 12);
        place(lock, LV_ALIGN_TOP_MID, LOCK[i][0], body_top - 12);
    }
}

/* Shaggy hair: a row of rounded locks hanging from `top`, in two shades. */
static void build_locks(lv_obj_t *parent, lv_align_t align, int32_t top, const int16_t (*locks)[3],
                        int count)
{
    for (int i = 0; i < count; i++) {
        lv_obj_t *lock = shape(parent, locks[i][1], locks[i][2], i % 2 ? COW_HAIR_B : COW_HAIR_A);
        place(lock, align, locks[i][0], top);
    }
}

static void build_critter(const look_t *k)
{
    /* Space above the body for antennae or ears; the hare's ears need more. */
    int32_t top_room = k->style == LOOK_HARE ? 172 : 110;
    s_style = k->style;
    /* Space under the body: a little for feet, more for the octopus's arms. */
    int32_t under = k->style == LOOK_OCTOPUS ? 56 : 10;
    make_root(k->style == LOOK_UNICORN ? 400 :
              k->body_w + (k->style == LOOK_PLAIN ? 60 : (k->style == LOOK_BUTTERFLY ? 216 : 170)),
              k->body_h + top_room + under);
    int32_t body_top = top_room;

    if (k->style == LOOK_BUTTERFLY) {
        build_wings(body_top);
    }
    if (k->style == LOOK_CRAB) {
        build_claws(k, body_top);
    }
    if (k->style == LOOK_OCTOPUS) {
        build_octopus_back(k, body_top);
    }
    if (k->style == LOOK_UNICORN) {
        unicorn_back(k, body_top);
    }
    if (k->style == LOOK_HARE) {
        /* A puff of a tail, then the ears: both blown the same way. */
        lv_obj_t *tail = shape(s_root, 48, 48, HARE_PALE);
        place(tail, LV_ALIGN_BOTTOM_MID, k->body_w / 2 + 6, -26);
        build_hare_ear(0, k, body_top, -36, 58, false);
        build_hare_ear(1, k, body_top, 36, 42, true);
    }

    /* Ears and antennae first so the body overlaps their bases. */
    if (k->ear) {
        for (int i = 0; i < 2; i++) {
            int32_t dx = (i == 0 ? -1 : 1) * (k->body_w * 34 / 100);
            lv_obj_t *ear = body_part(s_root, k->ear, k->ear, k->color);
            place(ear, LV_ALIGN_TOP_MID, dx, body_top - k->ear * 45 / 100);
            lv_obj_t *inner = shape(ear, k->ear * 55 / 100, k->ear * 55 / 100, 0xFFFFFF);
            lv_obj_set_style_bg_opa(inner, LV_OPA_30, 0);
            lv_obj_center(inner);
        }
    }
    const int16_t stalks[2] = {k->stalk_l, k->stalk_r};
    for (int i = 0; i < 2; i++) {
        if (!stalks[i]) {
            continue;
        }
        int32_t dx = (i == 0 ? -1 : 1) * (k->body_w * 24 / 100);
        int32_t y = body_top - stalks[i] + 16;
        s_stalk[i] = body_part(s_root, 14, stalks[i], k->color);
        place(s_stalk[i], LV_ALIGN_TOP_MID, dx, y);
        s_tip[i] = shape(s_root, 30, 30, TIP_COLOR);
        place(s_tip[i], LV_ALIGN_TOP_MID, dx, y - 22);
    }

    if (k->style == LOOK_COW) {
        build_horn(-1, k, body_top);
        build_horn(1, k, body_top);
        /* Ears that droop sideways, under the horns. */
        for (int i = 0; i < 2; i++) {
            int32_t dx = (i == 0 ? -1 : 1) * (k->body_w / 2 + 4);
            lv_obj_t *ear = body_part(s_root, 58, 32, k->color);
            place(ear, LV_ALIGN_TOP_MID, dx, body_top + 44);
            lv_obj_t *inner = shape(ear, 34, 16, COW_MUZZLE);
            lv_obj_set_style_bg_opa(inner, LV_OPA_50, 0);
            lv_obj_center(inner);
        }
    }

    if (k->style == LOOK_FOX) {
        build_fox_tail(k);
        build_fox_ear(-1, k, body_top);
        build_fox_ear(1, k, body_top);
    }

    s_body = body_part(s_root, k->body_w, k->body_h, k->color);
    place(s_body, LV_ALIGN_BOTTOM_MID, 0, -under);

    /* Belly: a shade lighter than the body, so it reads as colouring, not a feature. */
    s_belly = shape(s_body, k->body_w * 38 / 100, k->body_h * 20 / 100, 0xFFFFFF);
    lv_obj_set_style_bg_opa(s_belly, k->glow ? LV_OPA_70 : LV_OPA_20, 0);
    place(s_belly, LV_ALIGN_BOTTOM_MID, 0, -12);
    if (k->style == LOOK_COW) {
        lv_obj_add_flag(s_belly, LV_OBJ_FLAG_HIDDEN);   /* the muzzle sits there instead */
    }

    /* Feet; the octopus has its arms instead. */
    for (int i = 0; i < 2 && k->style != LOOK_OCTOPUS; i++) {
        /* A hare has long back feet. */
        int32_t foot_w = k->style == LOOK_HARE ? 84 : 56;
        lv_obj_t *foot = body_part(s_root, foot_w, 26, k->color);
        place(foot, LV_ALIGN_BOTTOM_MID,
                     (i == 0 ? -1 : 1) * (k->body_w * 23 / 100 + (foot_w - 56) / 2), 0);
    }

    if (k->style == LOOK_CRAB) {
        build_speckles();   /* before the eyes, so the eyes sit over them */
    }
    int32_t eye_y = -(k->body_h * 15 / 100);
    if (k->style == LOOK_OCTOPUS) {
        build_octopus_front(eye_y);
    }
    for (int i = 0; i < 2; i++) {
        int32_t dx = (i == 0) ? -(EYE_W + EYE_GAP) / 2 : (EYE_W + EYE_GAP) / 2;
        s_eye[i] = shape(s_body, EYE_W, EYE_OPEN_H, 0xFFFFFF);
        place(s_eye[i], LV_ALIGN_CENTER, dx, eye_y);
        s_pupil[i] = shape(s_eye[i], PUPIL_W, 34, FACE_COLOR);
        place(s_pupil[i], LV_ALIGN_CENTER, 0, 4);
    }

    /* Mouth: a curve and an oval, one shown at a time. */
    s_mouth_y = eye_y + 54;
    if (k->style == LOOK_HARE) {
        build_hare_face(eye_y);
        s_mouth_y = eye_y + 78;
    }
    if (k->style == LOOK_UNICORN) {
        snout(eye_y, 98);
        blush(eye_y, 70);
    }
    if (k->style == LOOK_FOX) {
        /* A narrow pale muzzle with a dark nose; the mouth sits under the nose. */
        lv_obj_t *muzzle = shape(s_body, 88, 60, FOX_PALE);
        place(muzzle, LV_ALIGN_CENTER, 0, eye_y + 70);
        lv_obj_t *nose = shape(muzzle, 20, 13, FOX_NOSE);
        place(nose, LV_ALIGN_TOP_MID, 0, 8);
        s_mouth_y = eye_y + 78;
    }
    if (k->style == LOOK_COW) {
        /* A pale muzzle with nostrils; the mouth moves down onto it. */
        lv_obj_t *muzzle = shape(s_body, k->body_w * 54 / 100, 70, COW_MUZZLE);
        place(muzzle, LV_ALIGN_CENTER, 0, eye_y + 72);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *nostril = shape(muzzle, 12, 9, COW_NOSTRIL);
            place(nostril, LV_ALIGN_TOP_MID, i == 0 ? -20 : 20, 12);
        }
        s_mouth_y = eye_y + 82;
        /* The fringe: long locks over the forehead, down to the tops of the eyes. */
        static const int16_t FRINGE[][3] = {
            {-62, 26, 40}, {-42, 28, 52}, {-21, 28, 46}, {0, 30, 58},
            {21, 28, 48},  {42, 28, 54},  {62, 26, 38},
        };
        build_locks(s_body, LV_ALIGN_TOP_MID, 4, FRINGE, 7);
    }
    s_mouth_curve = lv_arc_create(s_body);
    lv_obj_remove_style_all(s_mouth_curve);
    lv_obj_set_style_arc_width(s_mouth_curve, len(5), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_mouth_curve, lv_color_hex(FACE_COLOR), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_mouth_curve, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_mouth_curve, true, LV_PART_MAIN);
    lv_obj_remove_flag(s_mouth_curve, LV_OBJ_FLAG_CLICKABLE);
    s_mouth_open = shape(s_body, 26, 5, FACE_COLOR);

    if (k->style == LOOK_UNICORN) {
        unicorn_top(body_top);
    }
    if (k->style == LOOK_COW) {
        /* And the tuft that stands up between the antennae. */
        static const int16_t TUFT[][3] = {{-34, 26, 34}, {-15, 28, 46}, {8, 28, 52}, {30, 26, 36}};
        build_locks(s_root, LV_ALIGN_TOP_MID, body_top - 16, TUFT, 4);
    }

    /* Shown only while napping. */
    s_zzz = lv_label_create(s_parent);
    lv_label_set_text(s_zzz, "z z");
    lv_obj_set_style_text_color(s_zzz, lv_color_hex(0x8FA3C7), 0);
    /* Letters come in the sizes that were built in: the nearest one that is not too big. */
#if LV_FONT_MONTSERRAT_28
    if (px(14) >= 28) {
        lv_obj_set_style_text_font(s_zzz, &lv_font_montserrat_28, 0);
    } else
#endif
#if LV_FONT_MONTSERRAT_20
    if (px(14) >= 20) {
        lv_obj_set_style_text_font(s_zzz, &lv_font_montserrat_20, 0);
    }
#endif
    place(s_zzz, LV_ALIGN_TOP_RIGHT, -70, 70);
    lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

    /* Breathing bob, always on. */
    loop_anim(s_body, bob_cb, 0, px(5), 1100, true);
    if (k->glow) {
        loop_anim(s_belly, glow_cb, 70, 230, 1400, true);
    }
    if (k->shifting) {
        loop_anim(s_tinted, hue_cb, 0, 359, 9000, false);
    }
    /* Underfed: scrawny and flickering until it is fed back up. */
    if (s_underfed) {
        loop_anim(s_root, flicker_cb, 255, 110, 420, true);
    }

    eye_h_cb(NULL, rest_eye_h());
    apply_mouth();
}

static void rebuild(void)
{
    pet_action_t action = s_action;
    clear();
    s_is_egg = s_stage == STAGE_EGG;
    if (s_is_egg) {
        build_egg();
        return;
    }
    const look_t *k = s_stage == STAGE_HATCHLING ? &LOOK_HATCHLING
                                                 : &LOOKS[s_form < FORM_COUNT ? s_form : FORM_NONE];

    build_critter(k);
    /* Pick up whatever it was doing. */
    s_action = ACT_IDLE;
    if (action != ACT_IDLE) {
        s_action = action;
        start_action(ACT_IDLE);
    }
}

void pet_view_set_look(pet_stage_t stage, pet_form_t form, bool underfed)
{
    if (s_root && stage == s_stage && form == s_form && underfed == s_underfed) {
        return;
    }
    s_stage = stage;
    s_form = form;
    s_underfed = underfed;
    if (s_parent) {
        rebuild();
    }
}

void pet_view_create(lv_obj_t *parent)
{
    s_parent = parent;
    rebuild();
    s_blink_timer = lv_timer_create(blink_cb, 2500, NULL);
}
