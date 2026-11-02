/**
 * profsample.c - a small built-in sampling profiler for Linux and Android (GE_PROFSAMPLE=1).
 *
 * Android blocks simpleperf on most retail phones, so this samples the process itself: a
 * ITIMER_PROF signal (CPU time, all threads) records the interrupted pc plus two frame-pointer
 * callers (the Android build keeps frame pointers). Every GE_PROFSAMPLE_SECS seconds (default 20)
 * the hottest stacks are logged as module-relative offsets, which symbolize on the host with
 *     llvm-addr2line -f -C -e libmain.so <offset>
 * It costs nothing unless the variable is set.
 */
#include <ultra64.h>

#if defined(__linux__) || defined(__ANDROID__)

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <time.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unwind.h>
#include <unistd.h>

extern void portConLogf(const char *fmt, ...);

#define PS_RING 65536u
#define PS_DEPTH 7

static uintptr_t s_Ring[PS_RING][PS_DEPTH + 1]; /* [0] = tid, then pc + callers */
static volatile uint32_t s_Head;
static volatile uint32_t s_Tail;
static int s_Active;
static uintptr_t s_Base;

extern char _end[]; /* linker: end of this module's image */

static uintptr_t psPc(const ucontext_t *uc, uintptr_t *fp, uintptr_t *sp)
{
#if defined(__aarch64__)
    *fp = (uintptr_t) uc->uc_mcontext.regs[29];
    *sp = (uintptr_t) uc->uc_mcontext.sp;
    return (uintptr_t) uc->uc_mcontext.pc;
#elif defined(__arm__)
    *fp = (uintptr_t) uc->uc_mcontext.arm_fp;
    *sp = (uintptr_t) uc->uc_mcontext.arm_sp;
    return (uintptr_t) uc->uc_mcontext.arm_pc;
#elif defined(__x86_64__)
    *fp = (uintptr_t) uc->uc_mcontext.gregs[REG_RBP];
    *sp = (uintptr_t) uc->uc_mcontext.gregs[REG_RSP];
    return (uintptr_t) uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
    *fp = (uintptr_t) uc->uc_mcontext.gregs[REG_EBP];
    *sp = (uintptr_t) uc->uc_mcontext.gregs[REG_ESP];
    return (uintptr_t) uc->uc_mcontext.gregs[REG_EIP];
#else
    *fp = 0;
    *sp = 0;
    return 0;
#endif
}

/* is the page holding `a` mapped? (mincore fails with ENOMEM on an unmapped page; no memory is touched) */
static int psMapped(uintptr_t a)
{
    unsigned char v;

    return syscall(SYS_mincore, (void *) (a & ~(uintptr_t) 4095u), (size_t) 4096u, &v) == 0;
}

/* GE_PROFSAMPLE=2: unwind with the exception-table unwinder (it can walk through the GL driver, which
 * keeps no frame pointers). Not async-signal-safe in theory; fine for a diagnostic run. */
static int s_UseUnwind;

struct PsUnw
{
    uintptr_t *out;
    int n;
};

static _Unwind_Reason_Code psUnwCb(struct _Unwind_Context *c, void *arg)
{
    struct PsUnw *u = (struct PsUnw *) arg;
    const uintptr_t ip = (uintptr_t) _Unwind_GetIP(c);

    if (u->n >= PS_DEPTH + 3)
    {
        return _URC_END_OF_STACK;
    }

    u->out[u->n++] = ip;
    return _URC_NO_REASON;
}

static void psHandler(int sig, siginfo_t *si, void *ctx)
{
    uintptr_t fp;
    uintptr_t sp;
    uintptr_t pc = psPc((const ucontext_t *) ctx, &fp, &sp);
    uint32_t slot = __sync_fetch_and_add(&s_Head, 1u) % PS_RING;
    int d;

    (void) sig;
    (void) si;
    s_Ring[slot][0] = (uintptr_t) syscall(SYS_gettid);
    s_Ring[slot][1] = pc;

    if (s_UseUnwind)
    {
        struct PsUnw u;
        uintptr_t frames[PS_DEPTH + 4];
        int k;

        memset(frames, 0, sizeof(frames));
        u.out = frames;
        u.n = 0;
        _Unwind_Backtrace(psUnwCb, &u);

        /* frames 0 and 1 are the handler and the signal trampoline; 2 is the interrupted pc */
        for (k = 0; k < PS_DEPTH; k++)
        {
            s_Ring[slot][1 + k] = k == 0 ? pc : (u.n > k + 2 ? frames[k + 2] : 0);
        }

        return;
    }

    for (d = 1; d < PS_DEPTH; d++)
    {
        uintptr_t next = 0;

        /* the frame must sit just above sp, be mapped, and every link must climb */
        if (fp != 0 && (fp & (sizeof(uintptr_t) - 1)) == 0 && fp >= sp && fp - sp < 0x400000u
            && psMapped(fp) && psMapped(fp + 2 * sizeof(uintptr_t) - 1))
        {
            const uintptr_t *frame = (const uintptr_t *) fp;
            const uintptr_t nfp = frame[0];

            next = frame[1];
            fp = (nfp > fp && nfp - fp < 0x400000u) ? nfp : 0;
        }
        else
        {
            fp = 0;
        }

        s_Ring[slot][1 + d] = next;
    }
}

struct PsEntry
{
    uintptr_t tid;
    uintptr_t pc[PS_DEPTH];
    uint32_t count;
};

#define PS_TABLE 4096u
static struct PsEntry s_Table[PS_TABLE];
static uint32_t s_Total;
static uint32_t s_Dropped;

static void psRecord(const uintptr_t *s)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    uint32_t probe;

    for (i = 0; i < PS_DEPTH + 1; i++)
    {
        h = (h ^ (uint32_t) s[i]) * 16777619u;
    }

    for (probe = 0; probe < 64; probe++)
    {
        struct PsEntry *e = &s_Table[(h + probe) % PS_TABLE];

        if (e->count == 0)
        {
            e->tid = s[0];
            memcpy(e->pc, s + 1, sizeof(e->pc));
            e->count = 1;
            s_Total++;
            return;
        }

        if (e->tid == s[0] && memcmp(e->pc, s + 1, sizeof(e->pc)) == 0)
        {
            e->count++;
            s_Total++;
            return;
        }
    }

    s_Dropped++;
}

static int psCmp(const void *a, const void *b)
{
    const struct PsEntry *x = (const struct PsEntry *) a;
    const struct PsEntry *y = (const struct PsEntry *) b;

    return (int) y->count - (int) x->count;
}

static void psDump(void)
{
    static struct PsEntry sorted[PS_TABLE];
    uint32_t i;
    uint32_t shown = 0;

    memcpy(sorted, s_Table, sizeof(sorted));
    qsort(sorted, PS_TABLE, sizeof(sorted[0]), psCmp);
    portConLogf("ge: prof: %u samples (%u dropped), module base 0x%lx; offsets below\n",
                (unsigned) s_Total, (unsigned) s_Dropped, (unsigned long) s_Base);

    for (i = 0; i < PS_TABLE && shown < 80 && sorted[i].count != 0; i++, shown++)
    {
        char line[600];
        size_t len = 0;
        int d;

        len += (size_t) snprintf(line + len, sizeof(line) - len, "n=%u tid=%lu", (unsigned) sorted[i].count,
                                 (unsigned long) sorted[i].tid);

        for (d = 0; d < PS_DEPTH && sorted[i].pc[d] != 0 && len < sizeof(line) - 80; d++)
        {
            const uintptr_t a = sorted[i].pc[d];

            if (a >= s_Base && a < (uintptr_t) _end)
            {
                len += (size_t) snprintf(line + len, sizeof(line) - len, " F0x%lx", (unsigned long) (a - s_Base));
            }
            else
            {
                Dl_info di;
                const char *slash;

                if (dladdr((void *) a, &di) != 0)
                {
                    slash = di.dli_fname != NULL ? strrchr(di.dli_fname, '/') : NULL;
                    if (di.dli_sname != NULL)
                    {
                        len += (size_t) snprintf(line + len, sizeof(line) - len, " X%s::%s",
                                                 slash != NULL ? slash + 1 : "?", di.dli_sname);
                    }
                    else
                    {
                        len += (size_t) snprintf(line + len, sizeof(line) - len, " X%s+0x%lx",
                                                 slash != NULL ? slash + 1 : "?", (unsigned long) (a - (uintptr_t) di.dli_fbase));
                    }
                }
                else
                {
                    len += (size_t) snprintf(line + len, sizeof(line) - len, " X?");
                }
            }
        }

        portConLogf("ge: prof: %s\n", line);
    }

    memset(s_Table, 0, sizeof(s_Table));
    s_Total = 0;
    s_Dropped = 0;
}

/* Wall-clock sampling: one timer per engine thread (the game/render "SDLThread"s and the audio
 * pump), so a thread blocked inside the GL driver shows where it is blocked instead of vanishing
 * from a CPU-time profile. Threads are found by name under /proc/self/task, a few times a second. */
static pid_t s_Timed[16];
static int s_NumTimed;

static void psScanThreads(void)
{
    static uint64_t last;
    struct timespec ts;
    uint64_t now;
    DIR *dir;
    struct dirent *de;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t) ts.tv_sec * 4u + (uint64_t) ts.tv_nsec / 250000000u;

    if (now == last)
    {
        return;
    }

    last = now;
    dir = opendir("/proc/self/task");

    if (dir == NULL)
    {
        return;
    }

    while ((de = readdir(dir)) != NULL)
    {
        const pid_t tid = (pid_t) atoi(de->d_name);
        char path[64];
        char comm[32] = "";
        FILE *f;
        int i;
        int known = 0;

        if (tid <= 0 || s_NumTimed >= (int) (sizeof(s_Timed) / sizeof(s_Timed[0])))
        {
            continue;
        }

        for (i = 0; i < s_NumTimed; i++)
        {
            known |= s_Timed[i] == tid;
        }

        if (known)
        {
            continue;
        }

        snprintf(path, sizeof(path), "/proc/self/task/%d/comm", (int) tid);
        f = fopen(path, "r");

        if (f != NULL)
        {
            if (fgets(comm, sizeof(comm), f) == NULL)
            {
                comm[0] = 0;
            }

            fclose(f);
        }

        if (strncmp(comm, "SDLThread", 9) == 0 || strncmp(comm, "SDLAudio", 8) == 0)
        {
            struct sigevent sev;
            struct itimerspec its;
            timer_t timer;

            memset(&sev, 0, sizeof(sev));
            sev.sigev_notify = SIGEV_THREAD_ID;
            sev.sigev_signo = SIGPROF;
#ifdef sigev_notify_thread_id
            sev.sigev_notify_thread_id = tid;
#else
            sev._sigev_un._tid = tid;
#endif

            if (timer_create(CLOCK_MONOTONIC, &sev, &timer) == 0)
            {
                its.it_interval.tv_sec = 0;
                its.it_interval.tv_nsec = 2000000;
                its.it_value = its.it_interval;
                timer_settime(timer, 0, &its, NULL);
                s_Timed[s_NumTimed++] = tid;
                portConLogf("ge: prof: timing thread %d (%s)\n", (int) tid, comm[0] ? comm : "?");
            }
        }
    }

    closedir(dir);
}

/** Called regularly from the frame loop: drains the ring and, every few seconds, logs the top stacks. */
void portProfSampleTick(void)
{
    static uint64_t lastdump;
    struct timeval tv;
    uint64_t now;
    uint32_t head;

    if (!s_Active)
    {
        return;
    }

    psScanThreads();
    head = s_Head;

    while (s_Tail != head)
    {
        if (head - s_Tail > PS_RING)
        {
            s_Tail = head - PS_RING; /* overran: keep the newest */
        }

        psRecord(s_Ring[s_Tail % PS_RING]);
        s_Tail++;
    }

    gettimeofday(&tv, NULL);
    now = (uint64_t) tv.tv_sec;

    if (lastdump == 0)
    {
        lastdump = now;
    }
    else
    {
        const char *e = getenv("GE_PROFSAMPLE_SECS");
        const uint64_t every = (e != NULL && atoi(e) > 0) ? (uint64_t) atoi(e) : 20u;

        if (now - lastdump >= every)
        {
            lastdump = now;
            psDump();
        }
    }
}

void portProfSampleInit(void)
{
    const char *e = getenv("GE_PROFSAMPLE");
    struct sigaction sa;
    Dl_info info;

    if (s_Active || e == NULL || e[0] == '\0' || e[0] == '0')
    {
        return;
    }

    if (dladdr((void *) portProfSampleInit, &info) != 0)
    {
        s_Base = (uintptr_t) info.dli_fbase;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = psHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    s_UseUnwind = 0; /* the exception-table unwinder crashes inside the handler on Android: not offered */
    sigaction(SIGPROF, &sa, NULL);

    s_Active = 1;
    portConLogf("ge: prof: sampling started (GE_PROFSAMPLE)\n");
}

#else

void portProfSampleInit(void)
{
}

void portProfSampleTick(void)
{
}

#endif
