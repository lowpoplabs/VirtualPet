#include "screenshot.h"

#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"

static void put_le(FILE *f, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) fputc((v >> (8 * i)) & 0xFF, f);
}

bool save_screenshot(const char *path)
{
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (!snap) return false;

    uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    uint32_t row = (w * 3 + 3) & ~3u;
    FILE *f = fopen(path, "wb");
    if (!f) {
        lv_draw_buf_destroy(snap);
        return false;
    }
    fputc('B', f); fputc('M', f);
    put_le(f, 54 + row * h, 4); put_le(f, 0, 4); put_le(f, 54, 4);
    put_le(f, 40, 4); put_le(f, w, 4); put_le(f, h, 4);
    put_le(f, 1, 2); put_le(f, 24, 2); put_le(f, 0, 4);
    put_le(f, row * h, 4); put_le(f, 2835, 4); put_le(f, 2835, 4);
    put_le(f, 0, 4); put_le(f, 0, 4);

    static const uint8_t pad[3] = {0};
    for (int32_t y = (int32_t)h - 1; y >= 0; y--) {
        /* LVGL's RGB888 is stored B,G,R in memory, same as BMP. */
        fwrite(snap->data + (size_t)y * stride, 1, w * 3, f);
        fwrite(pad, 1, row - w * 3, f);
    }
    fclose(f);
    lv_draw_buf_destroy(snap);
    return true;
}
