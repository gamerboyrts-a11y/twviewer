#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

extern bool g_logging_enabled;

static inline void log_write(const char *fmt, ...) {
    if (!g_logging_enabled) return;
    FILE *f = fopen("/config/twviewer.log", "a");
    if (!f) return;
    va_list va; va_start(va, fmt);
    vfprintf(f, fmt, va); fprintf(f, "\n");
    va_end(va); fclose(f);
}
#define LOG(...) log_write(__VA_ARGS__)
