// A MusicStream decoded ahead on a thread of its own; see prefetch_music.h.

#include "audio/prefetch_music.h"

#include "base/log.h"
#include "base/prof.h"

#include <SDL3/SDL_properties.h>

#include <algorithm>
#include <cstring>
#include <exception>

#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#endif

namespace gt3r {

PrefetchMusic::PrefetchMusic(std::unique_ptr<MusicStream> inner, int core) : inner_(std::move(inner)), core_(core) {
    rate_ = inner_->sample_rate(), ch_ = inner_->channels(), total_ = inner_->total_frames();
    position_ = inner_->position_seconds();
    blocks_.resize(kBlocks);
    for (Block& b : blocks_) b.data.resize(kBlockFrames * std::max<u32>(ch_, 1));
    // The first blocks now, so the piece starts without a gap; the rest on the decoder.
    for (int i = 0; i < 2 && decode_one(); ++i) {
    }
    if (ended_.load()) return; // (a short piece: all decoded already)
    space_ = SDL_CreateSemaphore(0);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&thread_main));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "music decoder");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 256 * 1024);
    if (space_) thread_ = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!thread_) LOG_WARN("Music: no decoder thread (%s): the piece plays what was decoded", SDL_GetError());
}

PrefetchMusic::~PrefetchMusic() {
    quit_.store(true);
    if (thread_) {
        SDL_SignalSemaphore(space_);
        SDL_WaitThread(thread_, nullptr);
    }
    if (space_) SDL_DestroySemaphore(space_);
}

int SDLCALL PrefetchMusic::thread_main(void* self) {
    static_cast<PrefetchMusic*>(self)->decode_loop();
    return 0;
}

bool PrefetchMusic::decode_one() {
    Block& b = blocks_[write_];
    size_t got = 0;
    try {
        got = inner_->read(b.data.data(), kBlockFrames);
    } catch (const std::exception& e) {
        LOG_ERROR("Music stream error: %s", e.what());
        got = 0;
    }
    if (got == 0) {
        ended_.store(true, std::memory_order_release);
        return false;
    }
    b.frames = got;
    b.pos_after = inner_->position_seconds();
    write_ = (write_ + 1) % kBlocks;
    filled_.fetch_add(1, std::memory_order_release); // (the block's samples are out before it counts)
    return true;
}

void PrefetchMusic::decode_loop() {
#if defined(__vita__)
    if (core_ >= 0 && core_ <= 2)
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << core_);
#endif
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW); // (1.5 s ahead: whatever the core has left will do)
    prof::set_thread_off();
    while (!quit_.load()) {
        if (filled_.load(std::memory_order_acquire) >= kBlocks) {
            SDL_WaitSemaphoreTimeout(space_, 100);
            continue;
        }
        if (!decode_one()) break; // the end: the player plays out what is left
    }
}

size_t PrefetchMusic::read(s16* out, size_t frames) {
    size_t done = 0;
    while (done < frames) {
        if (filled_.load(std::memory_order_acquire) == 0) {
            // The end once the decoder has said so and nothing is left (looked at again: a last block may have come
            // between the two loads).
            if (ended_.load(std::memory_order_acquire) && filled_.load(std::memory_order_acquire) == 0) break;
            if (filled_.load(std::memory_order_acquire) > 0) continue;
            // Behind (never expected, 1.5 s ahead): silence for the rest of this call, the stream goes on.
            std::fill(out + done * ch_, out + frames * ch_, s16(0));
            return frames;
        }
        Block& b = blocks_[read_];
        const size_t n = std::min(frames - done, b.frames - offset_);
        std::memcpy(out + done * ch_, b.data.data() + offset_ * ch_, n * ch_ * sizeof(s16));
        done += n, offset_ += n;
        position_ = b.pos_after - (rate_ ? double(b.frames - offset_) / rate_ : 0.0);
        if (offset_ == b.frames) {
            offset_ = 0;
            read_ = (read_ + 1) % kBlocks;
            filled_.fetch_sub(1, std::memory_order_acq_rel);
            if (space_) SDL_SignalSemaphore(space_);
        }
    }
    return done;
}

} // namespace gt3r
