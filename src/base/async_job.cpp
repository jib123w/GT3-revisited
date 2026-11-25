// One job at a time on a thread of its own; see async_job.h.

#include "base/async_job.h"

#include "base/prof.h"

#include <SDL3/SDL_properties.h>

#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#endif

namespace gt3r {

AsyncJob::AsyncJob(int core) : core_(core) {
    wake_ = SDL_CreateSemaphore(0);
    done_ = SDL_CreateSemaphore(0);
    if (!wake_ || !done_) return;
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&thread_main));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "async job");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 1024 * 1024); // (the race step's depth)
    thread_ = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
}

AsyncJob::~AsyncJob() {
    join();
    if (thread_) {
        quit_ = true;
        SDL_SignalSemaphore(wake_);
        SDL_WaitThread(thread_, nullptr);
    }
    if (wake_) SDL_DestroySemaphore(wake_);
    if (done_) SDL_DestroySemaphore(done_);
}

int SDLCALL AsyncJob::thread_main(void* self) {
    static_cast<AsyncJob*>(self)->work();
    return 0;
}

void AsyncJob::work() {
#if defined(__vita__)
    if (core_ >= 0 && core_ <= 2)
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << core_);
#endif
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    prof::set_thread_off(); // (the profiler's sections are the main thread's)
    for (;;) {
        SDL_WaitSemaphore(wake_);
        if (quit_) return;
        fn_();
        SDL_SignalSemaphore(done_); // (a system call: the job's writes are out before it)
    }
}

void AsyncJob::start(std::function<void()> fn) {
    join();
    if (!thread_) {
        fn();
        return;
    }
    fn_ = std::move(fn);
    running_ = true;
    SDL_SignalSemaphore(wake_);
}

void AsyncJob::join() {
    if (!running_) return;
    SDL_WaitSemaphore(done_);
    running_ = false;
    fn_ = nullptr;
}

} // namespace gt3r
