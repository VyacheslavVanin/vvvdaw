#pragma once
#include <csignal>
#include <csetjmp>
#include <pthread.h>

// Runs `fn` under a SIGSEGV guard. If fn triggers a segmentation fault the
// handler longjmps back to the guard and runSigGuarded() returns false. This
// is used to protect against crashing native plugin code (e.g. DPF-based
// editors) so the host survives.
//
// The guard is thread-aware: sigaction() installs the handler process-wide,
// so if another thread (e.g. the PortAudio callback thread) faults while a
// guard is active here, the handler hands the crash off to the previously
// installed SIGSEGV handler (typically the app's crash handler) instead of
// longjmp'ing on a foreign thread's stack, which would be undefined behavior
// and produce an unreadable backtrace.
//
// Not reentrant: only one guard may be active per thread at a time.

namespace {

struct SigGuardState {
    sigjmp_buf jmpBuf;
    volatile sig_atomic_t crashed = 0;
    pthread_t tid = 0;
    struct sigaction prevSa {};
};

// thread_local so the handler (which runs on the crashing thread) finds the
// guard belonging to that thread, if any.
inline thread_local SigGuardState* t_sigGuardState = nullptr;

// Most recently installed previous SIGSEGV handler, used to hand off
// foreign-thread crashes so they terminate with a readable backtrace.
inline struct sigaction g_prevSa {};
inline bool g_prevSaValid = false;

inline void sigGuardHandler(int sig) {
    SigGuardState* g = t_sigGuardState;
    if (!g || !pthread_equal(pthread_self(), g->tid)) {
        // Fault on a thread without an active guard. Restore the previously
        // installed handler and re-raise so the real crash handler runs. If
        // that handler returns (e.g. a test handler), resume the faulting
        // thread as if the guard had never been installed.
        if (g_prevSaValid)
            sigaction(sig, &g_prevSa, nullptr);
        else
            signal(sig, SIG_DFL);
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, sig);
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);
        raise(sig);
        return;
    }
    g->crashed = 1;
    siglongjmp(g->jmpBuf, 1);
}

} // namespace

template <typename Fn>
inline bool runSigGuarded(Fn fn) {
    SigGuardState state;
    state.tid = pthread_self();
    state.crashed = 0;

    struct sigaction sa{};
    sa.sa_handler = sigGuardHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &state.prevSa);
    g_prevSa = state.prevSa;
    g_prevSaValid = true;

    SigGuardState* prevState = t_sigGuardState;
    t_sigGuardState = &state;

    bool ok = true;
    if (sigsetjmp(state.jmpBuf, 1) == 0) {
        fn();
    } else {
        ok = false;
    }

    t_sigGuardState = prevState;
    sigaction(SIGSEGV, &state.prevSa, nullptr);
    return ok;
}
