# PlayStation Vita

A port of GT3 Revisited to the PlayStation Vita (homebrew, on a console running HENkaku /
Ensō or HENlo). Work in progress on the `vita` branch.

## Status

| Milestone | State |
|---|---|
| 1. Compiles and links for the Vita, packaged as `gt3r.vpk` | ✅ done |
| 2. Boots to the menus (2D only: no 3D device yet) | ✅ done: on a console with a PAL v1.00 disc, the language choice, opening movie and menus work with the Vita's buttons; cars and races need 3D |
| 3. 3D renderer on vitaGL, races drawn | not started |
| 4. Controls, audio, movies, saves under `ux0:data/` | not started |
| 5. Performance: a steady frame rate in races | not started |

## Playing

1. Install `gt3r.vpk` with VitaShell.
2. Copy a disc image made from your own *Gran Turismo 3 A-Spec* (`.iso`; `.chd` reads slower)
   to `ux0:data/gt3r/`. The game opens the first image there when no remembered one opens, and
   the setup screen's **Look in ux0:data/gt3r/** looks again.
3. Saves, `config.ini` and the log (`gt3r.log`, the run before as `gt3r.prev.log`) are in the
   same folder. A crash leaves the system's core dump in `ux0:data/` too.

## Building

Needs VitaSDK (`$VITASDK`, default `~/vitasdk`) with its packages (`vdpm`'s `install-all.sh`:
SDL3, zlib, vitaGL). In Ubuntu (WSL) or Linux:

```bash
packaging/vita/build.sh
```

This configures `build-vita/` with VitaSDK's toolchain file and builds `build-vita/gt3r.vpk`.
Copy it to the Vita and install it with VitaShell. Extra arguments go to CMake's configure
step.

What the Vita build changes (CMakeLists.txt, `GT3R_VITA`):

- No unit tests, `gt3tool`, relay server or developer tools: those are desktop programs.
- `GT3R_GLES` off: the Vita's GPU (PowerVR SGX543MP4) is OpenGL ES 2.0 class, and the OpenGL
  ES renderer needs 3.0 (`#version 300 es` shaders, uniform blocks, instanced draws).
- `eboot.bin` and `gt3r.vpk` from VitaSDK's `vita_create_self` / `vita_create_vpk`, title ID
  `GT3R00001`, LiveArea pictures from `packaging/vita/sce_sys` (placeholders drawn by
  `packaging/vita/make_art.py`: the Vita wants 8-bit palette PNGs).
- `src/app/vita_process.cpp`: the newlib heap (256 MB) and the main thread's stack (1 MB).
- Extended memory mode (`ATTRIBUTE2=12` in `param.sfo`, through `VITA_MKSFOEX_FLAGS`): about
  365 MB for the game instead of about 256 MB. Without it the 256 MB heap is refused, newlib has
  no heap at all, and the first `new` of the static initializers aborts before `main` (the first
  console run: "undefined instruction" in `_kill_r`, from `abort`, under `__libc_init_array`).

Reading a crash: the Vita writes `ux0:data/psp2core-*.psp2dmp`, a gzip'd ELF core whose notes
hold the modules, threads and registers. Map the game module's segment 1 offsets onto the linked
ELF (`build-vita/gt3r`, text at `0x81000000`) and run `arm-vita-eabi-addr2line -f -C` on them;
the words on the crashed thread's stack that point into segment 1 give the backtrace.
- `_GNU_SOURCE` and `pthread`: newlib hides POSIX 2008 under a strict `-std=c++20`, and
  `std::thread` needs VitaSDK's pthread library, linked whole (`--whole-archive`): libstdc++
  takes threads for working only when `pthread_cancel`, which nothing calls, is linked in;
  otherwise every `std::thread` throws "Enable multithreading to use std::thread: Not owner"
  (the second console run, right after the gamepad was found).
- libchdr and its codecs built without position-independent code: the Vita's executable format
  takes no GOT-relative relocations.

Platform code with a Vita branch (`__vita__` / `SDL_PLATFORM_VITA`):

- `platform/crash_handlers.cpp`: no `sigaction` on the Vita; only `abort()` is noted (the
  system writes its own core dump for a crash).
- `formats/ps2/memcard.cpp`: no `timegm` in newlib; the card times use C++20's calendar.
- `net/socket.cpp`: no `getifaddrs`. Online play is not ported yet: the network module is never
  loaded (`sceNetInit`), so sockets fail.
- `base/file.cpp`: disc images are read with `sceIoOpen` / `sceIoPread`. SDL's file streams seek
  with newlib's `fseeko`, whose offset is a 32-bit `long`, so nothing past 2 GB could be read.
- `game/gt_save.cpp`: the user data folder is `ux0:data/gt3r/`.
- `app/app.cpp`: a fullscreen 960 × 544 window; no 3D device (`use_gpu` off, the 2D renderer is
  SDL's `vitagxm`); the disc found in the user data folder (no file dialog: SDL's is the dummy
  one); Vita paths kept as they are (`std::filesystem` takes `ux0:...` for relative); no update
  check (upstream has no Vita package).
- `app/screen_setup.cpp`: says where to copy the image; the Browse button looks there again.
- `app/vita_process.cpp`: the CPU at 444 MHz and the GPU at 222 MHz from the start.

## The hardware

| | Vita | Lowest verified Android device (Galaxy A15) |
|---|---|---|
| CPU | 4 × Cortex-A9 at 333 MHz (444 MHz overclocked), 3 cores for games | 2 × Cortex-A76 at 2.2 GHz + 6 × A55 |
| GPU | PowerVR SGX543MP4, OpenGL ES 2.0 class (native API: GXM) | Mali-G57 MC2, OpenGL ES 3.2 / Vulkan |
| Memory | 512 MB (about 365 MB for a game) + 128 MB video | 4 GB |
| Screen | 960 × 544 | |

The A15 races at 35–55 fps (PLATFORMS.md), so the Vita needs the lighter settings at the
least: GT3's own LODs (`full_car_detail` off), a render scale below 1, and the post effects off.

## Plan for the 3D renderer (milestone 3)

What was found:

- SDL_GPU has no Vita backend, and VitaSDK's SDL3 is built with no OpenGL at all: its render
  drivers are `vitagxm` and software only. SDL's own Vita GL backends (`SDL_vitagles.c` on
  Sony's Piglet, `SDL_vitagles_pvr.c` / `SDL_vitagl_pvr.c` on PVR_PSP2) are OpenGL ES 2.0, and
  the OpenGL ES renderer needs 3.0.
- vitaGL has nearly all of what `gpu_gles.cpp` calls (instanced draws, attribute divisors,
  uniform blocks, framebuffer blits, vertex arrays, samplers, `glMapBufferRange`). Missing:
  `glClearBufferfv` / `glClearBufferfi`, `glDrawElementsInstancedBaseVertex`,
  `glGetInternalformativ`, `glInvalidateFramebuffer`, `glRenderbufferStorageMultisample`,
  `glTexStorage2D`.
- vitaGL and SDL's `vitagxm` renderer cannot both own the GPU (each initialises GXM).

The design: everything on vitaGL, reusing the OpenGL ES path as it is on other platforms.

1. SDL3 built from source for the Vita (`GT3R_FETCH_SDL`) with a patch in `cmake/patches`: a
   vitaGL backend for SDL's Vita video driver (beside `SDL_vitagles.c`: `vglInit`,
   `vglGetProcAddress`, `vglSwapBuffers`, `vglWaitVblankStart`), and SDL's `opengles2` render
   driver turned on for the Vita. The 2D layer is then SDL's OpenGL ES renderer, as in
   `GT3R_GLES` builds elsewhere, and the 3D views are textures it composites.
2. `GT3R_GLES` on for the Vita, with fallbacks in `gpu_gles.cpp` for the six missing calls
   (`glClear`, `glTexImage2D`, no MSAA renderbuffers: vitaGL's own MSAA mode instead).
3. The shaders: vitaGL translates GLSL to Cg at run time and compiles it with Sony's shader
   compiler, so the console needs `ur0:data/libshacccg.suprx` (extracted with ShaRKBR33D). How
   much of GLSL ES 3.00 its translator takes is measured on the console; what it refuses gets
   Vita variants from `compile_shaders.py`.
4. Lighter defaults for the Vita (render scale, LODs, no post effects), then measured frame
   times.

## Performance ideas

- The exact physics reproduces the EE's rounding toward zero in software on every operation
  (`base/ee_float.h`, `base/ps2_float.cpp`). The Cortex-A9's VFP has a round-toward-zero mode
  and flush-to-zero, so a Vita-only fast path could use the hardware, at the cost of replays and
  online races no longer matching other platforms bit for bit.
- Clock the CPU at 444 MHz and the GPU at 222 MHz (`scePowerSetArmClockFrequency`,
  `scePowerSetGpuClockFrequency`).
