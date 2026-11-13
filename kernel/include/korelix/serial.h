#pragma once

void serial_init(void);
void serial_write(const char *s);
void serial_putc(char c);
int serial_getc(void);