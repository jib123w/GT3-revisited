#pragma once

// Frame section timer for the PlayStation Vita's PERF lines (App::iterate, docs/dev/VITA.md):
// GT3R_PROF("name") times the rest of the enclosing scope and adds it to that name's total; every
// 2 s the totals go to gt3r.log as milliseconds a frame, the largest first. Main thread only (the
// audio thread counts its mixing in audio_ns). Nothing on other platforms.

#include <SDL3/SDL_timer.h>

#include <atomic>
#include <cstring>

namespace gt3r::prof {

struct Slot {
    const char* name = nullptr;
    double ms = 0;
    int calls = 0;
};
inline constexpr int kSlots = 64;
inline Slot slots[kSlots];
inline int used = 0;
// The audio thread's mixing time (AudioEngine's stream callbacks), in nanoseconds.
inline std::atomic<Uint64> audio_ns{0};

inline int slot(const char* name) {
    for (int i = 0; i < used; ++i)
        if (std::strcmp(slots[i].name, name) == 0) return i;
    if (used == kSlots) return -1;
    slots[used].name = name;
    return used++;
}

// Threads whose sections are not counted (set_thread_off): the slots are the main thread's, unsynchronised (the race's
// steps on their own thread, base/async_job.h).
inline thread_local bool thread_off = false;
inline void set_thread_off() { thread_off = true; }

class Scope {
public:
    Scope(int& index, const char* name) : t0_(SDL_GetTicksNS()) {
        if (thread_off) return;
        if (index < 0) index = slot(name);
        index_ = index;
    }
    ~Scope() {
        if (index_ < 0) return;
        slots[index_].ms += double(SDL_GetTicksNS() - t0_) / 1e6;
        ++slots[index_].calls;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Uint64 t0_;
    int index_ = -1;
};

class AudioScope {
public:
    AudioScope() : t0_(SDL_GetTicksNS()) {}
    ~AudioScope() { audio_ns.fetch_add(SDL_GetTicksNS() - t0_, std::memory_order_relaxed); }
    AudioScope(const AudioScope&) = delete;
    AudioScope& operator=(const AudioScope&) = delete;

private:
    Uint64 t0_;
};

} // namespace gt3r::prof

#if defined(__vita__)
#define GT3R_PROF_CAT2(a, b) a##b
#define GT3R_PROF_CAT(a, b) GT3R_PROF_CAT2(a, b)
#define GT3R_PROF(name)                                                                                                \
    static int GT3R_PROF_CAT(gt3r_prof_index_, __LINE__) = -1;                                                         \
    ::gt3r::prof::Scope GT3R_PROF_CAT(gt3r_prof_scope_, __LINE__)(GT3R_PROF_CAT(gt3r_prof_index_, __LINE__), name)
#define GT3R_PROF_AUDIO() ::gt3r::prof::AudioScope gt3r_prof_audio_scope_
#else
#define GT3R_PROF(name) ((void)0)
#define GT3R_PROF_AUDIO() ((void)0)
#endif
