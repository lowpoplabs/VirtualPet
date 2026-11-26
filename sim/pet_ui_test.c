/*
 * Desktop checks for a platform that draws its own screens (pet_ui.h): the
 * pet-only screen, the notices, the words and the buttons. Runs the whole
 * app on a display that draws to nowhere. Exit code 0 = pass.
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "pet_app.h"
#include "pet_ui.h"

static int failures;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
            failures++;                                     \
        }                                                   \
    } while (0)

/* ---- A made-up device: its clock, its storage, and what it is told ------- */

static uint32_t s_ms = 1;

static uint32_t fake_tick(void) { return s_ms; }
/* Noon on day 20000, and a minute of the pet's time for each real second. */
static uint32_t fake_now(void) { return 20000u * 86400u + 12 * 3600 + s_ms / 1000 * 60; }
static uint32_t fake_random(void) { return 0x1234ABCDu; }

static int s_saves, s_setting_saves;
static bool fake_save(const void *data, size_t len)
{
    LV_UNUSED(data);
    LV_UNUSED(len);
    s_saves++;
    return true;
}
static bool fake_save_settings(const void *data, size_t len)
{
    LV_UNUSED(data);
    LV_UNUSED(len);
    s_setting_saves++;
    return true;
}

static int s_count[4];
static char s_last[4][96];

static void fake_notice(pet_notice_t what, const char *text)
{
    s_count[what]++;
    snprintf(s_last[what], sizeof(s_last[what]), "%s", text ? text : "");
}

static pet_hal_t s_hal = {
    .now_s = fake_now,
    .random = fake_random,
    .save = fake_save,
    .save_settings = fake_save_settings,
    .screen_managed = true,
    .pet_only = true,
    .notice = fake_notice,
};

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    LV_UNUSED(area);
    LV_UNUSED(px);
    lv_display_flush_ready(disp);
}

static void run_ms(uint32_t ms)
{
    for (uint32_t end = s_ms + ms; s_ms < end; s_ms += 20) {
        lv_timer_handler();
    }
}

/* Runs until a notice of this kind has come, or the time is up. */
static bool run_until(pet_notice_t what, uint32_t ms)
{
    int before = s_count[what];
    for (uint32_t end = s_ms + ms; s_ms < end && s_count[what] == before; s_ms += 20) {
        lv_timer_handler();
    }
    return s_count[what] != before;
}

static const char *line(pet_line_t which)
{
    static char buf[96];
    pet_app_line(which, buf, sizeof(buf));
    return buf;
}

int main(void)
{
    static uint8_t draw_buf[PET_AREA_SIZE * 40 * 4];
    lv_init();
    lv_tick_set_cb(fake_tick);
    lv_display_t *disp = lv_display_create(PET_AREA_SIZE, PET_AREA_SIZE);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    pet_app_start(&s_hal);
    const pet_t *pet = pet_app_pet();
    const pet_settings_t *set = pet_app_settings();

    /* The screen: the pet's square and nothing else. */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_update_layout(scr);
    CHECK(lv_obj_get_child_count(scr) == 1, "only the pet's square is on screen");
    CHECK(lv_obj_get_width(lv_obj_get_child(scr, 0)) == PET_AREA_SIZE, "and it is the pet's size");
    pet_app_open_page("diet");
    run_ms(200);
    CHECK(lv_obj_get_child_count(scr) == 1, "no page opens");

    /* Reading the pet. */
    CHECK(pet && pet->stage == STAGE_EGG, "a new pet is an egg");
    CHECK(!strcmp(line(PET_LINE_MOOD), "egg"), "and its mood line says so");
    CHECK(!strcmp(line(PET_LINE_STAGE), "egg"), "stage line");
    CHECK(!strcmp(line(PET_LINE_TILE_FARM), "empty"), "the farm is empty");
    CHECK(!strcmp(line(PET_LINE_HERE), "On the move."), "it is nowhere yet");
    CHECK(strstr(line(PET_LINE_RETIRING), "still growing up") != NULL, "an egg cannot retire");
    CHECK(pet_app_recent(NULL) == 0, "nothing eaten yet");
    CHECK(pet_app_day(pet->today.day) == &pet->today, "today's record is the day in progress");
    CHECK(pet_app_day(pet->today.day + 5) == NULL, "there is no record of a day to come");
    CHECK(run_until(PET_NOTICE_CHANGED, 2000), "the first tick says to read");

    /* A meal: the find line comes with its name, then goes; and word to read again. */
    int changed = s_count[PET_NOTICE_CHANGED];
    pet_app_feed(FOOD_WIFI_OPEN, 3.0f, "Corner Cafe Guest");
    CHECK(run_until(PET_NOTICE_FIND, 20 * 60 * 1000), "a meal is told as a find");
    CHECK(strstr(s_last[PET_NOTICE_FIND], "Corner Cafe Guest") != NULL, "with the name that was heard");
    CHECK(s_count[PET_NOTICE_CHANGED] > changed, "a meal says to read again");
    const pet_recent_t *recent = NULL;
    CHECK(pet_app_recent(&recent) == 1 && recent && recent[0].best == FOOD_WIFI_OPEN, "it is in the recent finds");
    char buf[64];
    pet_app_recent_text(0, buf, sizeof(buf));
    CHECK(strstr(buf, "new!") != NULL && !strstr(buf, "Corner"), "listed by kind, with no name");
    CHECK(run_until(PET_NOTICE_LINE_GONE, 10000), "the line goes after a few seconds");

    /* Naming. */
    int saves = s_saves;
    changed = s_count[PET_NOTICE_CHANGED];
    pet_app_rename("Biscuit");
    CHECK(!strcmp(pet->name, "Biscuit"), "the pet takes a name");
    CHECK(s_saves > saves, "and it is saved");
    CHECK(!strcmp(line(PET_LINE_TILE_PET), "Biscuit"), "its tile says the name");
    CHECK(run_until(PET_NOTICE_CHANGED, 2000) && s_count[PET_NOTICE_CHANGED] == changed + 1,
          "a new name says to read again, once");
    pet_app_rename("Bartholomew the Third");
    CHECK(strlen(pet->name) == PET_NAME_MAX - 1, "a long name is cut to fit");
    pet_app_rename("x\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9");   /* 15 bytes; the 12th is the first half of a letter */
    CHECK(strlen(pet->name) == 11 && pet->name[10] == (char)0xA9, "the cut does not leave half a letter");
    pet_app_rename("");
    CHECK(pet->name[0] == '\0', "a name can be taken away");
    CHECK(!pet_app_name_place(0, "Den"), "a place that does not exist cannot be named");

    /* Favourite food. */
    CHECK(pet_app_set_favourite(4) && pet->favourite == 4, "a favourite can be chosen");
    CHECK(!strcmp(line(PET_LINE_FAVOURITE), "Favourite: cars"), "and is named");
    CHECK(!pet_app_set_favourite(PET_FAVOURITE_MAX + 1) && pet->favourite == 4, "one that does not exist is refused");

    /* Settings. */
    CHECK(pet_app_setting_shown(SET_SOUND) && pet_app_setting_shown(SET_PAUSE), "ordinary settings are shown");
    CHECK(!pet_app_setting_shown(SET_TEXT), "no pages, so no Text size");
    CHECK(!pet_app_setting_shown(SET_CARRY) && !pet_app_setting_shown(SET_TOUCH) &&
              !pet_app_setting_shown(SET_BRIGHTNESS) && !pet_app_setting_shown(SET_RAISE) &&
              !pet_app_setting_shown(SET_HOUR) && !pet_app_setting_shown(SET_CARD) &&
              !pet_app_setting_shown(SET_DEV_LOG),
          "nor what this device cannot do");
    CHECK(!strcmp(pet_app_setting_label(SET_QUIET_START), "Quiet from"), "a setting has its name");
    pet_app_setting_text(SET_QUIET_START, buf, sizeof(buf));
    CHECK(!strcmp(buf, "22:00"), "and its value");
    int kept = s_setting_saves;
    pet_app_setting_step(SET_QUIET_START, -1);
    pet_app_setting_text(SET_QUIET_START, buf, sizeof(buf));
    CHECK(set->quiet_start == 21 && !strcmp(buf, "21:00"), "a step changes it");
    CHECK(s_setting_saves == kept + 1, "and keeps it");
    pet_app_setting_step(SET_TEXT, 1);
    CHECK(!set->text_large && s_setting_saves == kept + 1, "a setting that is not shown does not move");
    pet_app_setting_step(SET_PAUSE, 1);
    CHECK(set->paused && pet_app_paused(), "the pet can be paused");
    CHECK(!strcmp(line(PET_LINE_MOOD), "paused"), "and says so");
    char tile[32];
    CHECK(pet_app_line(PET_LINE_TILE_SETTINGS, tile, sizeof(tile)) && !strcmp(tile, "pet paused"),
          "the settings tile asks to be noticed");
    changed = s_count[PET_NOTICE_CHANGED];
    CHECK(run_until(PET_NOTICE_CHANGED, 2000), "a paused pet still says when to read");
    pet_app_setting_step(SET_PAUSE, -1);
    CHECK(!set->paused, "and woken");

    /* Retiring and starting over. */
    CHECK(!pet_app_retire(), "an egg cannot be retired");
    pet_app_set_favourite(2);
    pet_app_rename("Biscuit");
    int news = s_count[PET_NOTICE_NEWS];
    pet_app_start_over();
    CHECK(s_count[PET_NOTICE_NEWS] == news + 1 && !strcmp(s_last[PET_NOTICE_NEWS], "a new egg"),
          "starting over is news");
    CHECK(pet->name[0] == '\0' && pet->favourite == 0 && pet_app_recent(NULL) == 0, "and the pet is new");
    CHECK(set->quiet_start == 21, "the settings stay");
    run_ms(500);

    if (failures == 0) {
        printf("all screen checks passed\n");
    }
    return failures ? 1 : 0;
}
