#include <stdint.h>
#include <stddef.h>
#include <korelix/console.h>
#include <korelix/keyboard.h>
#include <korelix/serial.h>
#include <korelix/sched.h>
#include <korelix/string.h>
#include <korelix/log.h>

#define CONSOLE_LINE_MAX 256

static char line[CONSOLE_LINE_MAX];
static size_t line_len, line_pos;
static int esc_state;          /* 0 обычный, 1 после ESC, 2 внутри CSI-последовательности */

/* Символ с PS/2-клавиатуры или из serial (терминал, где запущен QEMU) */
static int poll_char(void) {
    int c = kbd_getc();
    return c >= 0 ? c : serial_getc();
}

static void echo(char c) {
    kwrite(&c, 1);
}

/* Предполагается один читатель (shell). Пока строки нет, задача спит по 10 мс. */
long console_read(char *buf, size_t len) {
    if (len == 0) return 0;

    if (line_pos >= line_len) {                 /* прошлая строка выдана целиком: ждём новую */
        line_len = line_pos = 0;
        for (;;) {
            int c = poll_char();
            if (c < 0) { task_sleep_ms(10); continue; }

            /* стрелки и прочие escape-последовательности пока проглатываем */
            if (esc_state == 1) { esc_state = (c == '[') ? 2 : 0; continue; }
            if (esc_state == 2) { if (c >= 0x40 && c <= 0x7e) esc_state = 0; continue; }
            if (c == 0x1b) { esc_state = 1; continue; }

            if (c == '\r') c = '\n';
            if (c == '\n') {
                line[line_len++] = '\n';
                echo('\n');
                break;
            }
            if (c == 4) {                       /* Ctrl-D: конец ввода на пустой строке */
                if (line_len == 0) return 0;
                continue;
            }
            if (c == 0x7f || c == '\b') {       /* Backspace */
                if (line_len > 0) {
                    line_len--;
                    kwrite("\b \b", 3);
                }
                continue;
            }
            if (c >= 0x20 && c < 0x7f && line_len < CONSOLE_LINE_MAX - 1) {
                line[line_len++] = (char)c;
                echo((char)c);
            }
        }
    }

    size_t n = line_len - line_pos;
    if (n > len) n = len;
    memcpy(buf, line + line_pos, n);
    line_pos += n;
    return (long)n;
}