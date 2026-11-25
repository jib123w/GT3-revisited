# PlayStation Vita

A port of GT3 Revisited to the PlayStation Vita (homebrew, on a console running HENkaku /
Ensō or HENlo). Work in progress on the `vita` branch.

## Status

| Milestone | State |
|---|---|
| 1. Compiles and links for the Vita, packaged as `gt3r.vpk` | ✅ done |
| 2. Boots to the menus (2D only: no 3D device yet) | ✅ done: on a console with a PAL v1.00 disc, the language choice, opening movie and menus work with the Vita's buttons; cars and races need 3D |
| 3. 3D renderer on vitaGL, races drawn | ✅ done: cars, courses and races draw correctly (the hand-written Cg shaders, the vitaGL patches below) |
| 4. Controls, audio, movies, saves under `ux0:data/` | not started |
| 5. Performance: a steady frame rate in races | in progress: lighter Vita defaults and a frame-time log (below); measured at about 24 fps in the showroom and 3 fps in races before them |

## Playing

1. Install `gt3r.vpk` with VitaShell.
2. Copy a disc image made from your own *Gran Turismo 3 A-Spec* (`.iso`; `.chd` reads slower)
   to `ux0:data/gt3r/`. The game opens the first image there when no remembered one opens, and
   the setup screen's **Look in ux0:data/gt3r/** looks again.
3. Saves, `config.ini` and the log (`gt3r.log`, the run before as `gt3r.prev.log`) are in the
   same folder. A crash leaves the system's core dump in `ux0:data/` too.
4. The console needs `ur0:data/libshacccg.suprx` (ShaRKBR33D extracts it) for vitaGL's shaders.

Race controls: GT3 original by default. Modern drives with the triggers, which the handheld has
none of, so a config from before (which saved Modern as its default) is moved to GT3 original
once (`[input] vita_race_layout`, `initial_race_layout`). GT3 original reads the menu buttons
elsewhere; on the Vita it has its own set made from them (`PlayerBindings::set`,
`vita_gt3_race`), because the handheld's L and R are the menu's L1 / R1 and it has no L2 / R2
for the gears: what drives L1 / R1 shifts down / up, Select changes the view, the right stick
looks around and back. Cross accelerates, Square brakes, as on the PS2.

The touch screen: no on-screen pad on the Vita (`TouchControls::visible` is always false; it has
its buttons), so touches go to the screen underneath. A touch anywhere shows a Settings tab in the
top right corner for three seconds; tapping it opens Settings (`App::vita_tab_event`).

Your own LiveArea pictures: `packaging/vita/make_custom_art.ps1 -Background <image> -Icon <image>`
(Windows PowerShell, WPF: PNG, JPEG, WebP) writes 8-bit palette PNGs to `packaging/vita/custom`,
which git ignores and the build takes over the placeholders (reconfigure after making them). The
icon is also the start button. Keep artwork you have no right to share out of anything you share.
The script writes the PNG files itself (IHDR with an 8-bit palette, PLTE, IDAT, IEND, nothing
else): the Vita's installer refuses a VPK whose LiveArea PNGs carry sRGB / gAMA / pHYs chunks or
are greyscale (WPF's encoder does both, the latter for a black-and-white picture) with error
0x8010113D at the end of the install.

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
- `%zu`: VitaSDK's newlib printf has no C99 length modifiers. `%zu` printed "zu" (the log's
  "zu meshes") and left its argument to the next conversion, so a `%zu ... %s` read a size as a
  string pointer: every race start crashed in `strlen` under `_svfprintf_r`, from a `hud.cpp` log
  line. The printf family (`vsnprintf`, `snprintf`, `vsprintf`, `sprintf`, `vfprintf`, `fprintf`,
  `printf`) is linked through `--wrap` to `app/vita_process.cpp`, which drops `z` / `t` (32-bit
  here) and turns `j` into `ll` before calling newlib's.

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

Done so far:

- `cmake/patches/sdl-vita-vitagl.patch`: `src/video/vita/SDL_vitagles_vitagl.c` (SDL's GL entry
  points on vitaGL: `vglInitExtended` at the first context, `vglGetProcAddress`,
  `vglSwapBuffers`, `vglWaitVblankStart`), the `VIDEO_VITA_VITAGL` option, and SDL's
  `opengles2` render driver on the Vita. Hints: `SDL_VITA_VGL_RAM_THRESHOLD` (bytes of main
  memory vitaGL leaves, 8 MB), `SDL_VITA_VGL_MSAA` (0, 2, 4). `vglInitExtended`'s return value
  is not success: `GL_TRUE` means the size was lowered to the display's largest, and a working
  init at 960 × 544 returns `GL_FALSE` (the first console run took that for a failure, and SDL's
  `vitagxm` fallback then crashed on the GXM vitaGL had already set up).
- `cmake/Dependencies.cmake`: SDL from source on the Vita (`VITA` set for SDL's CMake, the patch
  added to SDL's list; `ApplyPatches.cmake` takes `|` between patches).
- `GT3R_GLES` on for the Vita; `render/gpu_gles.cpp` takes stand-ins for the six calls vitaGL
  lacks and vitaGL's own version string.
- `app/app.cpp`: an OpenGL window, the OpenGL ES device first, one sample (no MSAA).
- vitaGL built from source (`cmake/Dependencies.cmake`, pinned commit, LGPL-3.0) and linked ahead
  of VitaSDK's package. The package's translator has no uniform blocks (`HAVE_GLSL_UBOS`) and it
  is built without error handling (`NO_DEBUG`): the first console run with 3D crashed in SceGxm
  under `glLinkProgram` from `program_for`, the first time a 3D view drew. vitaGL compiles GLSL
  only at `glLinkProgram` (`VGL_MODE_POSTPONED`), so `GL_COMPILE_STATUS` says nothing; a shader
  it cannot translate or compile then had no program to register. Built here with
  `HAVE_GLSL_UBOS=1 LOG_ERRORS=1 HAVE_SHARK_LOG=1`, and `cmake/patches/vitagl-log.patch` points
  its `vgl_log` at `gt3r_vgl_log` (`app/vita_process.cpp`): its errors and the shader compiler's
  are in `gt3r.log` as `vitaGL: ...` lines.
- What that log showed next (the second 3D run, crashed in vitaGL's `scene_reset` under `glClear`
  in `View3D::placeholder_cube`):
  - 32-bit float depth: vitaGL's header numbers `GL_DEPTH_COMPONENT32F` `0x8DAB` (OpenGL ES 3.0:
    `0x8CAC`; `GL_DEPTH32F_STENCIL8` `0x8DAC` / `0x8CAD`) and refuses the ES numbers. `tex_format`
    gives vitaGL its own numbers on the Vita. Its depth buffer is float whatever is asked
    (`DF32M`, `gxm.c`), so D32_FLOAT is also the format whose depth bias (64 units) suits it: a
    first fix that made the renderer take D24 left the D24 bias (2 units) on a float buffer, and
    distant coplanar layers z-fought (stair-stepped edges along a race's horizon).
  - Vertex offsets past 64 KB (`cmake/patches/vitagl-packed-offsets.patch`): for interleaved
    attributes from one buffer, vitaGL started the vertex stream at the buffer's start and put each
    attribute's whole offset into `SceGxmVertexAttribute.offset`, which is 16 bits. Any mesh stored
    more than 64 KB into its buffer read its vertices from elsewhere: a car's body (first in its
    buffer) was right while its wheels and tyres (after it) drew as rainbow shards, and far scenery
    in a race broke along the horizon. The patch starts the stream at the lowest enabled
    attribute's offset and keeps each attribute's offset from there. (`gpu_gles.cpp` moves the
    attribute offsets by a draw's vertex offset, vitaGL having no base vertex draws, which makes
    such offsets common.) The game relinks when vitaGL is rebuilt (`LINK_DEPENDS` on
    `libvitaGL.a`: SDL links it by name, so make did not know).
  - Instanced draws: with no attribute that has a divisor, vitaGL fetches every vertex stream of
    an instanced draw by the instance index (`custom_shaders.c`, `index_type & 2`). The rims'
    turned copies (`DrawItem::spin_turn`) were drawn so, as shards about the wheels.
    `GpuContext::spin_copies()` is false on the Vita (one copy; the Cg has no SPIN_COPIES variant
    anyway), and `replay` never draws instanced there.
  - No cube faces as frame buffer attachments (vitaGL's `glFramebufferTexture2D` takes
    `GL_TEXTURE_2D` only). A pass into a cube face draws into a 2D stand-in per cube and level, and
    `run_pass` copies it into the face with `glCopyTexSubImage2D`, which vitaGL does through the
    CPU (`glReadPixels` + `glTexSubImage2D`): correct, and slow for the per-frame environment cube
    (six read backs a full refresh). Blits and copies from cube faces are not covered (none
    happen today).
  - The shaders (the third 3D run): vitaGL's GLSL translator is made for OpenGL ES 2.0 style
    GLSL and turns the GLSL ES 3.00 here into Cg Sony's compiler rejects: block instance names
    (`u.params`), `layout(location = n) in` attributes, and then `texelFetch`, `textureSize`,
    integer bit operations, flat `int` varyings, `int` arrays and `gl_SampleMask` in
    `model.frag`. `program_for` remembers the shaders of a failed link and never attaches them
    again (vitaGL crashes attaching a shader it could not compile), so 3D is missing rather than
    the game ending.
- The shaders in Cg (`render/shaders/vita_cg.h`, by hand from `model.vert` / `model.frag` /
  `no_color.frag`; built, not yet run on a console). `gpu_gles.cpp` creates them with vitaGL's
  Cg shader types (`GL_CG_VERTEX_SHADER_EXT` / `GL_CG_FRAGMENT_SHADER_EXT`: no GLSL
  translation), binds the attributes by name to the GLSL locations before the link
  (`ShaderCode::gles_attributes`), and uploads each draw's uniform blocks as `float4` arrays
  named as the blocks with `glUniform4fv`, from the command buffer's arena (no uniform buffer).
  `GpuContext::make_shader` gives the stages without Cg (GS filter, wheel blurs, rim copies) the
  nearest one. What the Cg leaves out or simplifies is listed at the top of `vita_cg.h`.
  - Heap corruption (crashes in `free` while loading a car, and in vitaGL's texture free when a
    menu model was released): the Cg compiler drops the unused tail of a uniform array, vitaGL
    sizes each program's uniform memory to what is left (`calloc`, the program's default uniform
    buffer size), and its `glUniform4fv` writes `count` float4s without a check. A whole block
    written into a shortened array ran over the heap. `program_for` reads each array's compiled
    size (`glGetActiveUniform`; logged as `vitaGL program N: uniform arrays ...`) and `replay`
    never uploads more.
  - Mip chains (the crash leaving a race, in `sceClibMspaceFree` under `gpu_free_texture_data`
    as the course's textures were released): vitaGL lays a mip chain out at the power-of-two
    sizes and ends it where the shorter side reaches 1, but the game asks a full chain (256x16:
    9 levels, vitaGL made room for 5). The levels past vitaGL's were written after the end of the
    texture's memory, over the memory pool's records of the next block. `vita_levels` gives
    power-of-two 2D textures only the levels vitaGL makes (others one level), and uploads to the
    rest are dropped. `cmake/patches/vitagl-mip-stride.patch`: glTexSubImage2D into a mip level of
    a texture drawn in the last frames wrote its rows at an uninitialised stride.
  - Colour detaches: `attach(..., nullptr)` detached with `glFramebufferRenderbuffer`, which
    vitaGL refuses for colour (`GL_INVALID_ENUM`, logged twice a frame). A first fix detached with
    `glFramebufferTexture2D(0)`, but a detach throws the frame buffer's render target away (this
    vitaGL keeps one per frame buffer, no shared ones), so every copy and blit then made a new one.
    The Vita now leaves the copy frame buffers' colour attached (a same-size texture keeps the
    render target; a released texture they hold is freed when they take the next: two at most).
  - Out of render targets (the log after the reserve checks: `SCE_GXM_ERROR_OUT_OF_RENDER_TARGETS`
    from a race's finish on, hundreds of failed scenes, and the dealership's car not drawn): this
    vitaGL gives every frame buffer a render target of its own while it lives, and the GPU has few
    (vitaGL's shared mode pools only those up to 256 wide). Every view's placeholder cube was
    cleared in six render passes, which left six frame buffers (six render targets) for the view's
    life; on the Vita it is uploaded instead (`View3D::placeholder_cube`). A cube's faces also share
    one frame buffer there (`pass_fbo`: they all draw into the same stand-in). SDL's GLES2 renderer
    still keeps one frame buffer per render-texture size it has seen.
  - GT Mode menus broken after a race (`std::bad_alloc` loading `gt_mode*.pmb` pages and their
    images): `MenuSet` keeps every pmb it has loaded, with its decoded images, for the session, and
    the renderer every page's textures; on the Vita's 256 MB heap the pixels ran out. A menu set's
    scenes now drop their pixels once the renderer has them on the GPU and decode them again from
    the file if asked (`MenuScene::drop_pixels` / `ensure_pixels`, `set_source`); a pmb that
    fails for want of memory is not remembered as failed. Models keep no CPU copy of their geometry
    there either (only the guard-band clipping read it). The PERF lines give the heap in use.
  - "The race cannot start: std::bad_alloc" after browsing GT Mode (heap ~224 MB of 256 in the
    menus): the menus' car models (up to 13 kept) and what vitaGL had not freed yet left the race's
    load too little. vitaGL frees what the game releases only 4 frames later, on its GC thread
    (and a load draws no frames), and spills to the heap once its own pools are full. The menus
    now keep 3 cars on the Vita (as on phones: `platform::low_power()`). vitaGL is built with
    `HAVE_GC_ON_FINISH=1`, so a GPU wait (`glFinish`) also frees everything waiting to be freed.
    Starting a race waits that way once the menus have released their models
    (`Gt3MenuScreen::start_game`). A load's own periodic waits (`note_gpu_submits`) free memory too.
  - Black rear-view mirror: the mirror's mirrored view culls the front faces, and vitaGL culls
    back faces only (its `change_cull_mode` sets `no_polygons_mode` for `GL_FRONT`: nothing drawn).
    The OpenGL ES device turns a front cull into a back cull with the other winding in front
    (the pipeline's set-up); vitaGL uses the winding for nothing else.
  - Crash at a race's end (exact physics, a new view's placeholder cube being cleared):
    `update_scissor_test` wrote through `sceGxmReserveVertexDefaultUniformBuffer`'s pointer
    without checking the call, and with no scene begun the pointer was stack garbage.
    `cmake/patches/vitagl-reserve-checks.patch` checks the reserves of the clear, the scissor
    set-up and the blit, skips the draw and logs instead. vitaGL's messages now go to the log as
    errors (written at once: the crash's own reason was lost in the last 5 s), and the game no
    longer sets `GL_TEXTURE_MAX_LEVEL` there (vitaGL has none; a warning every ~30 s).
  - Harmless at start: `glPixelStorei` (pack/unpack alignment), `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`
    (the 256 default stands), sampler `GL_TEXTURE_WRAP_R` / `MIN_LOD` / `MAX_LOD`, texture
    `GL_TEXTURE_MAX_LEVEL`, and the six calls with stand-ins.

## Performance (milestone 5)

Done so far:

- **Lighter defaults** (`platform::low_power()`: phones, tablets and the Vita; `is_mobile()` stays
  Android/iOS only, as it also brings the touch controls): full car detail, depth of field and
  showroom wheels off, dynamic resolution on. Each is still in Settings.
- **Car reflections off by default** (`[video] car_reflections = on` brings them back): the env
  cube is six more course passes a frame, and vitaGL has no cube-face render targets, so every
  face also goes through `glCopyTexSubImage2D`, which in vitaGL is `glReadPixels` (a wait for the
  GPU to finish) and `glTexSubImage2D`: six GPU stalls a frame.
- **Streamed buffers**: vitaGL's `glBufferSubData` into a buffer drawn from in the last frames
  copies the whole buffer into new storage around the update. A cycled upload from offset 0 now
  takes new storage with `glBufferData(nullptr)` first (`gt3r_gl_UploadToGPUBuffer`).
- **Catch-up cap**: `FixedStep::advance` runs at most 4 ticks a frame on the Vita (8 elsewhere), so
  a slow frame slows the game down instead of piling ticks onto the next one.
- **Frame-time log**: every 2 s a `PERF` line in `gt3r.log`: frames a second, then the average
  frame split into pre (events), update (with sim ticks a frame and their time), render (env cube,
  item collection, scene draw) and present, the most meshes drawn and the 3D resolution. Then a
  `PERF sections` line: the `GT3R_PROF("name")` scopes (`base/prof.h`, Vita only) in ms a frame,
  the largest first, and the audio thread's mixing as a share of the time.
- **Uniform uploads**: `replay` skips a block whose bytes equal the program's last upload of it
  (vitaGL keeps each program's uniforms and sends them again itself on a program change).
- **Cores**: the main thread runs on the first user core alone, SDL's audio thread on the second.
- **The program's tables behind the menus**: the first race on exact physics read the program
  (`CORE.GT3` decoded and its tables worked out) for 15 s of its load. The main menu now starts
  that on a thread on the third core as soon as it is up (`race::prefetch_exact_program_data`).
  The race then finds it done, or waits for the rest of it. The log line
  `Race: Gran Turismo 3 physics (X s to read the program)` gives the time it took.
- **Two profiles on the Vita**: Performance and Max performance (Quality and Balanced are not
  built there). A fresh install starts on Max performance (`video_default`, the render scale's
  default included) with a 30 fps frame rate limit (`App::fps_lock`'s Vita default). The Vita's Performance turns the dynamic resolution on: 75%, lowered in 5%
  steps down to 50% when the GPU falls behind.
- **Dynamic resolution on the Vita**: its target is the frame rate limit's frame time (33 ms at
  30 fps; it aimed at 60 fps everywhere, so a capped frame looked late). A frame counts as
  GPU-bound by the time spent waiting for the GPU: elsewhere that is in the present, on the Vita
  at the passes' first clear, where vitaGL begins the GPU scene (`gles::take_gpu_wait_ms`, added
  to the present's time).
- **The mirror every other frame**: the rear-view mirror (in-car views; a second view of the
  course) is drawn on alternate frames and its last picture shown in between
  (`View3D::show_last`); a mirror not shown the frame before is drawn afresh.
- **Anti-aliasing (MSAA)**: Off, 2x or 4x on the Display page. vitaGL is built with
  `HAVE_ACCURATE_MSAA`: a multisampled pass draws into its resolve texture attached with
  `glFramebufferTexture2DMultisample` (`vita_msaa_fbo`); vitaGL gives that frame buffer a render
  target and depth buffer with the samples, and GXM averages them as it stores each tile. The
  multisampled colour texture has no storage (`vita_samples`), there is no resolve blit, and the
  reflection cube's faces stay single-sample. Off by default (`platform::default_msaa`): it costs
  GPU time and memory.
- **The race step on two cores**: measured on Trial Mountain with Performance, a race frame was
  ~50 ms, all of it on the main thread: drawing ~40 (`v3d.encode` ~15, vitaGL's `gl.replay` ~20,
  which must stay on the thread with the GL context) and the physics ~13.5 (`race.tick`, 3-4
  steps a frame). The step's per-car phases that touch only their own car (steering, slip,
  aids, dynamics, brake heat, the feedback; the chassis step, its effects then emitted in the
  cars' order) go through `sim::exact::set_car_pool`: on the Vita a `WorkerPool` with one
  worker on the third core shares the cars with the main thread. The results are the same as in
  turn: each car's work reads and writes its own `Car` (plain data) and constant race data
  (`CourseGround` has no mutable state; the scratch is `thread_local`), and the worker runs with
  the main thread's FPSCR (the fast physics' rounding). The AI's planning, the collisions, the
  random register's draws and the race flow stay in order on the main thread. Not on the second
  core: SDL's audio thread mixes there, and a worker it preempts mid-car would hold the step up.
- **A cheaper mirror**: drawn, it cost 20-25 ms (9-12 ms a frame at every other frame), half
  of the main view for a strip ~64 lines tall, as the frame rate follows the draw calls. On the
  Vita its scenery's small objects go at the size they have there (`min_size` six times the
  main view's, at least 0.015), the cars behind take their simpler models six times nearer
  (`race::car_lod_scale` for its collection), and it has no grading pass of its own.
- **Small far objects at the dynamic resolution's lower steps**: the main view's `min_size`
  (0.003 for Performance's 75%, 0.005 for Max performance's 50%) keeps the same size in pixels
  when the resolution drops below that.
- **The animated course nodes assembled on their worker**: their vertex and index lists
  (`GpuModel::assemble_geometry`) are built on the third core with the nodes themselves; the
  main thread only uploads them (`update_geometry(Assembled&&)`; ~2-3 ms a frame, in spikes
  at the reduced animation's six updates a second).
- **Draw calls in the log**: the PERF line gives the main view's draw calls a frame (`draws`)
  and the mirror's when drawn (`mirror`); a view's stats add up over its passes of a frame.
- **Draws without discards** (`PipelineKey::opaque`): `model.frag`'s PLAIN variant can discard
  for three things only (the alpha test's kill, the dithered fade, the vertices' drop past GT3's
  far plane), and on the PowerVR GPU a shader that can discard turns hidden-surface removal off
  for its draws, so every layer of the scenery was shaded (GPU waits of 5-20 ms a frame at 50%).
  A plain draw that is opaque and depth-writing, without the alpha test or a fade, of a mesh
  wholly nearer than the far plane (`Visible::inside_far`, the culling's measure) uses the
  variant built with `NO_DISCARD` (`GpuContext::fs_opaque_`; without it, the plain one).
- **The race's steps on their own core** (`[race] physics_thread`, Settings "Physics on its own
  core", on by default): in a live run (not online, replays or the licence results) `update`
  leaves the frame's steps pending (`async_ticks_`) and `render_drive` runs them on an
  `AsyncJob` on the third core while the main view is drawn (`start_async_ticks` just before
  `View3D::draw`, `join_async_ticks` after: everything the view shows is collected before; the
  mirror, the second view and the HUD read the race after). The picture shows the race as the
  steps before left it, a frame later. A frame that draws no main view runs them in the next
  update first; a restart, a new field and the screen's end join and drop them. `tick` touches
  no GPU (its visual updates, smoke, pylons and car looks, are CPU state uploaded when the items
  are collected); the GL layer logs an error if it is ever called off the main thread, and the
  profiler counts the main thread only (`prof::set_thread_off`). The per-car helper moved to
  the second core, with SDL's audio.
- **Music decoded ahead** (`audio/prefetch_music.h`): the disc's music streams straight from the
  disc image (`AdsStream` over `Volume::read_range`), and that read and the decoding ran in the
  audio thread's callback, which mixes the race at ~90% of its core: a slow read of the memory
  card ran the output dry and the music crackled. On the Vita every piece goes through
  `PrefetchMusic`: a decoder thread on the third core (low priority) keeps ~1.5 s decoded
  (32 blocks of 2,048 frames), the callback only copies, and the position it reports is the
  played one, per block (the music cues). Behind, it plays silence rather than ending. A piece
  that is replaced or stopped goes after the audio locks are let go (its decoder joins), still
  before the disc it reads can close.
- **The voices mixed on two cores**: with the music decoded ahead the audio thread was still
  ~90% busy in races (the ~46 voices' SPU2 work, at half rate), and its busiest moments still
  crackled. `VoiceMixer::render` now renders voice by voice through the block's passes
  (`render_voices`, into per-pass bus sums), then runs the output stage (the bus gains, the
  half-rate pairs, the reverb) as before; on the Vita a helper thread on the third core, at the
  audio thread's priority, takes the voices after the middle of the playing ones meanwhile, and
  its sums are added in. Each voice's work is its own and the sums are whole numbers, so the
  output is the same to the bit (reviewed against the old loop; no host compiler here to test).
- **Each car's second decode alongside the first**: `CarVisual` decodes the car twice (the body,
  then the brake lamps' lit variant: a full `load_car_model` each, ~2 s a car in a race's load).
  The lit one now runs on a loader `AsyncJob` on the third core while the main thread decodes
  the body; it is joined before its result is used (and before the constructor's locals go, on
  any exit), and an error in it is rethrown there. The decoder has no shared mutable state (the
  texture pack's cache is locked; the stand-in rim, which reads the disc, is the wheels' only).
- **Link-time optimisation** (`GT3R_LTO`, on): the engine and the game are optimised as one
  program (`-flto`), so the draw loop's calls into the OpenGL ES layer and the race step's into
  the physics inline; the eboot went from 8.77 MB to 7.97 MB. The Vita's FPU (VFPv3) has no fused
  multiply-add, so it changes no float result. The printf wrappers (`--wrap`) survive it.
- **Per-draw work cut** (thousands of primitives a frame): `encode` sets an item's lights once
  (not for every primitive, with a `normalize`), skips building and comparing the vertex-lit
  block when the item, material, sheen and 2x colour are those of the block pushed last
  (`LitKey`), and keeps its visible and blended lists' room from pass to pass; the replay looks a
  program's uniform locations up when the program changes, not with a map search every draw.
- **The mirror tip**: the Vita's Performance description suggests turning the mirror off for
  more frames; the mirror on or off keeps the profile there (`performance_profile` skips it).
- **Fewer draws on Performance** (the lap: 1,600-2,900 a frame in the main view, ~15 us each,
  2,600 ~15 fps and 1,100 ~20): `car_detail = normal` takes the simpler car models from half
  GT3's distances on the Vita (the starting grid's full models), Performance's `min_size` is
  0.004 there (0.006 at 50%), and the mirror is drawn every third frame, to 80 m, with its
  threshold seven times the main view's (at least 0.02) and its cars' models eight times nearer.
- **Opponents' models built once a race**: a race's load built the field two or three times (the
  start, `restart()`, the pre-race menu's `build_vehicle`). Each time it read and uploaded all the
  opponents' models again, ~2.3 s a car on the Vita: 22–33 s of a 45–53 s load. The screen now keeps
  them by model and paint (`opp_models_`). The next field reuses a match and frees the rest before
  loading new ones (`load_opponent_cars`). In the log: one `Car xx0000: lamps …` line per opponent.

- **The music's position without locks** (the sections' first find: `race.camera_manager` took
  122 of the 211 ms): the replay director's cut clock steps 60 times a second (12 steps in a
  frame at 5 fps) and each asked `AudioEngine::music_audible_position`, which took `mutex_`, the
  music stream's lock and the device's, all held by the audio thread through its mixes (busy
  ~76% of the time). `feed()` now leaves the position in atomics and the main thread reads
  them; `music_playing()` reads an atomic too, and `set_stream_level` takes no lock when the
  level is unchanged.
- **No guard-band clipping on the CPU** (`View3D::upload_clipped`, GT3's clipping of the
  triangles near the camera): the Vita leaves `guard_near` at 0 and the GPU clips.
- **Performance profiles** (Settings > Display, `App::set_performance_profile`): Quality,
  Balanced, Performance, Max performance, each a set of `[video]` values (car detail, showroom
  wheels, car reflections, car shadows, depth of field, replay effects, 3D resolution, dynamic
  resolution, draw distance, scenery animation); Custom when none matches. `App::video_setting`
  gives each key's effective value (config or platform default).
- **Frame rate limit** (`[video] fps_lock = off | 60 | 30 | 20`, `App::apply_frame_limit`): SDL's
  main loop rate on a whole number of display refreshes; it took over `limit_60fps = on` (60).
- **Draw distance** (`[video] draw_distance = 100 | 75 | 50`): scenery wholly beyond that share
  of GT3's far plane is culled (`DrawOptions::draw_distance`); the far plane still clips the rest.
- **Scenery animation** (`[video] scenery_animation = full | reduced`): reduced advances the
  course's CLUT animations and moving nodes in 1/6 s steps (`RaceScreen::animate_course`), so
  their recoloured textures (mips made on the CPU) and re-assembled geometry go up 6 times a
  second instead of every frame (`race.upload_animations`: 20 ms a frame at 13 fps).

- **Fast physics** (`[race] physics = exact | fast`, Settings > Display, Vita only; default exact):
  `ee::HostMathScope` around the race's vehicle step sets the FPU to round toward zero and flush
  denormals, and the `ee::F` operators (`base/ee_float.h`, `GT3R_EE_HOST_MATH`) use it: the EE's
  chopped add, subtract and multiply but for its adder's dropped bits and multiplier's lost carry,
  divide and square root without the divider's "one above". Exponent-255 operands, division by
  zero and overflows keep the EE's rules. Not bit for bit: the disc's recorded demonstrations and
  exact-mode replays can drift. The physics was the largest cost left: a race step takes 3.2–3.6
  ms on the grid and 6–8 ms racing (Trial Mountain, 6 cars), up to 32 ms a frame.
- **Smaller costs**: gamepads read once a frame (`InputManager::snapshot_pads`; two locked SDL
  calls a binding before, 4 ms a frame); the log written every 5 s and at once only for errors
  (each write to the memory card takes tens of ms); the race's pictures kept only where a
  dissolve or the trail can use them (`keep_last`, 3 ms a frame); `View3D::encode` keeps the last
  pipeline key's pipeline (no map search and mutex a primitive).

- **Live settings** (`RaceScreen::read_live_settings`, every race frame): physics, draw distance,
  scenery animation and the rear-view mirror (`[video] rear_mirror`, off in Max performance: the
  in-car mirror drew the course again, 19–22 ms a frame) change over a race at once. The first
  Fast test never ran Fast: the setting was changed over a race that had read it at its start.
- **Exact steps ~10% slower with the Fast paths inline** (7.3–9 ms a step against 6.4–7.6): the
  check is now `__builtin_expect`ed unlikely (`GT3R_EE_HOST_ON`), the FPU paths laid out of the way.
- **GLES recording**: a new command buffer reserves the largest arena and draw list seen
  (`arena_hint`, `draws_hint`: they grew from nothing every submit); on the Vita the arena's blocks
  sit on 16 bytes, not GL's 256-byte uniform buffer alignment (every vertex block took 512).

- **Fast physics measured** (Trial Mountain, Max performance, the same race switched mid-way):
  a race step 7.4 ms exact, 2.4–2.8 ms fast; the race 11 fps exact, 20–25 fast (27–29 at best).
  A fast frame (~40 ms): draws 13–18 (encode 5–9, GL 8–9), three steps 8, HUD 5, course
  animation uploads (reduced) 3.5–4, input 1.8, events 2.8, present 2.
- **Recoloured textures' levels on the GPU**: `Uploader::update_rgba` uploads level 0 and has
  vitaGL make the rest (`SDL_GenerateMipmapsForGPUTexture`: `sceGxmTransferDownscale`) instead of
  halving them on the CPU and uploading each. New textures keep their CPU-made levels.
- **SDL's quick parameter checks only** (`SDL_HINT_INVALID_PARAM_CHECKS = 1`, Vita): every SDL
  call looked its object up in a locked hash table (the HUD's 2D draws, gamepads, audio streams).
- Measured (Trial Mountain, Fast, Max performance): 24–30 fps (33–41 ms); HUD 1.5 ms (from 5),
  input 0.3, the recoloured textures gone from the sections; moving scenery 3–4.5 ms
  (`gpu.update_geometry` half of it).
- **Audio at half rate** (`VoiceMixer::render`, `kHalfRate`, Vita): the voices (~46 in a race,
  the SPU2's ADPCM, Gaussian taps and ADSR) took 96–97% of the audio core and the race's sound
  broke up. They are worked out once every two output samples, the first of each pair halfway from
  the pair before; envelopes and volume slews keep their per-sample rates, the reverb runs every
  sample. The callbacks' buffers are kept (no allocation, so no wait on the heap's lock).
- Measured with audio at half rate (Trial Mountain, Fast): races at a median 26 fps; the audio
  thread at ~90% while racing (the ADPCM decode runs at the source rate still), and the sound
  clean.
- **Moving nodes on a worker thread** (`CourseScene::moving_worker`, Vita, the third user core):
  the node programs and the parts they move (`AnimatedNodes::meshes`, ~13 ms an update, 6 a second
  with reduced animation: a hitch every fourth frame) run there; `upload_animations` uploads what
  it finished a frame later. While it works, `animate` holds the nodes' time back
  (`moving_pending_`), so the nodes never change under it; a run's restart waits for it.
- Measured with the worker (Trial Mountain, Fast, Max performance): a median 26.8 fps (p10 20, p90
  30); the moving nodes' upload ~2 ms; the frame rate follows the draw calls (500 a frame: 28 fps;
  1,000–1,265: 16–18 fps); the GPU begins to hold the CPU back (`gl.pass.begin`, the first clear of
  a pass: 4.5–7 ms a frame).
- **Small scenery culled** (`DrawOptions::min_size`, with draw distance under 100%): the course
  scenery's meshes under ~3 pixels across (radius / distance under 0.005 at 50%, 0.003 at 75%)
  are skipped; cars, sky and backdrop never are (only items with a node list).
- **Colour grading setting** (`[video] color_grading`, read every race frame, off in Max
  performance): GT3's per-course grade and the replay cameras' filters are a second full-screen
  pass (`View3D::post_wanted`) and another GPU scene a frame.
- Measured with small scenery culled: mid-race faster, but the start and the last stretch (Trial
  Mountain's start/finish straight: grandstands, pits, and at the start the whole grid in view)
  at 17–24 fps with 800–1,000 draw calls (draw recording ~15 ms, GL ~15 ms).
- **Car detail** (`[video] car_detail = normal | low`, read every race frame, low in Max
  performance): `race::car_lod`'s distances (30 and 80 m) times `set_car_lod_scale` (a third with
  low), so the cars round the player's take GT3's simpler models.
- Measured with car detail low (Trial Mountain, Max performance): a median 29.8 fps (p10 19, p90
  39); the audio thread back at 89–93% (its callbacks alone; SDL's resampling and mixing of the
  44.1 kHz music on top), and crackling.
- **Tried and taken back**: the reverb's 21 non-zero taps only, the nearest 3 opponents voiced
  (not 5), 2048-frame audio buffers (`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`) and vitaGL's garbage
  collector on the third core. The audio thread fell to 47–62%, but the sound was worse to the
  player's ear (likely the engine notes switching on and off as the cars change places, or the
  buffer size with SDL's Vita backend) and the race felt slower; all four were reverted (the build
  is the one before, byte for byte).
- **Settings on the Vita**: the Rumble & feedback, Touch and Online pages are left out (no wheels or
  feedback, no on-screen controls, no network), rows that do nothing there are removed on every
  page (fullscreen, MSAA, anisotropic filtering, 3D renderer, background controllers, Xbox pads,
  keyboard, audio output, music folder), and the Display page is grouped: Performance (profile,
  frame rate limit, physics, FPS counter), Picture, Details, Replays, Game.
- **`__sce_headroom`** (CMakeLists.txt): a build whose code ended within ~5 KB of a 64 KB
  boundary failed in vita-elf-create ("segment 1 overlaps"); 16 KB of headroom is reserved.

Measured with Max performance (Trial Mountain): course animation uploads 5.6 ms (from 20),
draw recording 13–33 ms and GL 9–22 ms with 100–176 meshes, the race step as above, input 4 ms,
HUD 4 ms, the picture copy 3 ms; the audio thread at 76–97% of its core (on its own core: the HUD
and input took the same time at 76% and 96%, so it does not slow the main thread).

Measured after the music fix (Super Speedway, the user's settings: 50% resolution, Custom):
75–79 ms a frame (12.6–13.5 fps): course animation uploads 20 ms, draw recording 13–16 ms and GL
10 ms, the race's 4 steps 14 ms, the picture kept for dissolves 2.7 ms (`copy_picture`), input
3.7 ms, HUD 3.5 ms; the cockpit view's mirror 15 ms more.

Measured (Super Speedway, 6 cars, the build before the sections): about 211 ms a frame (4–5
fps) at 100% and at 50% resolution alike, so the CPU, not the GPU: update about 140 ms, of which
the race's 4 steps took only 16 ms (3.5 ms a step for 6 cars); render about 63 ms (draw calls 36
ms for 600–1,200 draws); events 7 ms; present 2 ms.

## Performance ideas

- The exact physics reproduces the EE's rounding toward zero in software on every operation
  (`base/ee_float.h`, `base/ps2_float.cpp`). The Cortex-A9's VFP has a round-toward-zero mode
  and flush-to-zero, so a Vita-only fast path could use the hardware, at the cost of replays and
  online races no longer matching other platforms bit for bit.
- Clock the CPU at 444 MHz and the GPU at 222 MHz (`scePowerSetArmClockFrequency`,
  `scePowerSetGpuClockFrequency`).
