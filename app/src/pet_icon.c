/*
 * The small picture of a retired pet, as the farm shows it: its form's
 * colour and the one or two things that mark it out.
 *
 * It is a list of rounded boxes and nothing else, so the watch's farm page
 * and a platform that draws its own screens (pet_ui.h) draw the same picture
 * from the same list, at whatever size they like.
 */
#include "pet_ui.h"

typedef struct {
    pet_icon_part_t *parts;
    int max, count;
} icon_t;

static void part(icon_t *icon, bool round, int w, int h, uint32_t color, int x, int y)
{
    if (icon->parts && icon->count < icon->max) {
        int half = (w < h ? w : h) / 2;
        pet_icon_part_t *p = &icon->parts[icon->count];
        p->x = (uint8_t)x;
        p->y = (uint8_t)y;
        p->w = (uint8_t)w;
        p->h = (uint8_t)h;
        /* A dot is as round as it can be; a slab has its corners taken off. */
        p->radius = (uint8_t)(round || half < 4 ? half : 4);
        p->color = color;
    }
    icon->count++;
}

static void dot(icon_t *icon, int w, int h, uint32_t color, int x, int y)
{
    part(icon, true, w, h, color, x, y);
}

static void slab(icon_t *icon, int w, int h, uint32_t color, int x, int y)
{
    part(icon, false, w, h, color, x, y);
}

int pet_farm_icon(pet_form_t form, pet_icon_part_t *parts, int max)
{
    static const uint32_t BODY[FORM_COUNT] = {
        [FORM_NONE] = 0x8CF2B0,     [FORM_GRAZER] = 0xC6E68C,    [FORM_HUNTER] = 0xA9BEF7,
        [FORM_HOMEBODY] = 0xBF7436, [FORM_ROAMER] = 0xFFC857,    [FORM_SCAVENGER] = 0xE8674C,
        [FORM_SINGER] = 0xC79BF2,   [FORM_RACER] = 0xF48FB1,     [FORM_GLOWBUG] = 0xA9A2FF,
        [FORM_PRISM] = 0xFF9CB8,
    };
    icon_t icon = {parts, parts ? max : 0, 0};
    icon_t *i = &icon;
    uint32_t c = BODY[form < FORM_COUNT ? form : FORM_NONE];
    int body_x = 8, body_w = 40;
    switch (form) {
    case FORM_HOMEBODY:     /* horn-antennae and a fringe */
        dot(i, 6, 14, 0xE9DFCB, 6, 10);
        dot(i, 6, 14, 0xE9DFCB, 44, 10);
        dot(i, 9, 9, 0xFFE27A, 3, 4);
        dot(i, 9, 9, 0xFFE27A, 44, 4);
        break;
    case FORM_ROAMER:       /* wings */
        dot(i, 26, 32, 0xF2811D, 0, 14);
        dot(i, 26, 32, 0xF2811D, 30, 14);
        slab(i, 3, 12, c, 21, 6);
        slab(i, 3, 12, c, 32, 6);
        dot(i, 8, 8, 0xFFE27A, 18, 1);
        dot(i, 8, 8, 0xFFE27A, 30, 1);
        body_x = 15;
        body_w = 26;
        break;
    case FORM_SCAVENGER:    /* blue pincers */
        dot(i, 16, 18, 0x3F8FE3, 0, 12);
        dot(i, 16, 18, 0x3F8FE3, 40, 12);
        slab(i, 3, 12, c, 20, 8);
        slab(i, 3, 9, c, 33, 11);
        dot(i, 8, 8, 0xFFE27A, 17, 3);
        dot(i, 8, 8, 0xFFE27A, 30, 6);
        break;
    case FORM_SINGER:       /* huge ears and a tail */
        dot(i, 18, 30, c, 6, 0);
        dot(i, 18, 30, c, 32, 0);
        dot(i, 8, 16, 0xF6BFE4, 11, 6);
        dot(i, 8, 16, 0xF6BFE4, 37, 6);
        dot(i, 14, 14, 0xF4EAFF, 44, 36);
        break;
    case FORM_RACER:        /* long ears, one striped */
        dot(i, 12, 32, c, 15, 0);
        dot(i, 12, 32, c, 30, 0);
        slab(i, 12, 3, 0xFFFFFF, 30, 9);
        slab(i, 12, 3, 0xFFFFFF, 30, 15);
        dot(i, 12, 12, 0xFFE6EE, 44, 40);
        break;
    case FORM_GLOWBUG:      /* ear fins and arms */
        dot(i, 18, 14, 0xC2BDFF, 0, 14);
        dot(i, 18, 14, 0xC2BDFF, 38, 14);
        for (int arm = 0; arm < 4; arm++) {
            slab(i, 7, 16, c, 12 + arm * 9, 40);
        }
        break;
    case FORM_PRISM:        /* a horn and a rainbow mane */
        slab(i, 8, 20, 0xFFD36B, 24, 0);
        dot(i, 12, 12, 0xFFB25B, 42, 18);
        dot(i, 12, 12, 0x7BE38F, 45, 28);
        dot(i, 12, 12, 0x7DB8F2, 42, 38);
        break;
    default:
        break;
    }
    dot(i, body_w, 36, c, body_x, 18);
    if (form == FORM_HOMEBODY) {
        slab(i, 26, 9, 0xE9AE6A, 15, 15);
        dot(i, 20, 11, 0xF3D9B4, 18, 41);
    }
    if (form == FORM_GLOWBUG) {
        dot(i, 6, 6, 0x9FFFE8, 25, 20);
    }
    int eye = form == FORM_ROAMER ? 4 : 0;
    dot(i, 9, 12, 0xFFFFFF, 17 + eye / 2, 26);
    dot(i, 9, 12, 0xFFFFFF, 30 - eye / 2, 26);
    dot(i, 4, 6, 0x10131A, 19 + eye / 2, 30);
    dot(i, 4, 6, 0x10131A, 32 - eye / 2, 30);
    return icon.count;
}
