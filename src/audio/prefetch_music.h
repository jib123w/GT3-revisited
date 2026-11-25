#pragma once

// A MusicStream decoded ahead on a thread of its own: the stream's reads (the game's music streams straight from the
// disc image) and its decoding run there, ~1.5 s ahead, and the audio thread only copies from memory. On the
// PlayStation Vita the music crackled in races: the audio thread, ~90% busy mixing the race, also read the disc image
// on the memory card for the music, and a slow read ran the output dry. Should the decoder ever fall behind, the
// stream plays silence until it catches up instead of ending.

#include "audio/music_stream.h"

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <atomic>
#include <memory>
#include <vector>

namespace gt3r {

class PrefetchMusic final : public MusicStream {
public:
    // `core`: the Vita's user core (0-2) the decoder runs on; -1: any. The first blocks are decoded here, at once.
    explicit PrefetchMusic(std::unique_ptr<MusicStream> inner, int core = -1);
    ~PrefetchMusic() override;
    PrefetchMusic(const PrefetchMusic&) = delete;
    PrefetchMusic& operator=(const PrefetchMusic&) = delete;

    u32 sample_rate() const override { return rate_; }
    u32 channels() const override { return ch_; }
    u64 total_frames() const override { return total_; }
    // (The audio thread's: copies what the decoder has ready, never waits on it.)
    size_t read(s16* out, size_t frames) override;
    // Where the played music is (not the decoder, which is ahead).
    double position_seconds() const override { return position_; }

private:
    static constexpr size_t kBlockFrames = 2048, kBlocks = 32; // (~1.5 s at 44.1 kHz)
    struct Block {
        std::vector<s16> data;
        size_t frames = 0;
        double pos_after = 0; // the stream's position after the block
    };
    static int SDLCALL thread_main(void* self);
    bool decode_one();        // the next block (the decoder's); false at the stream's end
    void decode_loop();

    std::unique_ptr<MusicStream> inner_;
    u32 rate_ = 0, ch_ = 0;
    u64 total_ = 0;
    int core_ = -1;
    std::vector<Block> blocks_;
    std::atomic<size_t> filled_{0}; // blocks decoded and not yet played
    size_t write_ = 0;              // the decoder's next block
    size_t read_ = 0, offset_ = 0;  // the player's block and frame in it
    double position_ = 0;
    std::atomic<bool> ended_{false}, quit_{false};
    SDL_Semaphore* space_ = nullptr; // posted as the player frees a block (and to quit)
    SDL_Thread* thread_ = nullptr;
};

} // namespace gt3r
