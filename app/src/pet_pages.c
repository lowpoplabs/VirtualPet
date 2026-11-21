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

static lv_obj_t *text(lv_obj_t *parent, const char *str, uint32_t color, bool big)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, str);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, big ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
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
    case PAGE_NAME:     return s_name_habitat >= 0 ? PAGE_PLACES : PAGE_SETTINGS;
    case PAGE_RESET:    return PAGE_SETTINGS;
    case PAGE_RETIRE:   return PAGE_FARM;
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
 * Holding the Settings title opens the preview of every form. Nothing on
 * screen says so: the forms are meant to be a surprise.
 */
static void title_hold_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_current == PAGE_SETTINGS && s_host->preview) {
        pet_pages_open(PAGE_HOME);
        s_host->preview();
    }
}

static lv_obj_t *page_begin(const char *title, bool scrolls)
{
    s_page = filled(s_screen, s_w, s_h, 0x000000, 0);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);     /* taps stop here, not on the pet below */

    lv_obj_t *t = text(s_page, title, COLOR_TEXT, true);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
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

static const pet_day_t *day_record(const pet_t *pet, uint32_t day)
{
    if (pet->today.day == day) {
        return &pet->today;
    }
    for (int i = 0; i < PET_HISTORY_DAYS; i++) {
        if (pet->history[i].day == day) {
            return &pet->history[i];
        }
    }
    return NULL;
}

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
    uint32_t today = pet->today.day, biggest = 1, week[GROUP_COUNT] = {0};
    for (int i = 0; i < 7; i++) {
        const pet_day_t *d = day_record(pet, today - 6 + (uint32_t)i);
        uint32_t total = day_total(d);
        if (total > biggest) {
            biggest = total;
        }
        for (int g = 0; d && g < GROUP_COUNT; g++) {
            week[g] += d->group_pts[g];
        }
    }
    lv_obj_t *chart = row(body, CHART_H + 24);
    int32_t col_w = s_body_w / 7;
    for (int i = 0; i < 7; i++) {
        uint32_t day = today - 6 + (uint32_t)i;
        const pet_day_t *d = day_record(pet, day);
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
        lv_obj_set_pos(l, x + 8, CHART_H + 6);
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
        lv_obj_set_pos(text(item, pet_group_name((pet_group_t)g), COLOR_TEXT, false), 16, 0);
    }
    int len = 0;
    buf[0] = '\0';
    for (int g = 0; g < GROUP_OTHER; g++) {
        if (week[g] == 0 && len < (int)sizeof(buf)) {
            len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%s%s", len ? ", " : "none this week: ",
                            pet_group_name((pet_group_t)g));
        }
    }
    if (len) {
        para(body, buf, COLOR_WARN);
    }

    /* Growing up. */
    gap(body, 6);
    int days_done, days_needed, meals_done, meals_needed;
    pet_growth_counts(pet, &days_done, &days_needed, &meals_done, &meals_needed);
    snprintf(buf, sizeof(buf), "%s%s%s", pet_stage_name(pet->stage),
             pet->form != FORM_NONE ? " " : "", pet_form_name(pet->form));
    text(body, buf, COLOR_TEXT, true);
    if (pet->stage == STAGE_ADULT && pet->retire_ready) {
        para(body, "A full life lived.", COLOR_ACCENT);
    } else {
        if (days_needed > 0) {
            snprintf(buf, sizeof(buf), "day %d of %d", days_done, days_needed);
            para(body, buf, COLOR_DIM);
            bar(body, days_done, days_needed);
        }
        snprintf(buf, sizeof(buf), "meals %d of %d", meals_done, meals_needed);
        para(body, buf, COLOR_DIM);
        bar(body, meals_done, meals_needed);
    }
    if (pet->stage < STAGE_ADULT) {
        snprintf(buf, sizeof(buf), "drifting toward %s", pet_form_name(pet_form_leaning(pet)));
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
        lv_obj_t *line = row(body, 24);
        snprintf(buf, sizeof(buf), "%02d:%02d", (int)(r->time % 86400 / 3600),
                 (int)(r->time % 3600 / 60));
        lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_LEFT_MID, 0, 0);

        int len = snprintf(buf, sizeof(buf), "%s", food_label(r->best));
        if (r->foods > 1) {
            len += snprintf(buf + len, sizeof(buf) - (size_t)len, "  +%d", r->foods - 1);
        }
        if ((r->new_food || r->craving) && len < (int)sizeof(buf)) {
            snprintf(buf + len, sizeof(buf) - (size_t)len, "  %s", r->new_food ? "new!" : "craving");
        }
        lv_obj_align(text(line, buf, r->new_food ? COLOR_ACCENT : COLOR_TEXT, false),
                     LV_ALIGN_LEFT_MID, 56, 0);

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
    const pet_t *pet = s_host->pet;
    int n = 0;
    buf[0] = '\0';
    switch (page) {
    case PAGE_DEX:
        for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
            n += pet->tasted[i] > 0;
        }
        snprintf(buf, cap, "%d of %d tasted", n, FOOD_TYPE_COUNT);
        break;
    case PAGE_BADGES:
        for (int i = 0; i < BADGE_COUNT; i++) {
            n += (pet->badges >> i) & 1;
        }
        for (int k = 0; k < PLACE_KIND_COUNT; k++) {
            n += pet->places.kind[k].count > 0;
        }
        n += pet->places.offgrid_tier > 0;
        snprintf(buf, cap, "%d of %d earned", n, BADGE_COUNT + PLACE_KIND_COUNT + 1);
        if (pet->new_badges) {
            *color = COLOR_WARN;    /* one it has not been shown yet */
        }
        break;
    case PAGE_PLACES:
        n = places_count(&pet->places);
        snprintf(buf, cap, n == 1 ? "%d habitat" : "%d habitats", n);
        break;
    case PAGE_FARM:
        n = pet->farm.count + pet->farm.moved_on;
        if (pet_can_retire(pet)) {
            snprintf(buf, cap, "ready to retire");
            *color = COLOR_WARN;
        } else if (n == 0) {
            snprintf(buf, cap, "empty");
        } else {
            snprintf(buf, cap, "%d retired", n);
        }
        break;
    case PAGE_INFO:
        snprintf(buf, cap, "%s", pet->name[0] ? pet->name
                                 : (pet->form != FORM_NONE ? pet_form_name(pet->form)
                                                           : pet_stage_name(pet->stage)));
        break;
    default:
        if (s_host->settings->dev_log) {
            snprintf(buf, cap, "dev log on");
            *color = COLOR_WARN;
        } else if (s_host->settings->paused) {
            snprintf(buf, cap, "pet paused");
            *color = COLOR_WARN;
        }
        break;
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
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 14, 12);
        menu_icon(icon, ITEMS[i].page);

        char status[32];
        uint32_t color = COLOR_DIM;
        menu_status(ITEMS[i].page, status, sizeof(status), &color);
        lv_obj_align(text(tile, ITEMS[i].label, COLOR_TEXT, true), LV_ALIGN_BOTTOM_LEFT, 14,
                     status[0] ? -30 : -14);
        if (status[0]) {
            lv_obj_align(text(tile, status, color, false), LV_ALIGN_BOTTOM_LEFT, 14, -10);
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
        lv_obj_t *line = row(body, 24);
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
    snprintf(buf, sizeof(buf), "%d of %d earned", earned, BADGE_COUNT + PLACE_KIND_COUNT + 1);
    para(body, buf, COLOR_DIM);

    /* Earned ones first. */
    for (int pass = 0; pass < 2; pass++) {
        for (int b = 0; b < BADGE_COUNT; b++) {
            bool has = pet_has_badge(pet, (pet_badge_t)b);
            if (has != (pass == 0)) {
                continue;
            }
            lv_obj_t *line = row(body, 42);
            lv_obj_t *mark = filled(line, 30, 30, has ? COLOR_ACCENT : COLOR_PANEL, 15);
            lv_obj_align(mark, LV_ALIGN_LEFT_MID, 0, 0);
            if (has) {
                lv_obj_center(text(mark, LV_SYMBOL_OK, 0x10141A, false));
            }
            lv_obj_align(text(line, pet_badge_name((pet_badge_t)b), has ? COLOR_TEXT : COLOR_DIM,
                              false),
                         LV_ALIGN_TOP_LEFT, 42, 2);
            lv_obj_align(text(line, pet_badge_how((pet_badge_t)b), COLOR_DIM, false),
                         LV_ALIGN_TOP_LEFT, 42, 21);
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
        lv_obj_t *line = row(body, 42);
        lv_obj_t *mark = filled(line, 30, 30, TIER_COLOR[tier], 15);
        lv_obj_align(mark, LV_ALIGN_LEFT_MID, 0, 0);
        if (tier) {
            lv_obj_center(text(mark, LV_SYMBOL_OK, 0x10141A, false));
        }
        lv_obj_align(text(line, grid ? "Off the grid" : place_kind_name((place_kind_t)k),
                          tier ? COLOR_TEXT : COLOR_DIM, false),
                     LV_ALIGN_TOP_LEFT, 42, 2);
        if (!tier) {
            snprintf(buf, sizeof(buf), "%s", grid ? "an hour with no signal at all"
                                                  : place_kind_how((place_kind_t)k));
        } else if (grid) {
            snprintf(buf, sizeof(buf), "%s", place_tier_name(tier));
        } else {
            snprintf(buf, sizeof(buf), "%s, %d so far", place_tier_name(tier), count);
        }
        lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_TOP_LEFT, 42, 21);
    }
    gap(body, 4);
    para(body, "Silver at 3, gold at 10. Only the kind of place is kept, never its name.", COLOR_DIM);
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
    char buf[64], label[HABITAT_NAME_MAX + 4];
    int home = places_home(pl);

    if (pl->here >= 0) {
        places_label(pl, pl->here, label, sizeof(label));
        snprintf(buf, sizeof(buf), "Here now: %s", label);
        para(body, buf, COLOR_ACCENT);
    } else if (pl->cand_prints) {
        para(body, "Somewhere it does not know. Stay a while and it will remember.", COLOR_WARN);
    } else {
        para(body, "On the move.", COLOR_DIM);
    }
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
            lv_obj_t *line = row(body, 46);
            places_label(pl, i, label, sizeof(label));
            if (i == home && h->name[0]) {
                snprintf(buf, sizeof(buf), "%s (home)", label);
            } else {
                snprintf(buf, sizeof(buf), "%s", label);
            }
            lv_obj_align(text(line, buf, i == pl->here ? COLOR_ACCENT : COLOR_TEXT, false),
                         LV_ALIGN_TOP_LEFT, 0, 3);
            if (h->minutes < 90) {
                snprintf(buf, sizeof(buf), "%d visit%s, %d min", h->visits, h->visits == 1 ? "" : "s",
                         (int)h->minutes);
            } else {
                snprintf(buf, sizeof(buf), "%d visit%s, %d h", h->visits, h->visits == 1 ? "" : "s",
                         (int)((h->minutes + 30) / 60));
            }
            lv_obj_align(text(line, buf, COLOR_DIM, false), LV_ALIGN_TOP_LEFT, 0, 23);
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
    static const uint32_t BODY[FORM_COUNT] = {
        [FORM_NONE] = 0x8CF2B0,     [FORM_GRAZER] = 0xC6E68C,    [FORM_HUNTER] = 0xA9BEF7,
        [FORM_HOMEBODY] = 0xBF7436, [FORM_ROAMER] = 0xFFC857,    [FORM_SCAVENGER] = 0xE8674C,
        [FORM_SINGER] = 0xC79BF2,   [FORM_RACER] = 0xF48FB1,     [FORM_GLOWBUG] = 0xA9A2FF,
        [FORM_PRISM] = 0xFF9CB8,
    };
    uint32_t c = BODY[form < FORM_COUNT ? form : FORM_NONE];
    int32_t body_x = 8, body_w = 40;
    switch (form) {
    case FORM_HOMEBODY:     /* horn-antennae and a fringe */
        dot(box56, 6, 14, 0xE9DFCB, 6, 10);
        dot(box56, 6, 14, 0xE9DFCB, 44, 10);
        dot(box56, 9, 9, 0xFFE27A, 3, 4);
        dot(box56, 9, 9, 0xFFE27A, 44, 4);
        break;
    case FORM_ROAMER:       /* wings */
        dot(box56, 26, 32, 0xF2811D, 0, 14);
        dot(box56, 26, 32, 0xF2811D, 30, 14);
        slab(box56, 3, 12, c, 21, 6);
        slab(box56, 3, 12, c, 32, 6);
        dot(box56, 8, 8, 0xFFE27A, 18, 1);
        dot(box56, 8, 8, 0xFFE27A, 30, 1);
        body_x = 15;
        body_w = 26;
        break;
    case FORM_SCAVENGER:    /* blue pincers */
        dot(box56, 16, 18, 0x3F8FE3, 0, 12);
        dot(box56, 16, 18, 0x3F8FE3, 40, 12);
        slab(box56, 3, 12, c, 20, 8);
        slab(box56, 3, 9, c, 33, 11);
        dot(box56, 8, 8, 0xFFE27A, 17, 3);
        dot(box56, 8, 8, 0xFFE27A, 30, 6);
        break;
    case FORM_SINGER:       /* huge ears and a tail */
        dot(box56, 18, 30, c, 6, 0);
        dot(box56, 18, 30, c, 32, 0);
        dot(box56, 8, 16, 0xF6BFE4, 11, 6);
        dot(box56, 8, 16, 0xF6BFE4, 37, 6);
        dot(box56, 14, 14, 0xF4EAFF, 44, 36);
        break;
    case FORM_RACER:        /* long ears, one striped */
        dot(box56, 12, 32, c, 15, 0);
        dot(box56, 12, 32, c, 30, 0);
        slab(box56, 12, 3, 0xFFFFFF, 30, 9);
        slab(box56, 12, 3, 0xFFFFFF, 30, 15);
        dot(box56, 12, 12, 0xFFE6EE, 44, 40);
        break;
    case FORM_GLOWBUG:      /* ear fins and arms */
        dot(box56, 18, 14, 0xC2BDFF, 0, 14);
        dot(box56, 18, 14, 0xC2BDFF, 38, 14);
        for (int i = 0; i < 4; i++) {
            slab(box56, 7, 16, c, 12 + i * 9, 40);
        }
        break;
    case FORM_PRISM:        /* a horn and a rainbow mane */
        slab(box56, 8, 20, 0xFFD36B, 24, 0);
        dot(box56, 12, 12, 0xFFB25B, 42, 18);
        dot(box56, 12, 12, 0x7BE38F, 45, 28);
        dot(box56, 12, 12, 0x7DB8F2, 42, 38);
        break;
    default:
        break;
    }
    dot(box56, body_w, 36, c, body_x, 18);
    if (form == FORM_HOMEBODY) {
        slab(box56, 26, 9, 0xE9AE6A, 15, 15);
        dot(box56, 20, 11, 0xF3D9B4, 18, 41);
    }
    if (form == FORM_GLOWBUG) {
        dot(box56, 6, 6, 0x9FFFE8, 25, 20);
    }
    int32_t eye = form == FORM_ROAMER ? 4 : 0;
    dot(box56, 9, 12, 0xFFFFFF, 17 + eye / 2, 26);
    dot(box56, 9, 12, 0xFFFFFF, 30 - eye / 2, 26);
    dot(box56, 4, 6, 0x10131A, 19 + eye / 2, 30);
    dot(box56, 4, 6, 0x10131A, 32 - eye / 2, 30);
}

/* The three lines under the field, about the resident that was tapped. */
static void farm_show(int which)
{
    const pet_farm_t *farm = &s_host->pet->farm;
    if (which < 0 || which >= farm->count || !s_farm_line[0]) {
        return;
    }
    const pet_resident_t *r = &farm->resident[which];
    for (int i = 0; i < farm->count; i++) {
        if (s_farm_tile[i]) {
            lv_obj_set_style_bg_color(s_farm_tile[i], lv_color_hex(i == which ? FARM_PICKED : FARM_FIELD), 0);
        }
    }
    char buf[80];
    lv_label_set_text(s_farm_line[0], r->name[0] ? r->name : "(never named)");
    snprintf(buf, sizeof(buf), "%s, generation %d", pet_form_name((pet_form_t)r->form), r->generation);
    lv_label_set_text(s_farm_line[1], buf);
    int n = snprintf(buf, sizeof(buf), "lived %d days, %lu meals", r->days, (unsigned long)r->meals);
    for (int t = 0; t < TRAIT_COUNT && n > 0 && n < (int)sizeof(buf); t++) {
        if ((r->traits >> t) & 1) {
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, ", %s", pet_trait_name((pet_trait_t)t));
        }
    }
    lv_label_set_text(s_farm_line[2], buf);
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

    const char *who = pet->name[0] ? pet->name : "this pet";
    if (pet_can_retire(pet)) {
        snprintf(buf, sizeof(buf), "Retire %s...", who);
        button(body, s_body_w, 44, buf, retire_open_cb, NULL);
    } else if (pet->stage == STAGE_ADULT) {
        int days, days_needed, meals, meals_needed;
        pet_growth_counts(pet, &days, &days_needed, &meals, &meals_needed);
        snprintf(buf, sizeof(buf), "%s can retire after a full adult life: day %d of %d, meal %d of %d.",
                 pet->name[0] ? pet->name : "This pet", days, days_needed, meals, meals_needed);
        para(body, buf, COLOR_DIM);
    } else {
        snprintf(buf, sizeof(buf), "Generation %d is still growing up.", pet->generation);
        para(body, buf, COLOR_DIM);
    }
    gap(body, 8);
}

/* ---- Pet card ----------------------------------------------------------- */

static void fact(lv_obj_t *body, const char *what, const char *value)
{
    lv_obj_t *line = row(body, 24);
    lv_obj_align(text(line, what, COLOR_DIM, false), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(text(line, value, COLOR_TEXT, false), LV_ALIGN_RIGHT_MID, 0, 0);
}

static void build_info(void)
{
    const pet_t *pet = s_host->pet;
    lv_obj_t *body = page_begin(LV_SYMBOL_LEFT "  Pet", true);
    char buf[64];

    text(body, pet->name[0] ? pet->name : "(no name yet)", pet->name[0] ? COLOR_ACCENT : COLOR_DIM,
         true);
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

    snprintf(buf, sizeof(buf), "%s%s%s", pet_stage_name(pet->stage),
             pet->form != FORM_NONE ? " " : "", pet_form_name(pet->form));
    fact(body, "stage", buf);
    snprintf(buf, sizeof(buf), "%d", pet->generation);
    fact(body, "generation", buf);

    float speed = pet->tune.growth_speed > 0 ? pet->tune.growth_speed : 1;
    int days = (int)((pet->now - pet->born) * speed / 86400.0f);
    snprintf(buf, sizeof(buf), days == 1 ? "%d day" : "%d days", days);
    fact(body, "age", buf);

    int len = 0;
    buf[0] = '\0';
    for (int t = 0; t < TRAIT_COUNT; t++) {
        if (pet_has_trait(pet, (pet_trait_t)t) && len < (int)sizeof(buf)) {
            len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%s%s", len ? ", " : "",
                            pet_trait_name((pet_trait_t)t));
        }
    }
    fact(body, "traits", buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)pet->meals);
    fact(body, "meals", buf);

    float g[GROUP_COUNT];
    int top = 0;
    pet_group_points(pet, g);
    for (int i = 1; i < GROUP_COUNT; i++) {
        if (g[i] > g[top]) {
            top = i;
        }
    }
    fact(body, "eats mostly", g[top] > 0 ? pet_group_name((pet_group_t)top) : "nothing yet");

    /* Counted the way the Badges screen counts them, place badges included. */
    int earned = 0;
    for (int b = 0; b < BADGE_COUNT; b++) {
        earned += pet_has_badge(pet, (pet_badge_t)b);
    }
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        earned += pet->places.kind[k].count > 0;
    }
    earned += pet->places.offgrid_tier > 0;
    snprintf(buf, sizeof(buf), "%d of %d", earned, BADGE_COUNT + PLACE_KIND_COUNT + 1);
    fact(body, "badges", buf);
    if (pet->is_static) {
        gap(body, 4);
        para(body, "Underfed: it needs a good few meals to recover.", COLOR_WARN);
    }
}

/* ---- Settings ----------------------------------------------------------- */

static lv_obj_t *s_value[SET_COUNT];
static lv_obj_t *s_name_value;

static const char *const SETTING_LABEL[SET_COUNT] = {
    [SET_BRIGHTNESS] = "Brightness", [SET_SOUND] = "Sound", [SET_VOLUME] = "Volume",
    [SET_CARD] = "Card", [SET_DEV_LOG] = "Dev log",
    [SET_QUIET_START] = "Quiet from", [SET_QUIET_END] = "Quiet until",
    [SET_HOUR] = "Clock hour", [SET_MINUTE] = "Clock minute",
    [SET_CARRY] = "Screen", [SET_PAUSE] = "Pet", [SET_LIFE_SPEED] = "Life speed",
    [SET_RAISE] = "Wrist raise",
};

static void setting_text(pet_setting_id_t id, char *buf, size_t cap)
{
    const pet_settings_t *s = s_host->settings;
    uint32_t now = s_host->now();
    switch (id) {
    case SET_BRIGHTNESS:  snprintf(buf, cap, "%d%%", s->brightness); break;
    case SET_SOUND:       snprintf(buf, cap, "%s", s->sound == 0 ? "off" : (s->sound == 1 ? "rare" : "all")); break;
    case SET_VOLUME:      snprintf(buf, cap, "%d of 5", s->volume); break;
    case SET_CARD:        snprintf(buf, cap, "%s", s->card_shared ? "computer" : "pet"); break;
    case SET_DEV_LOG:     snprintf(buf, cap, "%s", s->dev_log ? "names" : "off"); break;
    case SET_QUIET_START: snprintf(buf, cap, "%02d:00", s->quiet_start); break;
    case SET_QUIET_END:   snprintf(buf, cap, "%02d:00", s->quiet_end); break;
    case SET_HOUR:        snprintf(buf, cap, "%02d", (int)(now % 86400 / 3600)); break;
    case SET_MINUTE:      snprintf(buf, cap, "%02d", (int)(now % 3600 / 60)); break;
    case SET_CARRY:       snprintf(buf, cap, "%s", s->carry ? "sleeps" : "stays on"); break;
    case SET_PAUSE:       snprintf(buf, cap, "%s", s->paused ? "paused" : "awake"); break;
    case SET_RAISE:       snprintf(buf, cap, "%s", s->raise_off ? "off" : "wakes"); break;
    case SET_LIFE_SPEED:
        snprintf(buf, cap, "%s", s->speed_fixed ? "by file"
                                 : (s->life_speed == LIFE_FAST ? "fast"
                                    : (s->life_speed == LIFE_MEDIUM ? "medium" : "slow")));
        break;
    default:              buf[0] = '\0'; break;
    }
}

static void settings_refresh(void)
{
    char buf[24];
    for (int i = 0; i < SET_COUNT; i++) {
        if (s_value[i]) {
            setting_text((pet_setting_id_t)i, buf, sizeof(buf));
            lv_label_set_text(s_value[i], buf);
        }
    }
}

static void setting_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    s_host->change((pet_setting_id_t)(code / 2), (code & 1) ? 1 : -1);
    settings_refresh();
}

static void name_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_name_habitat = -1;
    pet_pages_open(PAGE_NAME);
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

    lv_obj_t *line = setting_row(body, "Name");
    const pet_t *pet = s_host->pet;
    s_name_value = text(line, pet->name[0] ? pet->name : "none", COLOR_ACCENT, false);
    lv_obj_align(s_name_value, LV_ALIGN_RIGHT_MID, -108, 0);
    lv_obj_align(button(line, 96, 38, LV_SYMBOL_EDIT, name_cb, NULL), LV_ALIGN_RIGHT_MID, 0, 0);

    for (int i = 0; i < SET_COUNT; i++) {
        if ((i == SET_HOUR || i == SET_MINUTE) && !s_host->can_set_clock) {
            continue;
        }
        if (i == SET_CARD && !s_host->can_share_card) {
            continue;
        }
        if (i == SET_DEV_LOG && !s_host->can_dev_log) {
            continue;
        }
        line = setting_row(body, SETTING_LABEL[i]);
        s_value[i] = text(line, "", COLOR_ACCENT, false);
        lv_obj_align(s_value[i], LV_ALIGN_RIGHT_MID, -108, 0);
        lv_obj_align(button(line, 44, 38, LV_SYMBOL_MINUS, setting_cb, (void *)(intptr_t)(i * 2)),
                     LV_ALIGN_RIGHT_MID, -52, 0);
        lv_obj_align(button(line, 44, 38, LV_SYMBOL_PLUS, setting_cb, (void *)(intptr_t)(i * 2 + 1)),
                     LV_ALIGN_RIGHT_MID, 0, 0);
    }
    settings_refresh();
    gap(body, 4);
    para(body, "Screen: \"stays on\" for a desk, \"sleeps\" for a backpack. "
               "Pet: pause it when the device sits on a shelf; its time stands still "
               "and it stops scanning.", COLOR_DIM);
    para(body, s_host->settings->speed_fixed
                   ? "Life speed: set by growth_speed in tuning.txt on the card. Take that "
                     "line out to choose it here."
                   : "Life speed: a whole life takes about 12 weeks on \"slow\", 6 on "
                     "\"medium\" and 3 on \"fast\". Changing it keeps the pet's age.",
         COLOR_DIM);
    para(body, "Wrist raise: on battery, \"wakes\" lights the screen for a few seconds when "
               "it is turned toward you. \"off\" leaves it dark until touched or the side "
               "button is pressed, which saves the most battery on a wrist.", COLOR_DIM);
    if (s_host->can_share_card) {
        para(body, "Card: \"computer\" lends the memory card to a computer over the USB "
                   "cable, as a drive. Eject it there, then switch back to \"pet\"; "
                   "the device restarts.", COLOR_DIM);
    }
    if (s_host->can_dev_log) {
        para(body, "Dev log: \"names\" writes the name of each network and device "
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

static void hold_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    uint32_t held = lv_tick_elaps(s_hold_start);
    if (held < RESET_HOLD_MS) {
        lv_obj_set_width(s_hold_fill, (int32_t)(s_body_w * held / RESET_HOLD_MS));
        return;
    }
    hold_stop();
    if (s_hold_done) {
        s_hold_done();
    }
    pet_pages_open(PAGE_HOME);
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

/* ---- Naming the pet ----------------------------------------------------- */

static lv_obj_t *s_name_box;

static void name_done_cb(lv_event_t *e)
{
    pet_t *pet = s_host->pet;
    bool place = s_name_habitat >= 0 && s_name_habitat < HABITAT_MAX;
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        if (place) {
            snprintf(pet->places.habitat[s_name_habitat].name, HABITAT_NAME_MAX, "%s",
                     lv_textarea_get_text(s_name_box));
        } else {
            snprintf(pet->name, sizeof(pet->name), "%s", lv_textarea_get_text(s_name_box));
        }
        s_host->renamed();
    }
    pet_pages_open(place ? PAGE_PLACES : PAGE_SETTINGS);
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
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);
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
    if (!s_screen || (page == s_current && page != PAGE_SETTINGS)) {
        return;
    }
    if (s_page) {
        /* Often called from a button on the very page being closed. */
        lv_obj_delete_async(s_page);
        s_page = NULL;
    }
    memset(s_value, 0, sizeof(s_value));
    s_current = page;
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
    s_screen = screen;
    s_host = host;
    s_w = lv_display_get_horizontal_resolution(NULL);
    s_h = lv_display_get_vertical_resolution(NULL);
    s_body_w = s_w - 2 * PAGE_SIDE;
    lv_obj_add_event_cb(screen, gesture_cb, LV_EVENT_GESTURE, NULL);
}
