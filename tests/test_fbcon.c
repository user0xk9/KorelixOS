/* Консоль на framebuffer: отрисовка, UTF-8, перенос, прокрутка, курсор.
 * Запуск под ASan/UBSan: буфер выделен впритык, любая запись за границы = ошибка. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <korelix/fb.h>
#include "../kernel/drivers/video/font8x16.h"
#include "check.h"

void kprintf(const char *fmt, ...) { (void)fmt; }

#define W      640
#define H      480
#define PITCH  (W * 4 + 64)            /* строки с выравниванием: padding трогать нельзя */
#define COLS   78                      /* (640-16)/8  */
#define ROWS   29                      /* (480-16)/16 */
#define X0     8
#define Y0     8
#define FG     0xD8DEE9u               /* формат 0x00RRGGBB: red shift 16, green 8, blue 0 */
#define BG     0x0B0F14u
#define INV    0xFFFFFFu

static uint8_t *buf;

static uint32_t px(unsigned x, unsigned y) {
    uint32_t v;
    memcpy(&v, buf + (size_t)y * PITCH + (size_t)x * 4, 4);
    return v;
}

static const uint8_t *glyph(uint32_t cp) {
    for (int i = 0; i < FONT_GLYPHS; i++)
        if (font_codepoints[i] == cp) return font_data[i];
    return NULL;
}

/* Клетка (col,row) содержит символ cp (inv = под курсором) цветом fg */
static int cell_is(unsigned col, unsigned row, uint32_t cp, int inv, uint32_t fg) {
    const uint8_t *g = glyph(cp);
    CHECK(g);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t want = (g[y] & (0x80 >> x)) ? fg : BG;
            if (inv) want ^= INV;
            if (px(X0 + col * 8 + x, Y0 + row * 16 + y) != want) return 0;
        }
    return 1;
}
#define CELL(c, r, cp)  CHECK(cell_is((c), (r), (cp), 0, FG))

static void puts_fb(const char *s) { for (; *s; s++) fbcon_putc(*s); }

static int attach(void *addr, unsigned w, unsigned h, unsigned pitch, unsigned bpp) {
    return fb_attach(addr, w, h, pitch, (uint16_t)bpp, 8, 16, 8, 8, 8, 0);
}

static void save_ppm(const char *path, unsigned w, unsigned h, unsigned pitch) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            uint32_t v;
            memcpy(&v, buf + (size_t)y * pitch + (size_t)x * 4, 4);
            uint8_t rgb[3] = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(int argc, char **argv) {
    buf = malloc((size_t)PITCH * H);
    CHECK(buf);

    /* --- неподходящие режимы отклоняются, консоль остаётся выключенной --- */
    CHECK(attach(NULL, W, H, PITCH, 32) == -1);
    CHECK(attach(buf, W, H, PITCH, 24) == -1);
    CHECK(attach(buf, 16, H, PITCH, 32) == -1);
    CHECK(attach(buf, W, H, W * 4 - 4, 32) == -1);
    CHECK(attach(buf, W, H, W * 4 + 2, 32) == -1);
    CHECK(fbcon_cols() == 0 && fbcon_rows() == 0);
    fbcon_putc('x');                                   /* до attach: тихо игнорируется */
    fbcon_flush();

    /* --- нормальный режим --- */
    memset(buf, 0xAB, (size_t)PITCH * H);
    CHECK(attach(buf, W, H, PITCH, 32) == 0);
    CHECK(fbcon_cols() == COLS && fbcon_rows() == ROWS);
    CHECK(px(0, 0) == BG && px(W - 1, H - 1) == BG);

    puts_fb("Hi");
    CELL(0, 0, 'H');
    CELL(1, 0, 'i');
    fbcon_flush();
    CHECK(cell_is(2, 0, ' ', 1, FG));                  /* курсор = инвертированная клетка */
    fbcon_flush();                                     /* повторный flush не гасит курсор */
    CHECK(cell_is(2, 0, ' ', 1, FG));
    fbcon_putc('!');                                   /* вывод прячет курсор и рисует символ */
    CELL(2, 0, '!');
    fbcon_flush();
    CHECK(cell_is(3, 0, ' ', 1, FG));

    /* UTF-8: русские и украинские буквы */
    puts_fb("\n\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\x87\xD1\x97");   /* Привет Її */
    CELL(0, 1, 0x041F); CELL(1, 1, 0x0440); CELL(2, 1, 0x0438);
    CELL(3, 1, 0x0432); CELL(4, 1, 0x0435); CELL(5, 1, 0x0442);
    CELL(7, 1, 0x0407); CELL(8, 1, 0x0457);

    /* битый UTF-8 -> '?', дальше разбор продолжается */
    puts_fb("\n\xFF" "a");                             /* лишний байт 0xFF */
    CELL(0, 2, '?'); CELL(1, 2, 'a');
    puts_fb("\n\xC3(");                                /* начало двухбайтной, затем ASCII */
    CELL(0, 3, '?'); CELL(1, 3, '(');
    puts_fb("\n\xC0\x80" "z");                         /* «длинная» форма NUL */
    CELL(0, 4, '?'); CELL(1, 4, 'z');
    puts_fb("\n\xF0\x9F\x98\x80" "e");                 /* символ вне шрифта */
    CELL(0, 5, '?'); CELL(1, 5, 'e');
    puts_fb("\n\xE2\x82");                             /* оборванная трёхбайтная в конце потока */
    puts_fb("\n" "ok");                                /* следующая строка читается нормально */
    CELL(0, 7, 'o');

    /* перенос: ровно COLS символов не переводят строку, COLS+1-й переносится */
    puts_fb("\n");
    for (int i = 0; i < COLS; i++) fbcon_putc('a');
    puts_fb("\nb");                                    /* '\n' сразу после полной строки: без пустой строки */
    CELL(0, 9, 'b');
    puts_fb("\n");
    for (int i = 0; i < COLS + 3; i++) fbcon_putc('c');
    CELL(0, 11, 'c'); CELL(2, 11, 'c');                /* хвост на следующей строке */

    /* Backspace: обычный и через границу переноса */
    puts_fb("\nab\b \bc");
    CELL(0, 12, 'a'); CELL(1, 12, 'c');
    puts_fb("\n");
    for (int i = 0; i < COLS + 1; i++) fbcon_putc('z');    /* последний 'z' на следующей строке */
    puts_fb("\b \b");                                  /* стёрли его */
    CHECK(cell_is(0, 14, ' ', 0, FG));
    puts_fb("\b \b");                                  /* ушли на конец предыдущей строки и стёрли 'z' */
    CHECK(cell_is(COLS - 1, 13, ' ', 0, FG));
    CELL(COLS - 2, 13, 'z');

    /* табуляция */
    puts_fb("\n\tX");
    CELL(8, 14, 'X');                                  /* курсор вернулся на строку 13, перевод строки даёт 14 */

    /* прокрутка: 40 строк по одной букве, экран вмещает ROWS */
    puts_fb("\n");
    for (int i = 0; i < ROWS + 10; i++) { fbcon_putc((char)('A' + i % 26)); fbcon_putc('\n'); }
    /* последняя буква оказалась на ROWS-2, нижняя строка пустая */
    CELL(0, ROWS - 2, 'A' + (ROWS + 9) % 26);
    CELL(0, ROWS - 3, 'A' + (ROWS + 8) % 26);
    for (int x = 0; x < 8; x++) CHECK(px(X0 + x, Y0 + (ROWS - 1) * 16 + 5) == BG);

    /* красный вывод в панике */
    fbcon_panic_mode();
    fbcon_putc('P');
    {
        const uint8_t *g = glyph('P');
        int found = 0;
        for (int y = 0; y < 16 && !found; y++)
            for (int x = 0; x < 8 && !found; x++)
                if (g[y] & (0x80 >> x)) {
                    CHECK(px(X0 + x, Y0 + (ROWS - 1) * 16 + y) == 0xFF6B6Bu);
                    found = 1;
                }
        CHECK(found);
    }

    /* поля и padding между строками не тронуты */
    for (unsigned y = 0; y < H; y++)
        for (unsigned i = W * 4; i < PITCH; i++) CHECK(buf[(size_t)y * PITCH + i] == 0xAB);

    /* --- крошечный экран: 1x1 клетка, интенсивный вывод с прокруткой --- */
    free(buf);
    buf = malloc(24 * 4 * 40);
    CHECK(buf);
    CHECK(attach(buf, 24, 40, 24 * 4, 32) == 0);
    CHECK(fbcon_cols() == 1 && fbcon_rows() == 1);
    for (int i = 0; i < 300; i++) fbcon_putc((char)(i % 7 == 0 ? '\n' : (i % 5 == 0 ? '\t' : 'A' + i % 26)));
    for (int i = 0; i < 300; i++) fbcon_putc((char)('\b' * (i % 2)));
    fbcon_flush();
    free(buf);

    /* --- «скриншот» для глаз: экран загрузки --- */
    unsigned sw = 640, sh = 240;
    buf = malloc((size_t)sw * 4 * sh);
    CHECK(attach(buf, sw, sh, sw * 4, 32) == 0);
    puts_fb("Hello from KorelixOS!\nGDT OK\nIDT OK\nPMM: 480 MiB free of 511 MiB (bitmap 16 KiB)\n"
            "Framebuffer: 640x240, console 78x14\n"
            "\xD0\x94\xD0\xBE\xD0\xB1\xD1\x80\xD0\xBE \xD0\xBF\xD0\xBE\xD0\xB6\xD0\xB0\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB2 KorelixOS!\n"
            "korelix$ ls\n  [file] hello  12 bytes\n\ttab\n");
    puts_fb("korelix$ ");
    fbcon_flush();
    if (argc > 1) save_ppm(argv[1], sw, sh, sw * 4);
    free(buf);

    puts("test_fbcon: OK");
    return 0;
}
