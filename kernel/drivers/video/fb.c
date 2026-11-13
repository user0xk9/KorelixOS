#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <korelix/fb.h>
#include <korelix/log.h>
#include <korelix/string.h>
#include "font8x16.h"

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

#define MARGIN 8       /* поля вокруг текста, пиксели */

static struct {
    uint8_t *base;
    uint64_t width, height, pitch;
    uint8_t rbits, rshift, gbits, gshift, bbits, bshift;
    int ready;

    uint32_t cols, rows;           /* размер сетки */
    uint32_t col, row;             /* позиция вывода (col == cols: перенос отложен) */
    uint32_t x0, y0;               /* смещение сетки от края экрана */
    uint32_t fg, bg, invert;

    int cursor_on;                 /* курсор сейчас нарисован */
    uint32_t cur_col, cur_row;

    uint32_t utf_cp;               /* разбор UTF-8 */
    int utf_left;
} fb;

static uint32_t pack(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(r >> (8 - fb.rbits)) << fb.rshift) |
           ((uint32_t)(g >> (8 - fb.gbits)) << fb.gshift) |
           ((uint32_t)(b >> (8 - fb.bbits)) << fb.bshift);
}

static uint32_t *pixel_row(uint64_t y) {
    return (uint32_t *)(fb.base + y * fb.pitch);
}

/* ---------- глифы ---------- */

static const uint8_t *glyph_for(uint32_t cp) {
    uint32_t lo = 0, hi = FONT_GLYPHS;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (font_codepoints[mid] < cp) lo = mid + 1; else hi = mid;
    }
    if (lo < FONT_GLYPHS && font_codepoints[lo] == cp) return font_data[lo];
    return NULL;
}

static void draw_cell(uint32_t col, uint32_t row, uint32_t cp) {
    const uint8_t *g = glyph_for(cp);
    if (!g) g = glyph_for('?');

    uint64_t px = fb.x0 + (uint64_t)col * FONT_W;
    uint64_t py = fb.y0 + (uint64_t)row * FONT_H;
    for (int y = 0; y < FONT_H; y++) {
        uint32_t *p = pixel_row(py + (uint64_t)y) + px;
        uint8_t bits = g[y];
        for (int x = 0; x < FONT_W; x++)
            p[x] = (bits & (0x80 >> x)) ? fb.fg : fb.bg;
    }
}

/* Курсор: инвертируем клетку; повторный вызов возвращает всё как было */
static void toggle_cursor(void) {
    uint64_t px = fb.x0 + (uint64_t)fb.cur_col * FONT_W;
    uint64_t py = fb.y0 + (uint64_t)fb.cur_row * FONT_H;
    for (int y = 0; y < FONT_H; y++) {
        uint32_t *p = pixel_row(py + (uint64_t)y) + px;
        for (int x = 0; x < FONT_W; x++) p[x] ^= fb.invert;
    }
    fb.cursor_on = !fb.cursor_on;
}

/* ---------- сетка ---------- */

static void clear_text_row(uint32_t row) {
    uint64_t py = fb.y0 + (uint64_t)row * FONT_H;
    for (int y = 0; y < FONT_H; y++) {
        uint32_t *p = pixel_row(py + (uint64_t)y);
        for (uint64_t x = 0; x < fb.width; x++) p[x] = fb.bg;
    }
}

static void scroll_up(void) {
    uint64_t dst = fb.y0 * fb.pitch;
    uint64_t src = (fb.y0 + FONT_H) * fb.pitch;
    uint64_t len = (uint64_t)(fb.rows - 1) * FONT_H * fb.pitch;
    memmove(fb.base + dst, fb.base + src, len);
    clear_text_row(fb.rows - 1);
}

static void newline(void) {
    fb.col = 0;
    if (fb.row + 1 >= fb.rows) scroll_up();
    else fb.row++;
}

static void emit(uint32_t cp) {
    if (fb.col >= fb.cols) newline();
    draw_cell(fb.col, fb.row, cp);
    fb.col++;
}

static void put_ascii(uint8_t c) {
    switch (c) {
    case '\n': newline(); break;
    case '\r': fb.col = 0; break;
    case '\b':
        if (fb.col > 0) fb.col--;
        else if (fb.row > 0) { fb.row--; fb.col = fb.cols - 1; }   /* назад по перенесённой строке */
        break;
    case '\t': {
        uint32_t next = (fb.col + 8) & ~7u;
        if (next > fb.cols) next = fb.cols;
        while (fb.col < next) emit(' ');
        break;
    }
    default:
        if (c >= 0x20 && c < 0x7F) emit(c);   /* остальные управляющие игнорируем */
    }
}

void fbcon_putc(char ch) {
    if (!fb.ready) return;
    uint8_t c = (uint8_t)ch;

    if (fb.cursor_on) toggle_cursor();          /* на время вывода курсор прячем */

    if (fb.utf_left) {
        if ((c & 0xC0) == 0x80) {
            fb.utf_cp = (fb.utf_cp << 6) | (c & 0x3F);
            if (--fb.utf_left == 0)
                emit(fb.utf_cp < 0x80 ? '?' : fb.utf_cp);   /* <0x80: недопустимая «длинная» форма */
            return;
        }
        fb.utf_left = 0;                        /* оборванная последовательность */
        emit('?');
    }

    if (c < 0x80)                put_ascii(c);
    else if ((c & 0xE0) == 0xC0) { fb.utf_cp = c & 0x1F; fb.utf_left = 1; }
    else if ((c & 0xF0) == 0xE0) { fb.utf_cp = c & 0x0F; fb.utf_left = 2; }
    else if ((c & 0xF8) == 0xF0) { fb.utf_cp = c & 0x07; fb.utf_left = 3; }
    else                         emit('?');
}

void fbcon_flush(void) {
    if (!fb.ready || fb.cursor_on) return;
    fb.cur_row = fb.row;
    fb.cur_col = fb.col < fb.cols ? fb.col : fb.cols - 1;
    toggle_cursor();
}

void fbcon_panic_mode(void) {
    if (fb.ready) fb.fg = pack(0xFF, 0x6B, 0x6B);
}

uint32_t fbcon_cols(void) { return fb.ready ? fb.cols : 0; }
uint32_t fbcon_rows(void) { return fb.ready ? fb.rows : 0; }

/* ---------- инициализация ---------- */

int fb_attach(void *addr, uint64_t width, uint64_t height, uint64_t pitch, uint16_t bpp,
              uint8_t red_size, uint8_t red_shift, uint8_t green_size, uint8_t green_shift,
              uint8_t blue_size, uint8_t blue_shift) {
    if (!addr || bpp != 32) return -1;                       /* поддерживаем только 32 бита на пиксель */
    if (red_size > 8 || green_size > 8 || blue_size > 8) return -1;
    if (red_shift > 24 || green_shift > 24 || blue_shift > 24) return -1;
    if (width < 2 * MARGIN + FONT_W || height < 2 * MARGIN + FONT_H) return -1;
    if (pitch < width * 4 || pitch % 4) return -1;

    memset(&fb, 0, sizeof(fb));
    fb.base = addr;
    fb.width = width; fb.height = height; fb.pitch = pitch;
    fb.rbits = red_size;   fb.rshift = red_shift;
    fb.gbits = green_size; fb.gshift = green_shift;
    fb.bbits = blue_size;  fb.bshift = blue_shift;

    fb.cols = (uint32_t)((width  - 2 * MARGIN) / FONT_W);
    fb.rows = (uint32_t)((height - 2 * MARGIN) / FONT_H);
    fb.x0 = (uint32_t)((width  - (uint64_t)fb.cols * FONT_W) / 2);
    fb.y0 = (uint32_t)((height - (uint64_t)fb.rows * FONT_H) / 2);

    fb.fg = pack(0xD8, 0xDE, 0xE9);
    fb.bg = pack(0x0B, 0x0F, 0x14);
    fb.invert = pack(0xFF, 0xFF, 0xFF);

    for (uint64_t y = 0; y < height; y++) {                  /* очистить весь экран */
        uint32_t *p = pixel_row(y);
        for (uint64_t x = 0; x < width; x++) p[x] = fb.bg;
    }
    fb.ready = 1;
    return 0;
}

void fb_init(void) {
    if (!fb_request.response || fb_request.response->framebuffer_count == 0) {
        kprintf("Framebuffer: none, output goes to serial only\n");
        return;
    }
    struct limine_framebuffer *f = fb_request.response->framebuffers[0];
    if (f->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb_attach(f->address, f->width, f->height, f->pitch, f->bpp,
                  f->red_mask_size, f->red_mask_shift,
                  f->green_mask_size, f->green_mask_shift,
                  f->blue_mask_size, f->blue_mask_shift)) {
        kprintf("Framebuffer: unsupported mode (%lux%lu, %u bpp), serial only\n",
                f->width, f->height, (unsigned)f->bpp);
        return;
    }
    kprintf("Framebuffer: %lux%lu, console %ux%u\n",
            f->width, f->height, fb.cols, fb.rows);
}
