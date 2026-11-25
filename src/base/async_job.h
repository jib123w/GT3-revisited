#pragma once

// One job at a time on a thread of its own (pinned to a core on the PlayStation Vita): start() hands it a function,
// join() waits until that has returned. The race's steps run on it while the main thread draws the frame
// (race_screen::RaceScreen, [race] physics_thread). The thread sleeps between jobs.

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <functional>

namespace gt3r {

class AsyncJob {
public:
    // `core`: the Vita's user core (0-2) the thread runs on; -1: any.
    explicit AsyncJob(int core);
    ~AsyncJob();
    AsyncJob(const AsyncJob&) = delete;
    AsyncJob& operator=(const AsyncJob&) = delete;

    bool ok() const { return thread_ != nullptr; }
    // Runs `fn` on the thread (after the job before it, if any, is joined). Without a thread: here, at once.
    void start(std::function<void()> fn);
    // Waits for the job started last; nothing when none is running.
    void join();
    bool running() const { return running_; }

private:
    static int SDLCALL thread_main(void* self);
    void work();

    int core_;
    SDL_Thread* thread_ = nullptr;
    SDL_Semaphore *wake_ = nullptr, *done_ = nullptr;
    std::function<void()> fn_;
    bool running_ = false; // (the caller's: a job started and not yet joined)
    bool quit_ = false;
};

} // namespace gt3r
