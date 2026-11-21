#include "log_reader.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 8

/* Splits one CSV line in place. Handles "quoted, fields" with "" escapes. */
static int split_csv(char *line, char *fields[], int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        char *out = p;
        fields[n++] = out;
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && p[1] == '"') {
                    *out++ = '"';
                    p += 2;
                } else if (*p == '"') {
                    p++;
                    break;
                } else {
                    *out++ = *p++;
                }
            }
        } else {
            while (*p && *p != ',' && *p != '\r' && *p != '\n') {
                *out++ = *p++;
            }
        }
        char end = *p;
        *out = '\0';
        if (end != ',') {
            break;
        }
        p++;
    }
    return n;
}

/* "2026-11-18 07:23:58" -> local seconds. */
static bool parse_time(const char *s, uint32_t *out)
{
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return false;
    }
    /* Days since 1970-01-01 (days-from-civil). */
    y -= mo <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    *out = (uint32_t)(days * 86400 + h * 3600 + mi * 60 + se);
    return true;
}

void log_format_date(uint32_t t, char *out, size_t size)
{
    long z = (long)(t / 86400) + 719468;
    long era = z / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    long y = (long)yoe + era * 400 + (m <= 2);
    snprintf(out, size, "%04ld-%02u-%02u", y, m, d);
}

static wifi_sec_t sec_from_auth(int auth)
{
    /* esp-idf wifi_auth_mode_t numbers, as written in the log's detail field. */
    switch (auth) {
    case 0: case 10:
        return WIFI_SEC_OPEN;
    case 6: case 7: case 11: case 12: case 13: case 15: case 16:
        return WIFI_SEC_WPA3;
    default:
        return WIFI_SEC_SECURED;
    }
}

/* Rebuilds the sighting from the log's name and detail columns and classifies it again. */
static food_type_t reclassify(const char *kind, const char *name, const char *detail)
{
    if (strcmp(kind, "wifi") == 0) {
        int auth = 3;
        const char *a = strstr(detail, "auth=");
        if (a) {
            auth = atoi(a + 5);
        }
        wifi_sighting_t s = {.ssid = name, .sec = sec_from_auth(auth)};
        return food_classify_wifi(&s);
    }

    ble_sighting_t s = {.name = name};
    uint8_t payload[1];
    uint16_t uuids[8];
    size_t n = 0;
    unsigned v, t;
    const char *p = strstr(detail, "co=");
    if (p && sscanf(p, "co=%4x", &v) == 1) {
        s.has_company = true;
        s.company_id = (uint16_t)v;
        if (p[7] == '/' && sscanf(p + 8, "%2x", &t) == 1) {
            payload[0] = (uint8_t)t;
            s.mfg_payload = payload;
            s.mfg_payload_len = 1;
        }
    }
    p = strstr(detail, "app=");
    if (p && sscanf(p, "app=%4x", &v) == 1) {
        s.has_appearance = true;
        s.appearance = (uint16_t)v;
    }
    for (p = strstr(detail, "u="); p && n < 8; p = strstr(p + 2, "u=")) {
        if (sscanf(p, "u=%4x", &v) == 1) {
            uuids[n++] = (uint16_t)v;
        }
    }
    s.uuids16 = uuids;
    s.num_uuids16 = n;
    return food_classify_ble(&s);
}

bool log_reader_open(log_reader_t *r, const char *path, bool as_logged)
{
    r->file = fopen(path, "r");
    r->as_logged = as_logged;
    return r->file != NULL;
}

bool log_reader_next(log_reader_t *r, log_row_t *row)
{
    char line[512];
    while (r->file && fgets(line, sizeof(line), r->file)) {
        char *col[MAX_FIELDS] = {0};
        int n = split_csv(line, col, MAX_FIELDS);
        if (n < 5 || !parse_time(col[0], &row->time)) {
            continue;
        }
        const char *kind = col[2];
        row->is_food = !strcmp(kind, "wifi") || !strcmp(kind, "ble");
        row->reclassified = false;
        row->name[0] = '\0';
        row->type = FOOD_TYPE_COUNT;
        if (!row->is_food) {
            return true;
        }

        row->type = food_from_label(col[3]);
        bool has_detail = n >= 8 && (col[6][0] || col[7][0]);
        if (has_detail) {
            snprintf(row->name, sizeof(row->name), "%s", col[6]);
            if (!r->as_logged) {
                food_type_t again = reclassify(kind, col[6], col[7]);
                row->reclassified = again != row->type;
                row->type = again;
            }
        }
        if (row->type >= FOOD_TYPE_COUNT) {
            continue;   /* a label this build does not know */
        }
        return true;
    }
    return false;
}

void log_reader_close(log_reader_t *r)
{
    if (r->file) {
        fclose(r->file);
        r->file = NULL;
    }
}
