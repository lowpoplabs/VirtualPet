#include "pet_pages.h"

#include <stdio.h>
#include <string.h>

#define COLOR_TEXT   0xB8C0CC
#define COLOR_DIM    0x5A6270
#define COLOR_ACCENT 0x8CF2B0
#define COLOR_WARN   0xFFB454
#define COLOR_PANEL  0x1A1F27
#define COLOR_PRESS  0x2C3440

#define PAGE_TOP     52     /* below the title */
#define PAGE_BOTTOM  30     /* clear of the rounded corners */
#define PAGE_SIDE    34

static const uint32_t GROUP_COLOR[GROUP_COUNT] = {
    [GROUP_NETWORKS] = 0x6FA8FF, [GROUP_SCAVENGE] = 0xFFB454, [GROUP_MUSIC] = 0xE58CF2,
    [GROUP_CARS] = 0xFF6B6B,     [GROUP_GADGETS] = 0x8CF2B0,  [GROUP_OTHER] = 0x7A8494,
};

static const pet_pages_host_t *s_host;
static lv_obj_t *s_screen;
static lv_obj_t *s_page;
static pet_page_t s_current = PAGE_HOME;
static pet_page_t s_info_parent = PAGE_HOME;
static int s_name_habitat = -1;     /* the naming page: which habitat, or -1 for the pet */
static int32_t s_w, s_h, s_body_w;

/* ---- Small builders ---------------------------------------------------- */

static lv_obj_t *box(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *filled(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color, int32_t radius)
{
    lv_obj_t *o = box(parent, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

/*
 * "Text size" in the settings. Large sets the pages in the next size of
 * letters up, 20 for 14 and 28 for 20, for eyes that want their reading
 * glasses at the small one.
 */
static bool large(void)
{
    return s_host && s_host->settings->text_large;
}

/* A height or an offset that has to follow the small letters: 14 becomes 20. */
static int32_t tx(int32_t n)
{
    return large() ? (n * 10 + 3) / 7 : n;
}

static const lv_font_t *font(bool big)
{
    if (large()) {
        return big ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    }
    return big ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
}

static lv_obj_t *text(lv_obj_t *parent, const char *str, uint32_t color, bool big)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, str);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font(big), 0);
    return l;
}

/* A full-width line that wraps. */
static lv_obj_t *para(lv_obj_t *parent, const char *str, uint32_t color)
{
    lv_obj_t *l = text(parent, str, color, false);
    lv_obj_set_width(l, LV_PCT(100));
    return l;
}

static lv_obj_t *row(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *r = box(parent, LV_PCT(100), h);
    return r;
}

static lv_obj_t *button(lv_obj_t *parent, int32_t w, int32_t h, const char *label,
                        lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = filled(parent, w, h, COLOR_PANEL, 12);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_PRESS), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = text(b, label, COLOR_TEXT, true);
    lv_obj_center(l);
    return b;
}

static void bar(lv_obj_t *parent, int done, int needed)
{
    lv_obj_t *track = filled(parent, LV_PCT(100), 8, 0x232830, 4);
    if (needed > 0 && done > 0) {
        int32_t w = s_body_w * (done > needed ? needed : done) / needed;
        lv_obj_t *fill = filled(track, w < 8 ? 8 : w, 8, COLOR_ACCENT, 4);
        lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void gap(lv_obj_t *parent, int32_t h)
{
    box(parent, 1, h);
}

/* ---- Opening and closing ----------------------------------------------- */

static pet_page_t parent_of(pet_page_t page)
{
    switch (page) {
    case PAGE_DEX:
    case PAGE_BADGES:
    case PAGE_FARM:
    case PAGE_PLACES:
    case PAGE_SETTINGS: return PAGE_MENU;
    case PAGE_INFO:     return s_info_parent;
    case PAGE_NAME:     return s_name_habitat >= 0 ? PAGE_PLACES : PAGE_INFO;
    case PAGE_RESET:    return PAGE_SETTINGS;
    case PAGE_RETIRE:   return PAGE_FARM;
    case PAGE_MOVE:     return PAGE_INFO;
    default:            return PAGE_HOME;
    }
}

/* The swipe that leaves a page: the opposite of the one that opened it. */
static lv_dir_t back_swipe(pet_page_t page)
{
    switch (page) {
    case PAGE_JOURNAL: return LV_DIR_LEFT;
    case PAGE_RECENT:  return LV_DIR_TOP;
    case PAGE_MENU:    return LV_DIR_BOTTOM;
    case PAGE_NAME:    return LV_DIR_NONE;     /* swipes belong to the keyboard there */
    default:           return LV_DIR_RIGHT;
    }
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_pages_open(parent_of(s_current));
}

/*
 * Holding the Settings title opens the preview of every form, but only
 * while Dev log is on: the forms are meant to be a surprise, so a stray
 * hold must not spoil them. Nothing on screen says so.
 */
static void title_hold_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_current == PAGE_SETTINGS && s_host->preview && s_host->settings->dev_log) {
        pet_pages_open(PAGE_HOME);
        s_host->preview();
    }
}

static lv_obj_t *page_begin(const char *title, bool scrolls)
{
    s_page = filled(s_screen, s_w, s_h, 0x000000, 0);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);     /* taps stop here, not on the pet below */

    lv_obj_t *t = text(s_page, title, COLOR_TEXT, true);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, large() ? 12 : 16);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(t, 16);
    lv_obj_add_event_cb(t, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(t, title_hold_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *body = box(s_page, s_body_w, s_h - PAGE_TOP - PAGE_BOTTOM);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, PAGE_TOP);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);
    if (scrolls) {
        /* Clickable so a finger on plain text lands on the body: only then does a drag scroll it. */
        lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
        /* Up and down only, so a sideways swipe is still free to mean "back". */
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        lv_obj_set_style_bg_color(body, lv_color_hex(COLOR_DIM), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(body, LV_OPA_70, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(body, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(body, 2, LV_PART_SCROLLBAR);
    }
    return body;
}

/* ---- Diet --------------------------------------------------------------- */

#define CHART_H 140

static uint32_t day_total(const pet_day_t *d)
{
    uint32_t total = 0;
    for (int g = 0; d && g < GROUP_COUNT; g++) {
        total += d->group_pts[g];
    }
    return total;
}

static void build_diet(void)
{
    const pet_t *pet = s_host->pet;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Diet", true);
    char buf[96];

    /* The week: one stacked column per day, today on the right. */
    uint32_t today = pet->today.day, biggest = 1;
    for (int i = 0; i < 7; i++) {
        const pet_day_t *d = pet_app_day(today - 6 + (uint32_t)i);
        uint32_t total = day_total(d);
        if (total > biggest) {
            biggest = total;
        }
    }
    lv_obj_t *chart = row(body, CHART_H + tx(24));
    int32_t col_w = s_body_w / 7;
    for (int i = 0; i < 7; i++) {
        uint32_t day = today - 6 + (uint32_t)i;
        const pet_day_t *d = pet_app_day(day);
        int32_t x = col_w * i + (col_w - 26) / 2, y = CHART_H;
        for (int g = 0; d && g < GROUP_COUNT; g++) {
            if (d->group_pts[g] == 0) {
                continue;
            }
            int32_t h = (int32_t)(d->group_pts[g] * (uint32_t)CHART_H / biggest);
            if (h < 3) {
                h = 3;
            }
            y -= h;
            lv_obj_t *seg = filled(chart, 26, h - 1, GROUP_COLOR[g], 3);
            lv_obj_set_pos(seg, x, y < 0 ? 0 : y);
        }
        if (day_total(d) == 0) {
            lv_obj_set_pos(filled(chart, 26, 2, 0x232830, 1), x, CHART_H - 2);
        }
        static const char *const LETTER[7] = {"S", "M", "T", "W", "T", "F", "S"};
        lv_obj_t *l = text(chart, LETTER[(day + 4) % 7], i == 6 ? COLOR_ACCENT : COLOR_DIM, false);
        lv_obj_set_pos(l, x + (large() ? 6 : 8), CHART_H + 6);
    }

    /* Key, and what has been missing: the point of the chart. */
    lv_obj_t *key = row(body, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(key, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(key, 6, 0);
    lv_obj_set_style_pad_row(key, 4, 0);
    for (int g = 0; g < GROUP_COUNT; g++) {
        /* Dot and name travel together when the line wraps. */
        lv_obj_t *item = box(key, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_margin_right(item, 8, 0);
        lv_obj_align(filled(item, 10, 10, GROUP_COLOR[g], 5), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(text(item, pet_group_name((pet_group_t)g), COLOR_TEXT, false), LV_ALIGN_LEFT_MID, 16, 0);
    }
    if (pet_app_line(PET_LINE_MISSING, buf, sizeof(buf))) {
        para(body, buf, COLOR_WARN);
    }

    /* Growing up. */
    gap(body, 6);
    int days_done, days_needed, meals_done, meals_needed;
    pet_growth_counts(pet, &days_done, &days_needed, &meals_done, &meals_needed);
    pet_app_line(PET_LINE_STAGE, buf, sizeof(buf));
    text(body, buf, COLOR_TEXT, true);
    if (pet->stage == STAGE_ADULT && pet->retire_ready) {
        pet_app_line(PET_LINE_GROWTH_DAYS, buf, sizeof(buf));
        para(body, buf, COLOR_ACCENT);
    } else {
        if (days_needed > 0) {
            pet_app_line(PET_LINE_GROWTH_DAYS, buf, sizeof(buf));
            para(body, buf, COLOR_DIM);
            bar(body, days_done, days_needed);
        }
        pet_app_line(PET_LINE_GROWTH_MEALS, buf, sizeof(buf));
        para(body, buf, COLOR_DIM);
        bar(body, meals_done, meals_needed);
    }
    if (pet->stage < STAGE_ADULT) {
        pet_app_line(PET_LINE_LEANING, buf, sizeof(buf));
        para(body, buf, COLOR_ACCENT);
    }
}

/* ---- Journal ------------------------------------------------------------ */

static void journal_day(lv_obj_t *body, const pet_day_t *d, uint32_t today)
{
    static const char *const NAME[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };
    char buf[200];
    uint32_t ago = today - d->day;
    text(body, ago == 0 ? "Today" : (ago == 1 ? "Yesterday" : NAME[(d->day + 4) % 7]),
         ago == 0 ? COLOR_ACCENT : COLOR_TEXT, true);
    pet_journal_entry(s_host->pet, d, ago == 0, buf, sizeof(buf));
    para(body, buf, COLOR_TEXT);
    if (d->foods > 0) {
        snprintf(buf, sizeof(buf), "eaten %d    meals %d    kinds %d", d->foods, d->meals, d->types);
        para(body, buf, COLOR_DIM);
    }
    gap(body, 6);
}

static void build_journal(void)
{
    const pet_t *pet = s_host->pet;
    lv_obj_t *body = page_begin("Journal  " LV_SYMBOL_RIGHT, true);
    journal_day(body, &pet->today, pet->today.day);
    for (int i = 0; i < PET_HISTORY_DAYS; i++) {
        if (pet->history[i].day != 0) {
            journal_day(body, &pet->history[i], pet->today.day);
        }
    }
}

/* ---- Recent finds ------------------------------------------------------- */

static void build_recent(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_UP "  Recent finds", false);
    int n = *s_host->recent_count;
    char buf[64];
    if (n == 0) {
        para(body, "Nothing eaten since waking up.", COLOR_DIM);
    }
    for (int i = 0; i < n; i++) {
        const pet_recent_t *r = &s_host->recent[i];
        lv_obj_t *line = row(body, tx(24));
        snprintf(buf, sizeof(buf), "%02d:%02d", (int)(r->time % 86400 / 3600),
                 (int)(r->time % 3600 / 60));
        lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_LEFT_MID, 0, 0);

        pet_app_recent_text(i, buf, sizeof(buf));
        lv_obj_align(text(line, buf, r->new_food ? COLOR_ACCENT : COLOR_TEXT, false),
                     LV_ALIGN_LEFT_MID, tx(56), 0);

        snprintf(buf, sizeof(buf), "+%.1f", (double)r->value);
        lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_RIGHT_MID, 0, 0);
    }
    gap(body, 4);
    para(body, "Kinds of food only. Names are shown once and never kept.", COLOR_DIM);
}

/* ---- Menu --------------------------------------------------------------- */

static void menu_cb(lv_event_t *e)
{
    pet_page_t page = (pet_page_t)(uintptr_t)lv_event_get_user_data(e);
    if (page == PAGE_INFO) {
        s_info_parent = PAGE_MENU;
    }
    pet_pages_open(page);
}

/*
 * The little picture on each menu tile, drawn from plain shapes like the
 * pet itself. Each sits in a 56 x 56 box.
 */
static lv_obj_t *dot(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color, int32_t x, int32_t y)
{
    lv_obj_t *o = filled(parent, w, h, color, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(o, x, y);
    return o;
}

static lv_obj_t *slab(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color, int32_t x, int32_t y)
{
    lv_obj_t *o = filled(parent, w, h, color, 4);
    lv_obj_set_pos(o, x, y);
    return o;
}

static void menu_icon(lv_obj_t *box56, pet_page_t page)
{
    switch (page) {
    case PAGE_DEX:      /* a collection: four finds, one still missing */
        slab(box56, 24, 24, 0x8CF2B0, 2, 2);
        slab(box56, 24, 24, 0xF28C8C, 30, 2);
        slab(box56, 24, 24, 0x7DB8F2, 2, 30);
        slab(box56, 24, 24, 0x2C3440, 30, 30);
        break;
    case PAGE_BADGES:   /* a medal on its ribbon */
        slab(box56, 12, 26, 0xF2811D, 14, 0);
        slab(box56, 12, 26, 0xE8674C, 30, 0);
        dot(box56, 38, 38, 0xFFD36B, 9, 18);
        dot(box56, 20, 20, 0xFFEDB8, 18, 27);
        break;
    case PAGE_PLACES:   /* a map pin */
        dot(box56, 16, 26, 0x3F8FE3, 20, 28);
        dot(box56, 40, 40, 0x3F8FE3, 8, 0);
        dot(box56, 16, 16, 0xFFFFFF, 20, 12);
        dot(box56, 30, 8, 0x2C3440, 13, 48);
        break;
    case PAGE_FARM:     /* a barn */
        slab(box56, 44, 32, 0xD9573F, 6, 24);
        slab(box56, 52, 12, 0xF3D9B4, 2, 14);
        slab(box56, 30, 12, 0xF3D9B4, 13, 4);
        slab(box56, 16, 22, 0xF3D9B4, 20, 34);
        break;
    case PAGE_INFO:     /* the hatchling */
        slab(box56, 5, 16, 0x8CF2B0, 15, 4);
        slab(box56, 5, 16, 0x8CF2B0, 36, 4);
        dot(box56, 12, 12, 0xFFE27A, 11, 0);
        dot(box56, 12, 12, 0xFFE27A, 33, 0);
        dot(box56, 46, 42, 0x8CF2B0, 5, 14);
        dot(box56, 12, 16, 0xFFFFFF, 14, 22);
        dot(box56, 12, 16, 0xFFFFFF, 30, 22);
        dot(box56, 6, 8, 0x10131A, 17, 27);
        dot(box56, 6, 8, 0x10131A, 33, 27);
        break;
    default:            /* settings: three sliders */
        for (int i = 0; i < 3; i++) {
            static const uint32_t KNOB[3] = {0x8CF2B0, 0xFFB454, 0xC79BF2};
            static const int8_t AT[3] = {30, 8, 22};
            slab(box56, 52, 6, 0x4A5361, 2, 9 + i * 18);
            dot(box56, 16, 16, KNOB[i], AT[i], 4 + i * 18);
        }
        break;
    }
}

/* The small line under each tile's name: where things stand. */
static void menu_status(pet_page_t page, char *buf, size_t cap, uint32_t *color)
{
    pet_line_t line = page == PAGE_DEX ? PET_LINE_TILE_DEX
                    : page == PAGE_BADGES ? PET_LINE_TILE_BADGES
                    : page == PAGE_PLACES ? PET_LINE_TILE_PLACES
                    : page == PAGE_FARM ? PET_LINE_TILE_FARM
                    : page == PAGE_INFO ? PET_LINE_TILE_PET : PET_LINE_TILE_SETTINGS;
    if (pet_app_line(line, buf, cap)) {
        *color = COLOR_WARN;
    }
}

static void build_menu(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_DOWN "  Menu", false);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(body, 10, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    static const struct {
        const char *label;
        pet_page_t page;
    } ITEMS[] = {
        {"Food-dex", PAGE_DEX}, {"Badges", PAGE_BADGES}, {"Places", PAGE_PLACES},
        {"Farm", PAGE_FARM},    {"Pet", PAGE_INFO},      {"Settings", PAGE_SETTINGS},
    };
    /* Three rows of two fill the screen below the title. */
    int32_t w = (s_body_w - 10) / 2;
    int32_t h = (s_h - PAGE_TOP - PAGE_BOTTOM - 20) / 3;
    for (size_t i = 0; i < sizeof(ITEMS) / sizeof(ITEMS[0]); i++) {
        lv_obj_t *tile = filled(body, w, h, COLOR_PANEL, 16);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(tile, lv_color_hex(COLOR_PRESS), LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, menu_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ITEMS[i].page);

        lv_obj_t *icon = box(tile, 56, 56);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 14, large() ? 6 : 12);
        menu_icon(icon, ITEMS[i].page);

        char status[32];
        uint32_t color = COLOR_DIM;
        menu_status(ITEMS[i].page, status, sizeof(status), &color);
        lv_obj_align(text(tile, ITEMS[i].label, COLOR_TEXT, true), LV_ALIGN_BOTTOM_LEFT, 14,
                     status[0] ? (large() ? -34 : -30) : -14);
        if (status[0]) {
            /* A long name is cut short with dots, not run off the tile. */
            lv_obj_t *st = text(tile, status, color, false);
            lv_label_set_long_mode(st, LV_LABEL_LONG_DOT);
            lv_obj_set_size(st, w - 22, lv_font_get_line_height(font(false)));
            lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 14, large() ? -8 : -10);
        }
    }
}

/* ---- Food-dex ----------------------------------------------------------- */

static void build_dex(void)
{
    const pet_t *pet = s_host->pet;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Food-dex", true);
    char buf[48];
    int tasted = 0;
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        tasted += pet->tasted[i] > 0;
    }
    snprintf(buf, sizeof(buf), "%d of %d tasted", tasted, FOOD_TYPE_COUNT);
    para(body, buf, tasted == FOOD_TYPE_COUNT ? COLOR_ACCENT : COLOR_DIM);

    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        if (i == 0 || i == FOOD_FIRST_BLE) {
            gap(body, 2);
            text(body, i == 0 ? "Wi-Fi" : "Bluetooth", COLOR_TEXT, true);
        }
        bool known = pet->tasted[i] > 0;
        lv_obj_t *line = row(body, tx(24));
        lv_obj_t *dot = filled(line, 14, 14,
                               known ? GROUP_COLOR[pet_food_group((food_type_t)i)] : 0x232830, 7);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
        /* Not yet eaten: a shadow where the food will go. */
        lv_obj_align(text(line, known ? food_label((food_type_t)i) : "? ? ?",
                          known ? COLOR_TEXT : COLOR_DIM, false),
                     LV_ALIGN_LEFT_MID, 26, 0);
        if (known) {
            snprintf(buf, sizeof(buf), "x %lu", (unsigned long)pet->tasted[i]);
            lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_RIGHT_MID, -6, 0);
        }
    }
}

/* ---- Badges ------------------------------------------------------------- */

/*
 * One badge: its mark, its name, and under that how it is earned. The
 * second line wraps when it is too long for the screen, and the row grows
 * to hold it. Returns the mark, for a tick to go in.
 */
static lv_obj_t *badge_row(lv_obj_t *body, uint32_t mark_color, const char *name, uint32_t name_color,
                           const char *how)
{
    lv_obj_t *line = row(body, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(line, tx(42), 0);
    lv_obj_t *mark = filled(line, 30, 30, mark_color, 15);
    lv_obj_set_pos(mark, 0, (tx(42) - 30) / 2);
    lv_obj_set_pos(text(line, name, name_color, false), 42, 2);
    lv_obj_t *l = text(line, how, COLOR_DIM, false);
    lv_obj_set_width(l, s_body_w - 42 - 6);
    lv_obj_set_pos(l, 42, tx(21));
    return mark;
}

static void build_badges(void)
{
    const pet_t *pet = s_host->pet;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Badges", true);
    char buf[48];
    int earned = 0;
    for (int b = 0; b < BADGE_COUNT; b++) {
        earned += pet_has_badge(pet, (pet_badge_t)b);
    }
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        earned += pet->places.kind[k].count > 0;
    }
    earned += pet->places.offgrid_tier > 0;
    earned += (pet->farm.walk_tier > 0) + (pet->farm.ride_tier > 0);
    snprintf(buf, sizeof(buf), "%d of %d earned", earned, BADGE_COUNT + PLACE_KIND_COUNT + 1 + 2);
    para(body, buf, COLOR_DIM);

    /* Earned ones first. */
    for (int pass = 0; pass < 2; pass++) {
        for (int b = 0; b < BADGE_COUNT; b++) {
            bool has = pet_has_badge(pet, (pet_badge_t)b);
            if (has != (pass == 0)) {
                continue;
            }
            lv_obj_t *mark = badge_row(body, has ? COLOR_ACCENT : COLOR_PANEL, pet_badge_name((pet_badge_t)b),
                                       has ? COLOR_TEXT : COLOR_DIM, pet_badge_how((pet_badge_t)b));
            if (has) {
                lv_obj_center(text(mark, LV_SYMBOL_OK, 0x10141A, false));
            }
        }
    }

    /* Places: bronze, silver and gold. The last row is "off the grid". */
    static const uint32_t TIER_COLOR[PLACE_TIER_MAX + 1] = {COLOR_PANEL, 0xCD7F32, 0xC8CCD4, 0xFFD54A};
    gap(body, 6);
    text(body, "Places", COLOR_TEXT, true);
    for (int k = 0; k <= PLACE_KIND_COUNT; k++) {
        bool grid = k == PLACE_KIND_COUNT;
        int count = grid ? 0 : pet->places.kind[k].count;
        int tier = grid ? pet->places.offgrid_tier : place_tier(count);
        if (!tier) {
            snprintf(buf, sizeof(buf), "%s", grid ? "an hour with no signal at all"
                                                  : place_kind_how((place_kind_t)k));
        } else if (grid) {
            snprintf(buf, sizeof(buf), "%s", place_tier_name(tier));
        } else {
            snprintf(buf, sizeof(buf), "%s, %d so far", place_tier_name(tier), count);
        }
        lv_obj_t *mark = badge_row(body, TIER_COLOR[tier], grid ? "Off the grid" : place_kind_name((place_kind_t)k),
                                   tier ? COLOR_TEXT : COLOR_DIM, buf);
        if (tier) {
            lv_obj_center(text(mark, LV_SYMBOL_OK, 0x10141A, false));
        }
    }
    gap(body, 4);
    para(body, "Silver at 3, gold at 10. Only the kind of place is kept, never its name.", COLOR_DIM);

    /* Distance: all the owner's pets together, where a device can tell how far it went. */
    gap(body, 6);
    text(body, "Travel", COLOR_TEXT, true);
    for (int riding = 0; riding < 2; riding++) {
        int tier = riding ? pet->farm.ride_tier : pet->farm.walk_tier;
        long miles = (long)(pet_distance_total(pet, riding) / PET_MILE_M);
        if (!tier && miles == 0) {
            snprintf(buf, sizeof(buf), "%d miles %s", pet_distance_goal_miles(riding, 1),
                     riding ? "riding" : "on foot");
        } else if (!tier) {
            snprintf(buf, sizeof(buf), "%ld of %d miles", miles, pet_distance_goal_miles(riding, 1));
        } else {
            snprintf(buf, sizeof(buf), "%s, %ld miles so far", place_tier_name(tier), miles);
        }
        lv_obj_t *mark = badge_row(body, TIER_COLOR[tier], riding ? "On the road" : "On foot",
                                   tier ? COLOR_TEXT : COLOR_DIM, buf);
        if (tier) {
            lv_obj_center(text(mark, LV_SYMBOL_OK, 0x10141A, false));
        }
    }
    gap(body, 4);
    para(body, "On foot: 10, 50 and 200 miles. Riding: 100, 1,000 and 5,000. All your pets count "
               "together. Only the distance is kept, never where.", COLOR_DIM);
}

/* ---- Places ------------------------------------------------------------- */

static void place_name_cb(lv_event_t *e)
{
    s_name_habitat = (int)(intptr_t)lv_event_get_user_data(e);
    pet_pages_open(PAGE_NAME);
}

static void build_places(void)
{
    const pet_places_t *pl = &s_host->pet->places;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Places", true);
    char buf[64], here[80];
    int home = places_home(pl);

    bool unknown = pet_app_line(PET_LINE_HERE, here, sizeof(here));
    para(body, here, pl->here >= 0 ? COLOR_ACCENT : (unknown ? COLOR_WARN : COLOR_DIM));
    if (places_count(pl) == 0) {
        gap(body, 8);
        para(body, "No places yet. Ten minutes in one spot and the pet makes it a habitat.",
             COLOR_TEXT);
    }

    /* Home first, then the rest in the order they were found. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < HABITAT_MAX; i++) {
            const habitat_t *h = &pl->habitat[i];
            if (!h->prints || (i == home) != (pass == 0)) {
                continue;
            }
            lv_obj_t *line = row(body, tx(46));
            pet_app_habitat_text(i, 0, buf, sizeof(buf));
            lv_obj_align(text(line, buf, i == pl->here ? COLOR_ACCENT : COLOR_TEXT, false),
                         LV_ALIGN_TOP_LEFT, 0, 3);
            pet_app_habitat_text(i, 1, buf, sizeof(buf));
            lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_TOP_LEFT, 0, tx(23));
            lv_obj_align(button(line, 56, 38, LV_SYMBOL_EDIT, place_name_cb, (void *)(intptr_t)i),
                         LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    gap(body, 4);
    para(body, "A place is known by scrambled prints of the networks around it. "
               "No network names or addresses are kept.", COLOR_DIM);
}

/* ---- Farm --------------------------------------------------------------- */

#define FARM_FIELD   0x14241A
#define FARM_PICKED  0x24402E

static lv_obj_t *s_farm_tile[FARM_MAX];
static lv_obj_t *s_farm_line[3];

/* A retired pet, small: its form's colour and the one or two things that mark it out. */
static void farm_icon(lv_obj_t *box56, pet_form_t form)
{
    pet_icon_part_t part[PET_ICON_PARTS_MAX];
    int count = pet_farm_icon(form, part, PET_ICON_PARTS_MAX);
    for (int i = 0; i < count && i < PET_ICON_PARTS_MAX; i++) {
        lv_obj_set_pos(filled(box56, part[i].w, part[i].h, part[i].color, part[i].radius),
                       part[i].x, part[i].y);
    }
}

/* The three lines under the field, about the resident that was tapped. */
static void farm_show(int which)
{
    const pet_farm_t *farm = &s_host->pet->farm;
    if (which < 0 || which >= farm->count || !s_farm_line[0]) {
        return;
    }
    for (int i = 0; i < farm->count; i++) {
        if (s_farm_tile[i]) {
            lv_obj_set_style_bg_color(s_farm_tile[i], lv_color_hex(i == which ? FARM_PICKED : FARM_FIELD), 0);
        }
    }
    char buf[80];
    for (int line = 0; line < 3; line++) {
        pet_app_resident_text(which, line, buf, sizeof(buf));
        lv_label_set_text(s_farm_line[line], buf);
    }
}

static void farm_tap_cb(lv_event_t *e)
{
    farm_show((int)(intptr_t)lv_event_get_user_data(e));
}

static void retire_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_pages_open(PAGE_RETIRE);
}

static void build_farm(void)
{
    const pet_t *pet = s_host->pet;
    const pet_farm_t *farm = &pet->farm;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Baandwidth Farms", true);
    char buf[96];
    memset(s_farm_tile, 0, sizeof(s_farm_tile));
    memset(s_farm_line, 0, sizeof(s_farm_line));

    if (farm->count == 0) {
        gap(body, 30);
        lv_obj_t *l = para(body, "The farm is empty.", COLOR_TEXT);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        l = para(body, "Pets that have lived a full life retire here.", COLOR_DIM);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        gap(body, 20);
    } else {
        /* The field: four across, the newest last. */
        int rows = (farm->count + 3) / 4;
        int32_t cell = s_body_w / 4;
        lv_obj_t *field = filled(body, s_body_w, rows * 72 + 12, FARM_FIELD, 16);
        for (int i = 0; i < farm->count; i++) {
            lv_obj_t *tile = filled(field, cell - 6, 68, FARM_FIELD, 12);
            lv_obj_set_pos(tile, 3 + (i % 4) * cell, 6 + (i / 4) * 72);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(tile, farm_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *icon = box(tile, 56, 56);
            lv_obj_center(icon);
            farm_icon(icon, (pet_form_t)farm->resident[i].form);
            s_farm_tile[i] = tile;
        }
        s_farm_line[0] = text(body, "", COLOR_ACCENT, true);
        s_farm_line[1] = para(body, "", COLOR_TEXT);
        s_farm_line[2] = para(body, "", COLOR_DIM);
        farm_show(farm->count - 1);
        if (farm->moved_on) {
            snprintf(buf, sizeof(buf), farm->moved_on == 1 ? "%d earlier pet has moved on."
                                                             : "%d earlier pets have moved on.",
                     farm->moved_on);
            para(body, buf, COLOR_DIM);
        }
        gap(body, 4);
    }

    if (pet_app_line(PET_LINE_RETIRING, buf, sizeof(buf))) {
        button(body, s_body_w, 44, buf, retire_open_cb, NULL);
    } else {
        para(body, buf, COLOR_DIM);
    }
    gap(body, 8);
}

/* ---- Pet card ----------------------------------------------------------- */

static void fact(lv_obj_t *body, const char *what, const char *value)
{
    lv_obj_t *line = row(body, tx(24));
    lv_obj_align(text(line, what, COLOR_DIM, false), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(text(line, value, COLOR_TEXT, false), LV_ALIGN_RIGHT_MID, -8, 0);    /* clear of the scrollbar */
}

/*
 * The owner's pick of favourite food, on the card while the pet is still
 * growing. Short names, so the button fits at either text size.
 */
static lv_obj_t *s_favourite_label;

static void favourite_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_app_set_favourite((s_host->pet->favourite + 1) % (PET_FAVOURITE_MAX + 1));
    char buf[32];
    pet_app_line(PET_LINE_FAVOURITE, buf, sizeof(buf));
    lv_label_set_text(s_favourite_label, buf);
}

static void name_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_name_habitat = -1;
    pet_pages_open(PAGE_NAME);
}

static void move_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_pages_open(PAGE_MOVE);
}

static void build_info(void)
{
    const pet_t *pet = s_host->pet;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Pet", true);
    char buf[64];

    if (pet->away) {
        /* An empty pasture: no pet to describe, only how one comes to be here. */
        para(body, "The pasture is empty.", COLOR_ACCENT);
        para(body, "Its pet has moved to another device. The farm, your badges and your places "
                   "are still here, and place badges still count.", COLOR_TEXT);
        gap(body, 6);
        if (pet_app_can_exchange()) {
            button(body, s_body_w, 44, "Bring a pet here", move_open_cb, NULL);
        }
        return;
    }

    /* Its name, and the button that names it: the pet's own, so here and not in Settings. */
    lv_obj_t *named = row(body, 44);
    lv_obj_t *name = text(named, pet->name[0] ? pet->name : "(no name yet)",
                          pet->name[0] ? COLOR_ACCENT : COLOR_DIM, true);
    lv_obj_set_width(name, s_body_w - 108);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(button(named, 96, 38, LV_SYMBOL_EDIT, name_cb, NULL), LV_ALIGN_RIGHT_MID, -8, 0);
    /* What the bell on the home screen is about. */
    const char *craving = pet_craving_text(pet);
    if (craving[0]) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_BELL "  %s", craving);
        para(body, buf, COLOR_WARN);
    }
    if (pet_can_retire(pet)) {
        para(body, LV_SYMBOL_BELL "  ready to retire: see the farm", COLOR_WARN);
    }
    gap(body, 2);

    pet_app_line(PET_LINE_STAGE, buf, sizeof(buf));
    fact(body, "stage", buf);
    snprintf(buf, sizeof(buf), "%d", pet->generation);
    fact(body, "generation", buf);
    pet_app_line(PET_LINE_AGE, buf, sizeof(buf));
    fact(body, "age", buf);
    pet_app_line(PET_LINE_TRAITS, buf, sizeof(buf));
    fact(body, "traits", buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)pet->meals);
    fact(body, "meals", buf);
    pet_app_line(PET_LINE_EATS_MOSTLY, buf, sizeof(buf));
    fact(body, "eats mostly", buf);
    pet_app_line(PET_LINE_BADGES, buf, sizeof(buf));
    fact(body, "badges", buf);
    if (pet->stage < STAGE_ADULT) {
        gap(body, 4);
        pet_app_line(PET_LINE_FAVOURITE, buf, sizeof(buf));
        lv_obj_t *b = button(body, s_body_w, 44, buf, favourite_cb, NULL);
        s_favourite_label = lv_obj_get_child(b, 0);
        para(body, "Tap to choose. Its favourite food counts for more in what it grows into, "
                   "which helps where there is mostly Wi-Fi or mostly Bluetooth about.", COLOR_DIM);
    }
    if (pet->is_static) {
        gap(body, 4);
        para(body, "Underfed: it needs a good few meals to recover.", COLOR_WARN);
    }
    if (pet_app_can_exchange()) {
        gap(body, 4);
        button(body, s_body_w, 44, "Move or visit", move_open_cb, NULL);
        para(body, "Move this pet to another of your devices, or bring the farms on two of them level.",
             COLOR_DIM);
    }
}

/* ---- Settings ----------------------------------------------------------- */

static lv_obj_t *s_value[SET_COUNT];

static void settings_refresh(void)
{
    char buf[24];
    for (int i = 0; i < SET_COUNT; i++) {
        if (s_value[i]) {
            pet_app_setting_text((pet_setting_id_t)i, buf, sizeof(buf));
            lv_label_set_text(s_value[i], buf);
        }
    }
}

static void setting_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    bool was_large = large();
    s_host->change((pet_setting_id_t)(code / 2), (code & 1) ? 1 : -1);
    if (large() != was_large) {
        /* Every row changes size: draw the page again, with this row still in sight. */
        pet_pages_open(PAGE_SETTINGS);
        if (s_value[SET_TEXT]) {
            lv_obj_t *line = lv_obj_get_parent(s_value[SET_TEXT]);
            lv_obj_update_layout(lv_obj_get_parent(line));
            lv_obj_scroll_to_view(line, LV_ANIM_OFF);
        }
        return;
    }
    settings_refresh();
}

static void reset_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_pages_open(PAGE_RESET);
}

static lv_obj_t *setting_row(lv_obj_t *body, const char *label)
{
    lv_obj_t *line = row(body, 44);
    lv_obj_align(text(line, label, COLOR_TEXT, false), LV_ALIGN_LEFT_MID, 0, 0);
    return line;
}

static void build_settings(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Settings", true);
    memset(s_value, 0, sizeof(s_value));

    lv_obj_t *line;
    for (int i = 0; i < SET_COUNT; i++) {
        if (!pet_app_setting_shown((pet_setting_id_t)i)) {
            continue;
        }
        line = setting_row(body, pet_app_setting_label((pet_setting_id_t)i));
        s_value[i] = text(line, "", COLOR_ACCENT, false);
        lv_obj_align(s_value[i], LV_ALIGN_RIGHT_MID, -108, 0);
        lv_obj_align(button(line, 44, 38, LV_SYMBOL_MINUS, setting_cb, (void *)(intptr_t)(i * 2)),
                     LV_ALIGN_RIGHT_MID, -52, 0);
        lv_obj_align(button(line, 44, 38, LV_SYMBOL_PLUS, setting_cb, (void *)(intptr_t)(i * 2 + 1)),
                     LV_ALIGN_RIGHT_MID, 0, 0);
    }
    settings_refresh();
    gap(body, 4);
    para(body, s_host->screen_managed
                   ? "Pet: pause it when the device sits on a shelf; its time stands still "
                     "and it stops scanning."
                   : "Screen: \"stays on\" for a desk, \"sleeps\" for a backpack. "
                     "Pet: pause it when the device sits on a shelf; its time stands still "
                     "and it stops scanning.", COLOR_DIM);
    para(body, s_host->settings->speed_fixed
                   ? "Life speed: set by growth_speed in tuning.txt on the card. Take that "
                     "line out to choose it here."
                   : "Life speed: a whole life takes about 12 weeks on \"slow\", 6 on "
                     "\"medium\" and 3 on \"fast\". Changing it keeps the pet's age.",
         COLOR_DIM);
    if (s_host->can_raise) {
        para(body, "Wrist raise: on battery, \"wakes\" lights the screen for a few seconds when "
                   "it is turned toward you. \"off\" leaves it dark until touched or the side "
                   "button is pressed, which saves the most battery on a wrist.", COLOR_DIM);
    }
    if (!s_host->screen_managed) {
        para(body, "Touch: \"off\" stops a touch from lighting a dark screen, for a pocket, "
                   "where cloth and bumps keep waking it. The side button still lights it, "
                   "and once lit it works as usual.", COLOR_DIM);
    }
    if (s_host->can_share_card) {
        para(body, "Card: \"computer\" lends the memory card to a computer over the USB "
                   "cable, as a drive. Eject it there, then switch back to \"pet\"; "
                   "the device restarts.", COLOR_DIM);
    }
    if (s_host->can_dev_log) {
        para(body, s_host->dev_log_about
                       ? s_host->dev_log_about
                       : "Dev log: \"names\" writes the name of each network and device "
                         "to the log on the card, for tuning. Keep that file private. "
                         "\"off\" logs kinds of food only.", COLOR_DIM);
    }
    gap(body, 8);
    button(body, s_body_w, 44, "Start over...", reset_cb, NULL);
    gap(body, 8);
    /* Which build this is, for when the owner asks whether there is a newer one. */
    if (s_host->version && s_host->version[0]) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Baandwidth Farms %.20s", s_host->version);
        lv_obj_t *l = para(body, buf, COLOR_DIM);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        gap(body, 8);
    }
}

/* ---- Starting over, and retiring ---------------------------------------- */

#define RESET_HOLD_MS 3000

static lv_timer_t *s_hold_timer;
static lv_obj_t *s_hold_fill;
static uint32_t s_hold_start;
static void (*s_hold_done)(void);   /* what a full hold does */

static void hold_stop(void)
{
    if (s_hold_timer) {
        lv_timer_delete(s_hold_timer);
        s_hold_timer = NULL;
    }
}

static void move_give(void);

static void hold_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    uint32_t held = lv_tick_elaps(s_hold_start);
    if (held < RESET_HOLD_MS) {
        lv_obj_set_width(s_hold_fill, (int32_t)(s_body_w * held / RESET_HOLD_MS));
        return;
    }
    hold_stop();
    void (*done)(void) = s_hold_done;
    if (done) {
        done();
    }
    /* Sending the pet opens the waiting page; everything else held for is over, and goes home. */
    if (done != move_give) {
        pet_pages_open(PAGE_HOME);
    }
}

/* Only a finger kept on the button for the whole time counts; letting go early undoes it. */
static void hold_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        hold_stop();
        s_hold_start = lv_tick_get();
        s_hold_timer = lv_timer_create(hold_tick, 30, NULL);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DELETE) {
        hold_stop();
        if (code != LV_EVENT_DELETE) {
            lv_obj_set_width(s_hold_fill, 0);
        }
    }
}

/* The button that must be held until it fills. */
static void hold_button(lv_obj_t *body, const char *label, uint32_t fill, void (*done)(void))
{
    s_hold_done = done;
    lv_obj_t *b = filled(body, s_body_w, 64, COLOR_PANEL, 12);
    lv_obj_set_style_clip_corner(b, true, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);     /* a wobbling finger must not swipe the page away */
    s_hold_fill = filled(b, 0, 64, fill, 0);
    lv_obj_align(s_hold_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_center(text(b, label, COLOR_TEXT, true));
    lv_obj_add_event_cb(b, hold_cb, LV_EVENT_ALL, NULL);
}

static void build_reset(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Start over", false);
    const pet_t *pet = s_host->pet;
    char buf[96];
    snprintf(buf, sizeof(buf), "This says goodbye to %s for good.", pet->name[0] ? pet->name : "this pet");
    para(body, buf, COLOR_WARN);
    para(body, "Its journal, badges, places, food-dex and diet history go with it, and so does "
               "the farm. A new egg takes its place. Your settings stay as they are.", COLOR_TEXT);
    gap(body, 12);
    hold_button(body, "Hold to start over", COLOR_WARN, s_host->start_over);
    gap(body, 8);
    para(body, "Keep your finger on the button until it fills. Swipe right to keep the pet.",
         COLOR_DIM);
}

static void build_retire(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Retire", false);
    const pet_t *pet = s_host->pet;
    char buf[96];
    if (!pet_can_retire(pet)) {
        para(body, "This pet has not finished its adult life yet.", COLOR_TEXT);
        return;
    }
    snprintf(buf, sizeof(buf), "%s has lived a full life.", pet->name[0] ? pet->name : "This pet");
    para(body, buf, COLOR_ACCENT);
    para(body, "It moves to the farm and leaves an egg that takes after it. Your badges, places "
               "and settings stay; the egg starts its own diet and food-dex.", COLOR_TEXT);
    gap(body, 12);
    hold_button(body, "Hold to retire", COLOR_ACCENT, s_host->retire);
    gap(body, 8);
    para(body, "Keep your finger on the button until it fills. Swipe right to wait a while longer.",
         COLOR_DIM);
}

/* ---- Moving between devices --------------------------------------------- */

static void move_give(void)
{
    pet_app_exchange_open(true);
    pet_pages_open(PAGE_MOVE);
}

static void move_visit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_app_exchange_open(false);
    pet_pages_open(PAGE_MOVE);
}

static void move_again_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_app_exchange_close();
    pet_pages_open(PAGE_MOVE);
}

static void move_egg(void)
{
    pet_app_new_egg();
    pet_pages_open(PAGE_HOME);
}

static void build_move(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Move or visit", false);
    const pet_t *pet = s_host->pet;
    char buf[96];
    uint32_t code = 0;

    switch (pet_app_exchange_step(&code)) {
    case PET_EXCHANGE_WAITING:
        para(body, "Waiting for the other device.", COLOR_ACCENT);
        para(body, "On the phone, open the exchange and choose this watch. Keep the two close together.",
             COLOR_TEXT);
        para(body, "Swipe right to stop waiting.", COLOR_DIM);
        return;
    case PET_EXCHANGE_CODE:
        para(body, "Type this code on the other device:", COLOR_TEXT);
        gap(body, 8);
        snprintf(buf, sizeof(buf), "%03lu %03lu", (unsigned long)(code / 1000), (unsigned long)(code % 1000));
        lv_obj_set_style_text_font(para(body, buf, COLOR_ACCENT), &lv_font_montserrat_28, 0);
        gap(body, 8);
        para(body, "It proves the other device is in your hand. If you did not start this, swipe right.",
             COLOR_DIM);
        return;
    case PET_EXCHANGE_MOVING:
        para(body, "Under way. Keep the two close together.", COLOR_ACCENT);
        return;
    case PET_EXCHANGE_ENDED:
        para(body, pet_app_exchange_result(), COLOR_ACCENT);
        gap(body, 8);
        button(body, s_body_w, 44, "Again", move_again_cb, NULL);
        para(body, "Swipe right to go back.", COLOR_DIM);
        return;
    default:
        break;
    }

    if (pet->away) {
        para(body, "The pasture is empty.", COLOR_ACCENT);
        para(body, "To bring a pet here, send it from the other device while this watch waits. The same "
                   "meeting brings the two farms level.", COLOR_TEXT);
        gap(body, 6);
        button(body, s_body_w, 44, "Wait for the other device", move_visit_cb, NULL);
        gap(body, 12);
        para(body, "Or start afresh here with an egg of the first generation.", COLOR_TEXT);
        hold_button(body, "Hold for a new egg", COLOR_ACCENT, move_egg);
        return;
    }
    para(body, "A pet lives on one device at a time. The farm and badges are the same on all of them "
               "once they have met.", COLOR_TEXT);
    gap(body, 6);
    button(body, s_body_w, 44, "Visit: farms level", move_visit_cb, NULL);
    para(body, "The pet stays here. Use this to receive a pet too, if this one is an unhatched egg.", COLOR_DIM);
    gap(body, 12);
    snprintf(buf, sizeof(buf), "Hold to send %s", pet->name[0] ? pet->name : "the pet");
    hold_button(body, buf, COLOR_WARN, move_give);
    para(body, "It leaves when the other device has it safe, and this pasture is then empty. The other "
               "device does the calling: this watch only waits.", COLOR_DIM);
}

/* For an empty pasture, the pages that are all about the pet. */
static void build_empty(void)
{
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Empty pasture", false);
    para(body, "No pet lives here just now, so there is nothing to show on this page.", COLOR_TEXT);
    para(body, "Hold the empty field, or open Pet in the menu, to bring one here.", COLOR_DIM);
}

/* ---- Naming the pet ----------------------------------------------------- */

static lv_obj_t *s_name_box;

static void name_done_cb(lv_event_t *e)
{
    bool place = s_name_habitat >= 0 && s_name_habitat < HABITAT_MAX;
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        if (place) {
            pet_app_name_place(s_name_habitat, lv_textarea_get_text(s_name_box));
        } else {
            pet_app_rename(lv_textarea_get_text(s_name_box));
        }
    }
    pet_pages_open(place ? PAGE_PLACES : PAGE_INFO);
}

static void build_name(void)
{
    bool place = s_name_habitat >= 0 && s_name_habitat < HABITAT_MAX;
    page_begin(place ? LV_SYMBOL_LEFT "  Name this place" : LV_SYMBOL_LEFT "  Name", false);

    s_name_box = lv_textarea_create(s_page);
    lv_textarea_set_one_line(s_name_box, true);
    lv_textarea_set_max_length(s_name_box, PET_NAME_MAX - 1);
    lv_textarea_set_text(s_name_box, place ? s_host->pet->places.habitat[s_name_habitat].name
                                           : s_host->pet->name);
    lv_obj_set_width(s_name_box, s_body_w - 60);
    lv_obj_align(s_name_box, LV_ALIGN_TOP_MID, 0, PAGE_TOP + 10);
    lv_obj_set_style_bg_color(s_name_box, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_text_color(s_name_box, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_color(s_name_box, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(s_name_box, lv_color_hex(COLOR_ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_state(s_name_box, LV_STATE_FOCUSED);

    lv_obj_t *kb = lv_keyboard_create(s_page);
    lv_obj_set_size(kb, s_w - 16, s_h * 48 / 100);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -PAGE_BOTTOM);
    lv_obj_set_style_bg_color(kb, lv_color_black(), 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x232830), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_PRESS), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x232830), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(kb, font(false), LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(kb, 4, 0);
    lv_keyboard_set_textarea(kb, s_name_box);
    lv_obj_add_event_cb(kb, name_done_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, name_done_cb, LV_EVENT_CANCEL, NULL);
}

/* ---- Navigation --------------------------------------------------------- */

pet_page_t pet_pages_current(void)
{
    return s_current;
}

void pet_pages_open(pet_page_t page)
{
    /* Settings and the exchange page are opened again over themselves, to draw what has changed. */
    if (!s_screen || (page == s_current && page != PAGE_SETTINGS && page != PAGE_MOVE)) {
        return;
    }
    if (s_page) {
        /* Often called from a button on the very page being closed. */
        lv_obj_delete_async(s_page);
        s_page = NULL;
    }
    memset(s_value, 0, sizeof(s_value));
    /* Leaving the exchange page ends the exchange: the watch waits only while it is open. */
    if (s_current == PAGE_MOVE && page != PAGE_MOVE) {
        pet_app_exchange_close();
    }
    s_current = page;
    if (s_host->pet->away && (page == PAGE_DIET || page == PAGE_JOURNAL || page == PAGE_RECENT ||
                              page == PAGE_DEX || page == PAGE_RETIRE)) {
        build_empty();
        return;
    }
    switch (page) {
    case PAGE_DIET:     build_diet(); break;
    case PAGE_JOURNAL:  build_journal(); break;
    case PAGE_RECENT:   build_recent(); break;
    case PAGE_MENU:     build_menu(); break;
    case PAGE_DEX:      build_dex(); break;
    case PAGE_BADGES:   build_badges(); break;
    case PAGE_FARM:     build_farm(); break;
    case PAGE_INFO:     build_info(); break;
    case PAGE_SETTINGS: build_settings(); break;
    case PAGE_NAME:     build_name(); break;
    case PAGE_RESET:    build_reset(); break;
    case PAGE_RETIRE:   build_retire(); break;
    case PAGE_MOVE:     build_move(); break;
    case PAGE_PLACES:   build_places(); break;
    default:            s_current = PAGE_HOME; break;
    }
}

static void gesture_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (s_current == PAGE_HOME) {
        switch (dir) {
        case LV_DIR_LEFT:   pet_pages_open(PAGE_DIET); break;
        case LV_DIR_RIGHT:  pet_pages_open(PAGE_JOURNAL); break;
        case LV_DIR_TOP:    pet_pages_open(PAGE_MENU); break;
        case LV_DIR_BOTTOM: pet_pages_open(PAGE_RECENT); break;
        default:            return;
        }
    } else if (dir == back_swipe(s_current)) {
        pet_pages_open(parent_of(s_current));
    } else {
        return;
    }
    /* The rest of this touch is not a tap on whatever is underneath. */
    lv_indev_wait_release(indev);
}

void pet_pages_open_card(void)
{
    s_info_parent = PAGE_HOME;
    pet_pages_open(PAGE_INFO);
}

void pet_pages_init(lv_obj_t *screen, const pet_pages_host_t *host)
{
    s_host = host;
    if (host->pet_only) {
        return;     /* no screen of ours to put a page on, and no swipes */
    }
    s_screen = screen;
    s_w = lv_display_get_horizontal_resolution(NULL);
    s_h = lv_display_get_vertical_resolution(NULL);
    s_body_w = s_w - 2 * PAGE_SIDE;
    lv_obj_add_event_cb(screen, gesture_cb, LV_EVENT_GESTURE, NULL);
}
