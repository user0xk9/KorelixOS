#include <ulib.h>

/* Считает сумму 1/i^2 (стремится к pi^2/6 = 1.6449340668...) на double.
 * Для N = 2 000 000 результат * 1e6 равен 1644933. Если регистры SSE при
 * переключении задач портятся, у разных процессов получатся разные числа. */
void _start(void) {
    double sum = 0.0;

    for (long i = 1; i <= 2000000; i++) {
        double x = (double)i;
        sum += 1.0 / (x * x);
        if (i % 500000 == 0)
            sys_sleep_ms(10);        /* добровольное переключение в середине вычисления */
    }

    i64 result = (i64)(sum * 1000000.0);

    char line[80];
    char *p = line;
    p = u_append(p, "[fpu pid=");
    p = u_append_num(p, (u64)sys_getpid());
    p = u_append(p, "] sum * 1e6 = ");
    p = u_append_num(p, (u64)result);
    p = u_append(p, "\n");
    sys_write(1, line, (u64)(p - line));

    sys_exit(0);
}