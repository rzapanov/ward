/// Crash capture does not call the allocator, loader, unwinder or symbolizer.
/// A pre-spawned copy of this executable owns optional addr2line execution.
#define _GNU_SOURCE
#include "internal/runner.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifdef __linux__
#include <fcntl.h>
#include <link.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define WARD_HAVE_EXECINFO 1
#endif

extern char **environ;

enum { TRACE_DEPTH = 12, TRACE_MODULES = 128, TRACE_PATH = 256 };
struct TraceFrame {
    uintptr_t pc;
    uintptr_t address;
    char module[TRACE_PATH];
};
struct Trace {
    unsigned count;
    struct TraceFrame frames[TRACE_DEPTH];
};
struct Module {
    uintptr_t start, end, bias;
    char path[TRACE_PATH];
};
static struct Module modules[TRACE_MODULES];
static unsigned module_count;
static struct Trace trace;
static int trace_fd = -1;
// Keep a reader open until capture ends, so a dead helper cannot cause SIGPIPE.
static int trace_guard = -1;
static pid_t reporter = -1;
static bool reporter_succeeded;
static atomic_flag reporting = ATOMIC_FLAG_INIT;
_Static_assert(sizeof(struct Trace) <= 4096, "trace must fit Linux PIPE_BUF");

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int remember_module(struct dl_phdr_info *info, size_t size, void *data) {
    (void) size;
    (void) data;
    for (unsigned i = 0; i < info->dlpi_phnum && module_count < TRACE_MODULES; i++) {
        ElfW(Phdr) const *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X)) continue;
        struct Module *m = &modules[module_count++];
        m->bias = info->dlpi_addr;
        m->start = m->bias + ph->p_vaddr;
        m->end = m->start + ph->p_memsz;
        char const *path = info->dlpi_name;
        if (!path || !*path) {
            ssize_t n = readlink("/proc/self/exe", m->path, sizeof(m->path));
            if (n > 0 && (size_t) n < sizeof(m->path)) m->path[n] = '\0';
            else m->path[0] = '\0';
        } else if (strlen(path) < sizeof(m->path)) {
            strcpy(m->path, path);
        } else {
            m->path[0] = '\0';
        }
    }
    return 0;
}

static void add_frame(uintptr_t pc, bool return_address) {
    if (!pc || trace.count == TRACE_DEPTH) return;
    struct TraceFrame *f = &trace.frames[trace.count++];
    f->pc = pc;
    // Return PCs point past the call; fault PCs already identify the instruction.
    uintptr_t lookup = return_address ? pc - 1 : pc;
    f->address = lookup;
    f->module[0] = '\0';
    for (unsigned i = 0; i < module_count; i++) {
        struct Module const *m = &modules[i];
        if (lookup < m->start || lookup >= m->end) continue;
        f->address = lookup - m->bias;
        for (unsigned j = 0; j < TRACE_PATH; j++) {
            f->module[j] = m->path[j];
            if (!m->path[j]) break;
        }
        break;
    }
}

static void raw_hex(uintptr_t value) {
    char buffer[2 + sizeof(value) * 2];
    buffer[0] = '0'; buffer[1] = 'x';
    for (size_t i = 0; i < sizeof(value) * 2; i++) {
        buffer[sizeof(buffer) - 1 - i] = "0123456789abcdef"[value & 15];
        value >>= 4;
    }
    (void) write(STDERR_FILENO, buffer, sizeof(buffer));
}

static void raw_trace(void) {
    for (unsigned i = 0; i < trace.count; i++) {
        char label[] = "  #0  ";
        label[3] = (char) ('0' + (i < 10 ? i : i / 10));
        if (i >= 10) label[4] = (char) ('0' + i % 10);
        (void) write(STDERR_FILENO, label, sizeof(label) - 1);
        raw_hex(trace.frames[i].pc);
        char const *module = trace.frames[i].module;
        size_t length = 0;
        while (length < TRACE_PATH && module[length]) length++;
        if (length) {
            (void) write(STDERR_FILENO, " ", 1);
            (void) write(STDERR_FILENO, module, length);
            (void) write(STDERR_FILENO, "+", 1);
            raw_hex(trace.frames[i].address);
        }
        (void) write(STDERR_FILENO, "\n", 1);
    }
    if (!trace.count) {
        static char const unavailable[] = "  fault registers unavailable on this architecture\n";
        (void) write(STDERR_FILENO, unavailable, sizeof(unavailable) - 1);
    }
}

static void submit_trace(void) {
    static char const heading[] = "ward: backtrace:\n";
    (void) write(STDERR_FILENO, heading, sizeof(heading) - 1);
    bool sent = false;
    if (trace_fd >= 0) {
        ssize_t n;
        do {
            n = write(trace_fd, &trace, sizeof(trace));
        } while (n < 0 && errno == EINTR);
        sent = n == (ssize_t) sizeof(trace);
    }
    ward_trace_stop();
    // A failed or killed helper must not suppress the emergency trace.
    if (!sent || !reporter_succeeded) raw_trace();
}

void ward_trace_start(void) {
    module_count = 0;
    dl_iterate_phdr(remember_module, NULL);
    atomic_flag_clear(&reporting);
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) != 0) return;
    // The reporter reads synchronously. Only the handler's write end is nonblocking.
    (void) fcntl(pipes[0], F_SETFL, 0);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipes[0]); close(pipes[1]); return;
    }
    int error = posix_spawn_file_actions_adddup2(&actions, pipes[0], STDIN_FILENO);
    char *args[] = { (char *) "ward-symbolizer", (char *) "--_ward-symbolizer", NULL };
    if (!error) error = posix_spawn(&reporter, "/proc/self/exe", &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error) { close(pipes[0]); close(pipes[1]); reporter = -1; return; }
    trace_guard = pipes[0];
    trace_fd = pipes[1];
}

void ward_trace_stop(void) {
    reporter_succeeded = false;
    if (trace_fd >= 0) { close(trace_fd); trace_fd = -1; }
    if (trace_guard >= 0) { close(trace_guard); trace_guard = -1; }
    if (reporter > 0) {
        long long deadline = now_ms() + 2000;
        for (;;) {
            int status = 0;
            pid_t waited = waitpid(reporter, &status, WNOHANG);
            if (waited == reporter) {
                reporter_succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                break;
            }
            if (waited < 0 && errno != EINTR) break;
            if (now_ms() >= deadline) {
                (void) kill(reporter, SIGKILL);
                while (waitpid(reporter, NULL, 0) < 0 && errno == EINTR) {}
                break;
            }
            struct timespec pause = { .tv_nsec = 1000000 };
            (void) nanosleep(&pause, NULL);
        }
        reporter = -1;
    }
}

void ward_trace_assertion(void) {
    if (atomic_flag_test_and_set(&reporting)) return;
    trace.count = 0;
#ifdef WARD_HAVE_EXECINFO
    void *frames[TRACE_DEPTH];
    int count = backtrace(frames, TRACE_DEPTH);
    for (int i = 1; i < count; i++) add_frame((uintptr_t) frames[i], true);
#endif
    if (!trace.count) add_frame((uintptr_t) __builtin_return_address(0), true);
    submit_trace();
}

void ward_trace_signal(void *context) {
    if (atomic_flag_test_and_set(&reporting)) {
        static char const recursive[] = "ward: recursive/concurrent fault during reporting\n";
        (void) write(STDERR_FILENO, recursive, sizeof(recursive) - 1);
        return;
    }
    trace.count = 0;
    uintptr_t pc = 0, fp = 0, sp = 0;
    ucontext_t const *uc = context;
#if defined(__x86_64__)
    pc = (uintptr_t) uc->uc_mcontext.gregs[REG_RIP];
    fp = (uintptr_t) uc->uc_mcontext.gregs[REG_RBP];
    sp = (uintptr_t) uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
    pc = uc->uc_mcontext.pc;
    fp = uc->uc_mcontext.regs[29];
    sp = uc->uc_mcontext.sp;
#else
    (void) uc;
#endif
    add_frame(pc, false);
    // Linux syscall reads safely even if the stack/frame pointer is corrupt.
    // No direct dereference, allocation, loader calls or libc unwinder here.
    while (trace.count < TRACE_DEPTH && fp >= sp && fp - sp < 8 * 1024 * 1024
           && fp % sizeof(uintptr_t) == 0) {
        uintptr_t pair[2];
        struct iovec local = { pair, sizeof(pair) };
        struct iovec remote = { (void *) fp, sizeof(pair) };
        long n = syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0);
        if (n != (long) sizeof(pair)) break;
        add_frame(pair[1], true);
        if (pair[0] <= fp) break;
        fp = pair[0];
    }
    submit_trace();
}

// One deadline for the entire trace; even a broken addr2line cannot hang --exact.
static bool symbolize(struct TraceFrame const *frame, unsigned index, long long deadline) {
    if (!frame->module[0] || now_ms() >= deadline) return false;
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) != 0) return false;
    char address[2 + sizeof(uintptr_t) * 2 + 1];
    snprintf(address, sizeof(address), "0x%lx", (unsigned long) frame->address);
    char *args[] = { (char *) "addr2line", (char *) "-f", (char *) "-C", (char *) "-i",
                    (char *) "-e", (char *) frame->module, address, NULL };
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipes[0]); close(pipes[1]); return false;
    }
    if (posix_spawnattr_init(&attr) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipes[0]); close(pipes[1]); return false;
    }
    int error = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    error |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    error |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    error |= posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    error |= posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = -1;
    if (!error) error = posix_spawnp(&pid, "addr2line", &actions, &attr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(pipes[1]);
    if (error) { close(pipes[0]); return false; }
    char output[8192];
    size_t used = 0;
    int status = 0;
    bool finished = false;
    while (now_ms() < deadline) {
        ssize_t n = read(pipes[0], output + used, sizeof(output) - used - 1);
        if (n > 0) used += (size_t) n;
        if (used == sizeof(output) - 1) break;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            finished = true;
            // Collect bytes written immediately before exit.
            while ((n = read(pipes[0], output + used, sizeof(output) - used - 1)) > 0) {
                used += (size_t) n;
                if (used == sizeof(output) - 1) break;
            }
            break;
        }
        struct pollfd fd = { .fd = pipes[0], .events = POLLIN };
        (void) poll(&fd, 1, 10);
    }
    if (!finished) {
        (void) kill(-pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    close(pipes[0]);
    if (!finished || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || !used) return false;
    output[used] = '\0';
    if (strcmp(output, "??\n??:0\n") == 0 || strcmp(output, "??\n??:?\n") == 0) return false;
    // addr2line -f -i emits function/location pairs, innermost inline frame first.
    unsigned lines = 0;
    for (size_t i = 0; i < used; i++) lines += output[i] == '\n';
    if (output[used - 1] != '\n' || lines % 2 != 0) return false;
    char *cursor = output;
    bool printed = false;
    while (*cursor) {
        char *function = cursor;
        char *end = strchr(cursor, '\n');
        *end = '\0';
        char *location = end + 1;
        end = strchr(location, '\n');
        *end = '\0';
        cursor = end + 1;
        if (strcmp(function, "??") == 0 && strncmp(location, "??:", 3) == 0) continue;
        if (printed) fputs("       inlined by ", stderr);
        else fprintf(stderr, "  #%-2u ", index);
        fputs(strcmp(function, "??") == 0 ? "<unknown>" : function, stderr);
        if (strncmp(location, "??:", 3) != 0) fprintf(stderr, " at %s", location);
        else fprintf(stderr, " (%s+%s)", frame->module, address);
        fputc('\n', stderr);
        printed = true;
    }
    return printed;
}

int ward_trace_helper(void) {
    struct Trace packet;
    size_t used = 0;
    while (used < sizeof(packet)) {
        ssize_t n = read(STDIN_FILENO, (char *) &packet + used, sizeof(packet) - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 1;
        used += (size_t) n;
    }
    if (packet.count > TRACE_DEPTH) return 1;
    long long deadline = now_ms() + 1500;
    bool resolved = false;
    for (unsigned i = 0; i < packet.count; i++) {
        packet.frames[i].module[TRACE_PATH - 1] = '\0';
        struct TraceFrame const *f = &packet.frames[i];
        if (symbolize(f, i, deadline)) {
            resolved = true;
        } else {
            fprintf(stderr, "  #%-2u 0x%lx", i, (unsigned long) f->pc);
            if (f->module[0]) fprintf(stderr, " (%s+0x%lx)", f->module,
                                      (unsigned long) f->address);
            fputc('\n', stderr);
        }
    }
    if (!packet.count) fputs("  fault registers unavailable on this architecture\n", stderr);
    else if (!resolved) fputs("  (source symbolization unavailable)\n", stderr);
    return 0;
}
#else
void ward_trace_start(void) {}
void ward_trace_stop(void) {
    reporter_succeeded = false;}
void ward_trace_assertion(void) {}
void ward_trace_signal(void *context) { (void) context; }
int ward_trace_helper(void) { return 0; }
#endif
