#pragma once
// A tiny libc for the freestanding WebAssembly build (tools/wasm/build.sh): just what house_logic.h uses.
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
char *strncpy(char *d, const char *s, size_t n);
int strcmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strstr(const char *h, const char *n);
#ifdef __cplusplus
}
#endif
