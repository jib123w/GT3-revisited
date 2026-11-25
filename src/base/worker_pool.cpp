// A few worker threads sharing out a job's items with the caller; see worker_pool.h.

#include "base/worker_pool.h"

#include <SDL3/SDL_properties.h>

#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#endif

namespace gt3r {

namespace {

unsigned read_fpscr() {
#if defined(__arm__)
    unsigned v = 0;
    asm volatile("vmrs %0, fpscr" : "=r"(v));
    return v;
#else
    return 0;
#endif
}

void write_fpscr(unsigned v) {
#if defined(__arm__)
    asm volatile("vmsr fpscr, %0" : : "r"(v) : "memory");
#else
    (void)v;
#endif
}

void cpu_relax() {
#if defined(__arm__)
    asm volatile("yield");
#endif
}

} // namespace

WorkerPool::WorkerPool(const std::vector<int>& cores) : cores_(cores) {
    if (cores.empty() || !(wake_ = SDL_CreateSemaphore(0))) return;
    for (size_t k = 0; k < cores.size(); ++k) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&thread_main));
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "worker");
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 512 * 1024); // (the race step's depth)
        if (SDL_Thread* t = SDL_CreateThreadWithProperties(props)) threads_.push_back(t);
        SDL_DestroyProperties(props);
    }
}

WorkerPool::~WorkerPool() {
    quit_.store(true);
    for (size_t k = 0; k < threads_.size(); ++k) SDL_SignalSemaphore(wake_);
    for (SDL_Thread* t : threads_) SDL_WaitThread(t, nullptr);
    if (wake_) SDL_DestroySemaphore(wake_);
}

int SDLCALL WorkerPool::thread_main(void* self) {
    static_cast<WorkerPool*>(self)->work();
    return 0;
}

void WorkerPool::work() {
    const int k = started_.fetch_add(1);
#if defined(__vita__)
    if (size_t(k) < cores_.size() && cores_[size_t(k)] >= 0 && cores_[size_t(k)] <= 2)
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << cores_[size_t(k)]);
#else
    (void)k;
#endif
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH); // (ahead of the core's other work: the caller waits)
    for (;;) {
        SDL_WaitSemaphore(wake_);
        if (quit_.load()) return;
        const unsigned saved = read_fpscr();
        write_fpscr(fpscr_);
        for (size_t i; (i = next_.fetch_add(1, std::memory_order_acq_rel)) < n_;) fn_(ctx_, i);
        write_fpscr(saved);
        pending_.fetch_sub(1, std::memory_order_release); // (its items' results with it)
    }
}

void WorkerPool::run(size_t n, Fn fn, void* ctx) {
    const int w = int(threads_.size());
    // (One job at a time: a caller while another's runs works through its own items.)
    if (w == 0 || n < 2 || busy_.exchange(true, std::memory_order_acquire)) {
        for (size_t i = 0; i < n; ++i) fn(ctx, i);
        return;
    }
    struct Release {
        std::atomic<bool>& b;
        ~Release() { b.store(false, std::memory_order_release); }
    } release{busy_};
    fn_ = fn, ctx_ = ctx, n_ = n, fpscr_ = read_fpscr();
    pending_.store(w, std::memory_order_relaxed);
    next_.store(0, std::memory_order_release);
    for (int k = 0; k < w; ++k) SDL_SignalSemaphore(wake_); // (a system call: the job's fields are out before it)
    for (size_t i; (i = next_.fetch_add(1, std::memory_order_acq_rel)) < n;) fn(ctx, i);
    // Every item is taken; the workers' are done once each post is worked off (a worker that wakes late finds none
    // left, and the next job's fields are only written after it has let go of these).
    while (pending_.load(std::memory_order_acquire) != 0) cpu_relax();
}

} // namespace gt3r
