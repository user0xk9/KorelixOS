#pragma once

void kbd_init(void);
int  kbd_getc(void);   /* код символа или -1, если буфер пуст */