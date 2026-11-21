/*
 * Artwork check: draws the pet alone, in one stage, form, mood and action,
 * and saves a picture. No pet logic runs, so the picture is exactly what
 * was asked for.
 *
 *   look_sheet OUT.bmp STAGE FORM [MOOD [ACTION [MS [UNDERFED]]]]
 *
 * STAGE 0-3 (egg, hatchling, juvenile, adult), FORM 0-9 (pet_form_t),
 * MOOD 0-5 (pet_mood_t), ACTION 0-5 (pet_action_t, or 9 to munch),
 * MS how long to let it animate before the picture (default 600).
 */
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "lvgl.h"
#include "pet_view.h"
#include "screenshot.h"

static uint32_t tick_cb(void) { return SDL_GetTicks(); }

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s OUT.bmp STAGE FORM [MOOD [ACTION [MS [UNDERFED]]]]\n", argv[0]);
        return 2;
    }
    int stage = atoi(argv[2]), form = atoi(argv[3]);
    int mood = argc > 4 ? atoi(argv[4]) : PET_MOOD_CONTENT;
    int action = argc > 5 ? atoi(argv[5]) : ACT_IDLE;
    uint32_t ms = argc > 6 ? (uint32_t)atoi(argv[6]) : 600;
    bool underfed = argc > 7 && atoi(argv[7]);

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_sdl_window_create(PET_AREA_SIZE, PET_AREA_SIZE);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    pet_view_set_look((pet_stage_t)stage, (pet_form_t)form, underfed);
    pet_view_create(scr);
    pet_view_set_mood((pet_mood_t)mood);
    if (action == 9) {
        pet_view_munch();
    } else {
        pet_view_set_action((pet_action_t)action);
    }

    while (SDL_GetTicks() < ms) {
        lv_timer_handler();
        SDL_Delay(5);
    }
    return save_screenshot(argv[1]) ? 0 : 1;
}
