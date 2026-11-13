#pragma once

/* Системные вызовы возвращают -КОД при ошибке (значения как в Linux).
 * Те же значения продублированы в userland/libc/ulib.h. */
#define EPERM         1      /* общая ошибка */
#define ENOENT        2
#define E2BIG         7
#define ENOEXEC       8
#define EBADF         9
#define ENOMEM       12
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define ENOSPC       28
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOTEMPTY    39
