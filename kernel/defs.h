// declarations of the kernel's functions, one section per file,
// in the same order as the C version's defs.h. each conversion
// step adds the section for the file it brings back.
#pragma once

// console.cpp
void consoleinit();
void consoleintr(int);
void consputc(int);

// printk.cpp
// format(printf): g++ checks each call's arguments against the
// format string, as it does for printf. the C version has no check.
int printk(const char *, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void panic(const char *);

// string.cpp
extern "C" {
void *memset(void *, int, uint64);
int memcmp(const void *, const void *, uint64);
void *memmove(void *, const void *, uint64);
void *memcpy(void *, const void *, uint64);
}
char *safestrcpy(char *, const char *, int);
int strlen(const char *);
int strncmp(const char *, const char *, uint);
char *strncpy(char *, const char *, int);

// uart.cpp
void uartinit();
void uartintr();
void uartputc_sync(int);

// fbcons.cpp
void fbconsinit();
void fbconsputc(int);

// kbd.cpp
void kbdinit();
void kbdintr();
