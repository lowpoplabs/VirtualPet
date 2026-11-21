/*
 * Log replayer: feeds a foodlog.csv from the watch through the pet core
 * and prints how the pet would have fared, hour by hour.
 *
 *   pet_replay foodlog-day1.csv
 *   pet_replay foodlog-day1.csv --drain 0.8 --gap 120 --start 5
 *   pet_replay foodlog-day1.csv --as-logged
 *
 * Rows from a debug (names-on) log are re-classified with the current
 * rules, so a fix to the classifier shows up in old logs. --as-logged
 * keeps the food type the watch wrote at the time. Food values always
 * come from the current value table.
 *
 * To watch the same replay on the pet itself: virtualpet_sim --replay FILE
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log_reader.h"
#include "pet_core.h"

#define TICK_S 30

/* Running totals for one hour, or for the whole log. */
typedef struct {
    int meals, foods;
    float raw, eaten, wasted;
    float stomach_min, stomach_max;
    int ticks, hungry_ticks, full_ticks;
} span_t;

static void span_reset(span_t *s, float stomach)
{
    memset(s, 0, sizeof(*s));
    s->stomach_min = s->stomach_max = stomach;
}

static void span_add(span_t *total, const span_t *hour)
{
    total->meals += hour->meals;
    total->foods += hour->foods;
    total->raw += hour->raw;
    total->eaten += hour->eaten;
    total->wasted += hour->wasted;
    total->ticks += hour->ticks;
    total->hungry_ticks += hour->hungry_ticks;
    total->full_ticks += hour->full_ticks;
    if (hour->stomach_min < total->stomach_min) total->stomach_min = hour->stomach_min;
    if (hour->stomach_max > total->stomach_max) total->stomach_max = hour->stomach_max;
}

static const char *mood_name(pet_mood_t m)
{
    static const char *const names[] = {"happy", "content", "bored", "hungry", "sleepy", "queasy"};
    return names[m];
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    bool as_logged = false;
    pet_tuning_t tune = pet_tuning_default();

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--as-logged")) {
            as_logged = true;
        } else if (!strcmp(argv[i], "--drain") && i + 1 < argc) {
            tune.drain_per_hour = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--gap") && i + 1 < argc) {
            tune.meal_gap_s = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--start") && i + 1 < argc) {
            tune.start_stomach = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) {
            tune.repeat_factor = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--junk-limit") && i + 1 < argc) {
            tune.junk_limit = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--growth") && i + 1 < argc) {
            tune.growth_speed = (float)atof(argv[++i]);
        } else if (argv[i][0] != '-' && !path) {
            path = argv[i];
        } else {
            fprintf(stderr,
                    "usage: %s foodlog.csv [--as-logged] [--drain N] [--gap SECONDS]\n"
                    "       [--start STOMACH] [--repeat FACTOR] [--junk-limit N] [--growth SPEED]\n",
                    argv[0]);
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "no log file given\n");
        return 2;
    }
    log_reader_t reader;
    if (!log_reader_open(&reader, path, as_logged)) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }

    pet_t pet;
    bool started = false;
    uint32_t rng = 20261117;   /* fixed, so two runs on one log match */
    span_t hour, total;
    uint32_t hour_start = 0;
    int type_count[FOOD_TYPE_COUNT] = {0};
    float type_worth[FOOD_TYPE_COUNT] = {0};
    int reclassified = 0, rows = 0, queasy_times = 0, cravings_met = 0;
    log_row_t row;

    printf("hour             meals foods   raw  eaten wasted   stomach min-max    end  mood     craving\n");

    while (log_reader_next(&reader, &row)) {
        if (!started) {
            pet_init(&pet, &tune, row.time, 99);
            span_reset(&hour, pet.stomach);
            span_reset(&total, pet.stomach);
            hour_start = row.time - row.time % 3600;
            started = true;
        }

        /* Walk time forward to this row in small steps, as the watch would. */
        while (pet.now < row.time) {
            uint32_t to = pet.now + TICK_S < row.time ? pet.now + TICK_S : row.time;
            if (to > hour_start + 3600) {
                to = hour_start + 3600;
            }
            pet_meal_t m;
            bool ate = pet_tick(&pet, to, &m);
            if (pet_take_evolved(&pet)) {
                char date[16];
                log_format_date(pet.now, date, sizeof(date));
                printf("%s %02u:%02u  ** ", date, (unsigned)(pet.now % 86400 / 3600),
                       (unsigned)(pet.now % 3600 / 60));
                if (pet.stage == STAGE_HATCHLING) {
                    printf("hatched");
                } else {
                    printf("%s: %s", pet_stage_name(pet.stage), pet_form_name(pet.form));
                }
                printf(" (after %lu meals) **\n", (unsigned long)pet.meals);
            }
            if (ate) {
                hour.meals++;
                hour.eaten += m.value - m.wasted;
                hour.wasted += m.wasted;
                queasy_times += m.became_queasy;
                cravings_met += m.craving_satisfied;
            }
            hour.ticks++;
            hour.hungry_ticks += pet.stomach < tune.hungry_below;
            hour.full_ticks += pet.stomach >= tune.stomach_max - 0.01f;
            if (pet.stomach < hour.stomach_min) hour.stomach_min = pet.stomach;
            if (pet.stomach > hour.stomach_max) hour.stomach_max = pet.stomach;

            if (pet.now >= hour_start + 3600) {
                char date[16];
                log_format_date(hour_start, date, sizeof(date));
                printf("%s %02u:00 %5d %5d %5.1f %6.1f %6.1f   %5.1f - %5.1f  %5.1f  %-8s %s\n",
                       date, (unsigned)((hour_start % 86400) / 3600), hour.meals, hour.foods,
                       hour.raw, hour.eaten, hour.wasted, hour.stomach_min, hour.stomach_max,
                       pet.stomach, mood_name(pet_mood(&pet)), pet_craving_text(&pet));
                span_add(&total, &hour);
                span_reset(&hour, pet.stomach);
                hour_start += 3600;
            }
        }

        if (!row.is_food) {
            continue;
        }
        rows++;
        reclassified += row.reclassified;
        rng = rng * 1664525u + 1013904223u;
        float base = food_value(row.type, rng >> 8);
        float worth = pet_feed(&pet, row.type, base);
        hour.foods++;
        hour.raw += base;
        type_count[row.type]++;
        type_worth[row.type] += worth;
    }
    log_reader_close(&reader);
    if (!started) {
        fprintf(stderr, "no timestamped rows in %s\n", path);
        return 1;
    }

    printf("(last hour, partial: %d meals, %d foods, stomach %.1f, %s)\n", hour.meals, hour.foods,
           pet.stomach, mood_name(pet_mood(&pet)));
    span_add(&total, &hour);

    printf("\n== summary ==\n");
    printf("foods found            %d (%d re-classified by current rules)\n", rows, reclassified);
    printf("eating moments         %d\n", total.meals);
    printf("raw food value         %.1f\n", total.raw);
    printf("after eating rules     %.1f  (%.1f fitted in the stomach, %.1f wasted)\n",
           total.eaten + total.wasted, total.eaten, total.wasted);
    printf("stomach range          %.1f to %.1f, ended at %.1f\n", total.stomach_min,
           total.stomach_max, pet.stomach);
    printf("time hungry (<%.0f)      %.0f%%\n", tune.hungry_below,
           total.ticks ? 100.0f * total.hungry_ticks / total.ticks : 0);
    printf("time full              %.0f%%\n",
           total.ticks ? 100.0f * total.full_ticks / total.ticks : 0);
    printf("queasy episodes        %d\n", queasy_times);
    printf("cravings satisfied     %d\n", cravings_met);
    printf("settings               drain %.2f/h, gap %us, repeat x%.2f, junk limit %d\n",
           tune.drain_per_hour, (unsigned)tune.meal_gap_s, tune.repeat_factor, tune.junk_limit);

    float wifi = 0, ble = 0, all = 0;
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        all += type_worth[i];
    }
    printf("\n== by food ==          found   worth  share\n");
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        if (!type_count[i]) {
            continue;
        }
        bool is_ble = food_is_ble((food_type_t)i);
        printf("%-5s %-16s %5d  %6.1f  %4.0f%%\n", is_ble ? "ble" : "wifi",
               food_label((food_type_t)i), type_count[i], type_worth[i],
               all > 0 ? 100 * type_worth[i] / all : 0);
        *(is_ble ? &ble : &wifi) += type_worth[i];
    }
    printf("\nWi-Fi %.1f vs Bluetooth %.1f\n", wifi, ble);

    static const char *const group_names[GROUP_COUNT] = {
        "networks", "open and hotspots", "music", "cars", "phones and watches", "other",
    };
    float g[GROUP_COUNT], time_done, meals_done;
    pet_group_points(&pet, g);
    pet_growth_progress(&pet, &time_done, &meals_done);
    printf("\n== growth ==\n");
    printf("stage                  %s%s%s\n", pet_stage_name(pet.stage),
           pet.form != FORM_NONE ? ", " : "", pet_form_name(pet.form));
    printf("this stage             %.0f%% of its days, %.0f%% of its meals (speed x%g)\n",
           time_done * 100, meals_done * 100, tune.growth_speed);
    if (pet.stage != STAGE_ADULT) {
        printf("on this diet, next     %s\n", pet_form_name(pet_form_leaning(&pet)));
    }
    printf("diet by group         ");
    for (int i = 0; i < GROUP_COUNT; i++) {
        printf(" %s %.0f%%%s", group_names[i], all > 0 ? 100 * g[i] / all : 0,
               i + 1 < GROUP_COUNT ? "," : "\n");
    }
    return 0;
}
