// PlayStation Vita process settings (docs/dev/VITA.md), read by VitaSDK's start-up code. They live
// in the executable, not gt3r_engine: nothing references them, so the linker would leave them out
// of a static library.

#ifdef __vita__
#include "base/log.h"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

// VitaSDK's newlib printf has no C99 length modifiers: "%zu" prints "zu" and leaves its argument
// for the next conversion, so a "%zu ... %s" reads the size as a pointer (the first race on a
// console crashed so, in a hud.cpp log line). The game prints sizes with %zu in dozens of places,
// so every printf-family call is routed here (-Wl,--wrap=..., CMakeLists.txt): the format with
// `z` and `t` dropped (size_t and ptrdiff_t are 32 bits here, as unsigned / int) and `j` as `ll`.
namespace {
const char* vita_format(const char* fmt, char* buf, size_t size) {
    if (!fmt) return fmt;
    size_t o = 0;
    bool changed = false;
    for (const char* p = fmt; *p; ++p) {
        if (o + 4 >= size) return fmt; // too long to rewrite here: as it is
        buf[o++] = *p;
        if (*p != '%') continue;
        if (p[1] == '%') {
            buf[o++] = *++p;
            continue;
        }
        while (p[1] && std::strchr("-+ #0123456789.*", p[1])) {
            if (o + 4 >= size) return fmt;
            buf[o++] = *++p;
        }
        if (p[1] == 'z' || p[1] == 't') {
            ++p, changed = true;
        } else if (p[1] == 'j') {
            ++p, changed = true;
            buf[o++] = 'l';
            buf[o++] = 'l';
        }
    }
    buf[o] = 0;
    return changed ? buf : fmt;
}
} // namespace

extern "C" {
int __real_vsnprintf(char* s, size_t n, const char* fmt, va_list ap);
int __real_vsprintf(char* s, const char* fmt, va_list ap);
int __real_vfprintf(FILE* f, const char* fmt, va_list ap);

int __wrap_vsnprintf(char* s, size_t n, const char* fmt, va_list ap) {
    char buf[1024];
    return __real_vsnprintf(s, n, vita_format(fmt, buf, sizeof buf), ap);
}
int __wrap_vsprintf(char* s, const char* fmt, va_list ap) {
    char buf[1024];
    return __real_vsprintf(s, vita_format(fmt, buf, sizeof buf), ap);
}
int __wrap_vfprintf(FILE* f, const char* fmt, va_list ap) {
    char buf[1024];
    return __real_vfprintf(f, vita_format(fmt, buf, sizeof buf), ap);
}
int __wrap_snprintf(char* s, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_sprintf(char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
}

// vitaGL's messages and its shader compiler's (vitaGL built with LOG_ERRORS, its vgl_log pointed
// here by cmake/patches/vitagl-log.patch), in gt3r.log instead of the debug console.
extern "C" int gt3r_vgl_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t len = std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
    // (An error, so the log takes it to the memory card at once: a crash right after loses no message.)
    LOG_ERROR("vitaGL: %s", buf);
    return n;
}

namespace {
// Full speed from the start: the CPU at 444 MHz (333 by default) and the GPU at 222 MHz. The
// opening movie's MPEG-2 decode alone needs it.
struct FullClocks {
    FullClocks() {
        scePowerSetArmClockFrequency(444);
        scePowerSetBusClockFrequency(222);
        scePowerSetGpuClockFrequency(222);
        scePowerSetGpuXbarClockFrequency(166);
        // The main thread (static initialisers run on it) on the first user core alone: the
        // audio thread takes the second (AudioEngine's callbacks pin it), so mixing the race's
        // voices never takes time from the frame.
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
    }
} g_full_clocks;
} // namespace

extern "C" {
// The C/C++ heap, which newlib takes in one block at start-up. What it leaves of the ~365 MB a
// game may use is what SDL's GXM renderer and the kernel can still allocate; textures go to the
// GPU's own 128 MB. The ~365 MB needs extended memory mode (ATTRIBUTE2=12 in param.sfo,
// CMakeLists.txt): in the default ~256 MB the block is refused, newlib is left with no heap, and
// the first `new` of the static initializers aborts before main.
unsigned int _newlib_heap_size_user = 256 * 1024 * 1024;
// The main thread's stack: the default 256 KB is too small for the engine's deeper frames.
unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;
}
#endif
