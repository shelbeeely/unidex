// The tiny libc behind libc/string.h and libc/stdio.h (WebAssembly build only).
#include <stdarg.h>
#include "string.h"
#include "stdio.h"

void *memcpy(void *d, const void *s, size_t n) {
  unsigned char *a = d;
  const unsigned char *b = s;
  while (n--) *a++ = *b++;
  return d;
}
void *memmove(void *d, const void *s, size_t n) {
  unsigned char *a = d;
  const unsigned char *b = s;
  if (a < b)
    while (n--) *a++ = *b++;
  else
    while (n--) a[n] = b[n];
  return d;
}
void *memset(void *d, int c, size_t n) {
  unsigned char *a = d;
  while (n--) *a++ = (unsigned char)c;
  return d;
}
int memcmp(const void *x, const void *y, size_t n) {
  const unsigned char *a = x, *b = y;
  for (; n; n--, a++, b++)
    if (*a != *b) return *a - *b;
  return 0;
}
size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}
size_t strnlen(const char *s, size_t m) {
  size_t n = 0;
  while (n < m && s[n]) n++;
  return n;
}
char *strncpy(char *d, const char *s, size_t n) {
  size_t i = 0;
  for (; i < n && s[i]; i++) d[i] = s[i];
  for (; i < n; i++) d[i] = 0;
  return d;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return (unsigned char)*a - (unsigned char)*b;
}
char *strchr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return 0;
  }
}
char *strstr(const char *h, const char *n) {
  const size_t k = strlen(n);
  for (; *h; h++)
    if (!memcmp(h, n, k)) return (char *)h;
  return k ? 0 : (char *)h;
}

static void put(char *out, size_t len, size_t *at, char c) {
  if (*at + 1 < len) out[*at] = c;
  (*at)++;
}

static void putNumber(char *out, size_t len, size_t *at, unsigned long v, int width, int negative) {
  char digits[24];
  int n = 0;
  do digits[n++] = '0' + v % 10, v /= 10;
  while (v);
  if (negative) put(out, len, at, '-');
  for (int i = n; i < width; i++) put(out, len, at, '0');
  while (n) put(out, len, at, digits[--n]);
}

int snprintf(char *out, size_t len, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  size_t at = 0;
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      put(out, len, &at, *fmt);
      continue;
    }
    fmt++;
    int width = 0;
    if (*fmt == '0') fmt++;
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    int isLong = 0;
    while (*fmt == 'l') isLong = 1, fmt++;
    if (*fmt == 's') {
      for (const char *s = va_arg(ap, const char *); s && *s; s++) put(out, len, &at, *s);
    } else if (*fmt == 'd') {
      long v = isLong ? va_arg(ap, long) : va_arg(ap, int);
      putNumber(out, len, &at, v < 0 ? -(unsigned long)v : (unsigned long)v, width, v < 0);
    } else if (*fmt == 'u') {
      putNumber(out, len, &at, isLong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), width, 0);
    } else if (*fmt == '%') {
      put(out, len, &at, '%');
    }
  }
  if (len) out[at < len ? at : len - 1] = 0;
  va_end(ap);
  return (int)at;
}
