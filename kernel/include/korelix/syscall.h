#pragma once
#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_GETPID   2
#define SYS_SLEEP_MS 3
#define SYS_OPEN     4
#define SYS_READ     5
#define SYS_CLOSE    6
#define SYS_READDIR  7
#define SYS_SPAWN    8

/* Порядок полей = порядок push в syscall_entry */
struct syscall_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx;
    uint64_t r9, r8, r10, rdx, rsi, rdi;
    uint64_t rax;
    uint64_t rip;       /* rcx при входе */
    uint64_t rflags;    /* r11 при входе */
    uint64_t rsp;       /* пользовательский стек */
};

void syscall_init(void);
int  copy_from_user(void *dst, uint64_t src, size_t n);
int  copy_to_user(uint64_t dst, const void *src, size_t n);