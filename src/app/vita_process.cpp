// PlayStation Vita process settings (docs/dev/VITA.md), read by VitaSDK's start-up code. They live
// in the executable, not gt3r_engine: nothing references them, so the linker would leave them out
// of a static library.

#ifdef __vita__
#include <psp2/power.h>

namespace {
// Full speed from the start: the CPU at 444 MHz (333 by default) and the GPU at 222 MHz. The
// opening movie's MPEG-2 decode alone needs it.
struct FullClocks {
    FullClocks() {
        scePowerSetArmClockFrequency(444);
        scePowerSetBusClockFrequency(222);
        scePowerSetGpuClockFrequency(222);
        scePowerSetGpuXbarClockFrequency(166);
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
