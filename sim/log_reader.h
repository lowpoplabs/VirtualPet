/* Reads a foodlog.csv written by the watch, one row at a time. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pet_food.h"

typedef struct {
    FILE *file;
    bool as_logged;     /* keep the food type the watch wrote; do not re-classify */
} log_reader_t;

typedef struct {
    uint32_t time;      /* local seconds; midnight falls on a multiple of 86400 */
    bool is_food;       /* false for boot and battery rows */
    food_type_t type;   /* valid when is_food */
    bool reclassified;  /* current rules gave a different type than the watch did */
    char name[64];      /* "" unless the log was written by a names-on build */
} log_row_t;

bool log_reader_open(log_reader_t *r, const char *path, bool as_logged);

/* Returns false at the end of the file. Rows without a timestamp are skipped. */
bool log_reader_next(log_reader_t *r, log_row_t *row);

void log_reader_close(log_reader_t *r);

/* Local seconds -> "2026-11-18". */
void log_format_date(uint32_t time, char *out, size_t size);
