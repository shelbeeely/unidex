#pragma once
// snprintf for the WebAssembly build: only %s, %d, %u, %lu and %02lu (what house_logic.h and house_wasm.cpp use).
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int snprintf(char *out, size_t len, const char *fmt, ...);
#ifdef __cplusplus
}
#endif
