#pragma once

// A few worker threads that share out the items of a job with the calling thread (WorkerPool::run): each item
// once, in any order, the caller back when every one has run. For work whose items are independent of each other
// (the race step's per-car phases: sim::exact::set_car_pool, on the PlayStation Vita's third core). The workers
// sleep between jobs.

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <atomic>
#include <cstddef>
#include <vector>

namespace gt3r {

class WorkerPool {
public:
    using Fn = void (*)(void* ctx, size_t i);
    // A worker for each entry of `cores`: the core it runs on (the Vita's user cores 0-2; -1: any).
    explicit WorkerPool(const std::vector<int>& cores);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // fn(ctx, i) for every i in [0, n), on the workers and the calling thread; back when all have run (without
    // workers, or for one item: in turn here). The workers run the job with the caller's floating-point state (the
    // Vita's FPSCR, which the race's fast physics sets to round toward zero).
    void run(size_t n, Fn fn, void* ctx);
    size_t workers() const { return threads_.size(); }

private:
    static int SDLCALL thread_main(void* self);
    void work();

    std::vector<SDL_Thread*> threads_;
    std::vector<int> cores_;
    SDL_Semaphore* wake_ = nullptr; // a post a worker for each job (run waits until every post is taken)
    std::atomic<bool> quit_{false};
    std::atomic<int> started_{0};   // (each worker's index into cores_)
    // The job: written before the posts, read by the workers until pending_ is back to 0.
    Fn fn_ = nullptr;
    void* ctx_ = nullptr;
    size_t n_ = 0;
    unsigned fpscr_ = 0;
    std::atomic<size_t> next_{0}; // the next item to take
    std::atomic<int> pending_{0}; // posts not yet taken and worked off
    std::atomic<bool> busy_{false}; // a job is running (run is for one caller at a time)
};

} // namespace gt3r
