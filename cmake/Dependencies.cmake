# Third-party dependencies. A system/package-manager install is preferred;
# otherwise the dependency is fetched and built from source so that Windows,
# Linux, macOS and Android builds all work without manual setup.

include(FetchContent)

# --- libchdr: read compressed DVD/CD disc images --------------------------
FetchContent_Declare(libchdr
  GIT_REPOSITORY https://github.com/rtissera/libchdr.git
  GIT_TAG v0.3.0
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)
if(CMAKE_VERSION VERSION_LESS 3.28)
  # FetchContent's EXCLUDE_FROM_ALL option was added in 3.28.
  FetchContent_GetProperties(libchdr)
  if(NOT libchdr_POPULATED)
    FetchContent_Populate(libchdr)
    add_subdirectory(${libchdr_SOURCE_DIR} ${libchdr_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
else()
  FetchContent_MakeAvailable(libchdr)
endif()
# PlayStation Vita: libchdr builds itself and its codecs position-independent (for shared
# libraries), and the Vita's executable format takes no GOT-relative relocations
# (vita-elf-create: "Invalid relocation type 25").
if(GT3R_VITA)
  foreach(_t chdr-static chdr-lzma miniz zstd)
    if(TARGET ${_t})
      set_target_properties(${_t} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    endif()
  endforeach()
endif()

# --- SDL3: windowing, input (gamepads, wheels, sensors), audio, GPU ---------
# Release packages build SDL from source (GT3R_FETCH_SDL) so they don't depend
# on a system or Homebrew SDL; Windows packages use SDL3.dll (GT3R_SDL_SHARED).
option(GT3R_FETCH_SDL "Always build SDL3 from source instead of using an installed one" OFF)
option(GT3R_SDL_SHARED "Build the fetched SDL3 as a shared library" OFF)
# VR headsets (docs/dev/VR.md): SDL_GPU's OpenXR support arrives with SDL 3.6, so these
# builds take SDL from its development branch at a pinned commit (the Quest APK's
# android/fetch-sdl.sh --xr fetches the same one); every other build stays on 3.4.
option(GT3R_XR "OpenXR mode for VR headsets (Meta Quest, PC VR); needs SDL 3.6 with SDL_GPU OpenXR" OFF)
set(GT3R_SDL_TAG release-3.4.12)
set(GT3R_SDL_SHALLOW TRUE)
if(GT3R_XR)
  set(GT3R_SDL_TAG c17963b8ff64ebd51cbc0bac7389dd48a57c1786) # SDL main, 2026-10-18
  set(GT3R_SDL_SHALLOW FALSE) # a shallow clone has only the branch tip; a pinned commit needs history
  set(GT3R_FETCH_SDL ON)
  set(SDL_GPU_OPENXR ON CACHE BOOL "" FORCE)
endif()
# iOS: an installed SDL3 (Homebrew's) is built for macOS, so the device build always makes its own.
if(IOS)
  set(GT3R_FETCH_SDL ON)
endif()
# PlayStation Vita: VitaSDK's SDL3 has no OpenGL, and SDL's own Vita GL backends stop at OpenGL ES
# 2.0, so SDL is built here with a vitaGL backend (patches/sdl-vita-vitagl.patch, docs/dev/VITA.md).
# SDL's CMake knows the Vita by VITA, which VitaSDK's toolchain file does not set.
if(GT3R_VITA)
  set(GT3R_FETCH_SDL ON)
  set(VITA ON)
  set(VIDEO_VITA_VITAGL ON CACHE BOOL "" FORCE)
endif()
if(NOT GT3R_FETCH_SDL)
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT SDL3_FOUND)
  message(STATUS "Building SDL3 from source")
  set(SDL_SHARED ${GT3R_SDL_SHARED} CACHE BOOL "" FORCE)
  if(GT3R_SDL_SHARED)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  else()
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  # Our fixes to SDL (cmake/patches; the VR builds' pinned commit has its own copy). A source tree
  # given with FETCHCONTENT_SOURCE_DIR_SDL3 is used as it is.
  find_package(Git REQUIRED)
  set(_sdl_patch sdl-vulkan-amd-fullscreen.patch)
  if(GT3R_XR)
    set(_sdl_patch sdl-main-vulkan-amd-fullscreen.patch)
  endif()
  set(_sdl_patches ${CMAKE_CURRENT_LIST_DIR}/patches/${_sdl_patch})
  if(GT3R_VITA)
    list(APPEND _sdl_patches ${CMAKE_CURRENT_LIST_DIR}/patches/sdl-vita-vitagl.patch)
  endif()
  list(JOIN _sdl_patches "|" _sdl_patch_list) # ApplyPatches.cmake: '|' survives PATCH_COMMAND
  set(GT3R_SDL_PATCH PATCH_COMMAND "${CMAKE_COMMAND}" "-DGIT=${GIT_EXECUTABLE}"
      "-DPATCHES=${_sdl_patch_list}"
      -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatches.cmake")
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${GT3R_SDL_TAG}
    GIT_SHALLOW ${GT3R_SDL_SHALLOW}
    ${GT3R_SDL_PATCH})
  FetchContent_MakeAvailable(SDL3)
  if(GT3R_LIBTOOL_QUIET AND TARGET SDL3-static)
    set_target_properties(SDL3-static PROPERTIES STATIC_LIBRARY_OPTIONS ${GT3R_LIBTOOL_QUIET})
  endif()
endif()
if(GT3R_XR)
  # The OpenXR headers SDL builds against (it vendors them; they are not installed).
  if(NOT EXISTS ${sdl3_SOURCE_DIR}/include/SDL3/SDL_openxr.h OR
     NOT EXISTS ${sdl3_SOURCE_DIR}/src/video/khronos/openxr/openxr.h)
    message(FATAL_ERROR "GT3R_XR needs SDL 3.6 with OpenXR (got ${sdl3_SOURCE_DIR}); "
                        "on Android run android/fetch-sdl.sh --xr")
  endif()
  set(GT3R_OPENXR_INCLUDE_DIR ${sdl3_SOURCE_DIR}/src/video/khronos)
  # Windows: the Khronos OpenXR loader, which SDL opens when the game starts (no loader, no
  # headset: the window); it ships next to gt3r.exe. Keep it at the OpenXR version of the headers
  # SDL builds with, like the Quest APK's (android/app/build.gradle). Its NuGet package is a zip.
  if(WIN32)
    set(_openxr ${CMAKE_BINARY_DIR}/_deps/openxr_loader)
    file(DOWNLOAD https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.65/OpenXR.Loader.1.1.65.nupkg
         ${_openxr}/loader.zip
         EXPECTED_HASH SHA256=05638c526f07ba3a5f1084bbc3308d654d5167f844ffa1e35515325cb72e0c34)
    file(ARCHIVE_EXTRACT INPUT ${_openxr}/loader.zip DESTINATION ${_openxr}
         PATTERNS native/x64/release/bin/openxr_loader.dll)
    set(GT3R_OPENXR_LOADER ${_openxr}/native/x64/release/bin/openxr_loader.dll)
  endif()
endif()

# --- vitaGL (PlayStation Vita): built here instead of VitaSDK's package ---------
# The package has no uniform blocks in its GLSL translator, which every shader here uses, and no
# error handling, so a shader it cannot compile crashes in glLinkProgram instead of failing the
# link. HAVE_GLSL_UBOS: uniform blocks. LOG_ERRORS, HAVE_SHARK_LOG: its errors and the shader
# compiler's go to gt3r.log (patches/vitagl-log.patch: vgl_log -> gt3r_vgl_log in
# src/app/vita_process.cpp). HAVE_GC_ON_FINISH: glFinish also frees what its garbage collector holds back
# for 4 frames (a load, with no frames between, ran out of memory with the menus' freed models still
# held). HAVE_ACCURATE_MSAA: multisampled frame buffers (glFramebufferTexture2DMultisample), the
# anti-aliasing (src/render/gpu_gles.cpp vita_msaa_fbo). The game links it ahead of VitaSDK's (CMakeLists.txt).
if(GT3R_VITA)
  include(ExternalProject)
  find_package(Git REQUIRED)
  ExternalProject_Add(vitagl
    GIT_REPOSITORY https://github.com/Rinnegatamante/vitaGL.git
    GIT_TAG f33b63c7466f89c94febf3ed8e4e76179f0b692a
    # vitagl-packed-offsets.patch: interleaved attributes from a buffer past 64 KB wrapped the
    # 16-bit SceGxmVertexAttribute.offset (meshes far into a buffer read the wrong vertices).
    # vitagl-mip-stride.patch: glTexSubImage2D into a mip level of a recently drawn texture
    # wrote its rows at an uninitialised stride. vitagl-reserve-checks.patch: the GXM uniform reserves
    # of the clear, the scissor set-up and the blit unchecked (no scene begun: a crash at a race's end).
    PATCH_COMMAND "${CMAKE_COMMAND}" "-DGIT=${GIT_EXECUTABLE}"
                  "-DPATCHES=${CMAKE_CURRENT_LIST_DIR}/patches/vitagl-log.patch|${CMAKE_CURRENT_LIST_DIR}/patches/vitagl-packed-offsets.patch|${CMAKE_CURRENT_LIST_DIR}/patches/vitagl-mip-stride.patch|${CMAKE_CURRENT_LIST_DIR}/patches/vitagl-reserve-checks.patch"
                  -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatches.cmake"
    UPDATE_DISCONNECTED TRUE
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE TRUE
    BUILD_COMMAND $(MAKE) HAVE_GLSL_UBOS=1 LOG_ERRORS=1 HAVE_SHARK_LOG=1 HAVE_GC_ON_FINISH=1 HAVE_ACCURATE_MSAA=1
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/libvitaGL.a)
  ExternalProject_Get_Property(vitagl SOURCE_DIR)
  set(GT3R_VITAGL_DIR ${SOURCE_DIR})
endif()

# --- zlib: GT3 stores compressed files as gzip / raw deflate ---------------
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
  message(STATUS "zlib not found on system; fetching")
  set(SKIP_INSTALL_ALL ON) # a static zlib is linked in; don't install its files into packages
  # zlib's example programs register CTest tests ("example", "minigzip") that our build never
  # builds, so ctest reported them as failed ("Not Run") on Windows CI.
  set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(zlib)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
  target_include_directories(zlibstatic INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()
