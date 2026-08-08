#pragma once
#include <csignal>
#include <csetjmp>

// Runs `fn` under a SIGSEGV guard. If fn triggers a segmentation fault the
// handler longjmps back to the guard and runSigGuarded() returns false. This
// is used to protect against crashing native plugin code (e.g. DPF-based
// editors) so the host survives.
//
// Not reentrant: only one guard may be active per thread at a time. The
// handler operates on thread-local state, matching how the previous per-file
// sigsetjmp guards behaved.
template <typename Fn>
inline bool runSigGuarded(Fn fn) {
    static thread_local sigjmp_buf jmpBuf;
    static thread_local volatile sig_atomic_t crashed = 0;

    struct sigaction oldSa, sa{};
    sa.sa_handler = [](int) {
        crashed = 1;
        siglongjmp(jmpBuf, 1);
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &oldSa);

    crashed = 0;
    bool ok = true;
    if (sigsetjmp(jmpBuf, 1) == 0) {
        fn();
    } else {
        ok = false;
    }

    sigaction(SIGSEGV, &oldSa, nullptr);
    return ok;
}
