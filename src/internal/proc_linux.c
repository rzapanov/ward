/// Multi-process executor for Linux.
///
/// Each test case runs in its own process, created with `posix_spawn` and
/// tracked through a pidfd. Design notes:
///
///  - `posix_spawn` instead of `fork` + `dup2` + `execv`: nothing runs in the
///    child between "fork" and "exec", so an inherited stdio lock, a malloc
///    lock or pending buffered output can never deadlock or duplicate output in
///    the child. That is the bug that used to make the runner hang.
///  - Single threaded event loop. No pthreads, so no lock can be held by a
///    "lost" thread at spawn time.
///  - Completion is observed on a pidfd, not by pipe EOF, so a test that closes
///    or leaks its own stdout cannot be mistaken for having finished.
///  - Every test has a deadline; expiry escalates SIGTERM then SIGKILL.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "internal/runner.h"
#include "ward/support/fatal.h"

extern char **environ;

struct OutputBuffer {
    char *bytes;
    size_t size;
    bool truncated;
};

struct Slot {
    int readfd;
    int writefd;
    int pidfd;
    pid_t pid;
    struct WardCase *test;
    struct OutputBuffer *output;
    long long deadline_ms;
    long long kill_deadline_ms;
    bool running;
    bool timed_out;
};

struct Loop {
    struct WardRunner *runner;
    int epfd;
    int job_count;
    struct Slot *slots;
    struct OutputBuffer *outputs;
    size_t next_case;
    int running;
    bool stop_scheduling;
};

enum {
    EV_PIPE = 1,
    EV_PROC = 2,
};

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int ward_pidfd_open(pid_t pid) {
    return (int) syscall(SYS_pidfd_open, pid, 0);
}

bool ward_parallel_supported(void) {
    static int cached = -1;
    if (cached >= 0) {
        return cached == 1;
    }
    int fd = ward_pidfd_open(getpid());
    if (fd < 0) {
        cached = 0;
        return false;
    }
    close(fd);
    cached = 1;
    return true;
}

static int adjust_job_count(struct WardRunner *runner) {
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) {
        cpus = 1;
    }
    long jobs = runner->options.jobs > 0 ? runner->options.jobs : cpus;
    if (jobs > WARD_MAX_PARALLELISM) {
        jobs = WARD_MAX_PARALLELISM;
    }
    return (int) jobs;
}

static void epoll_add(int epfd, int fd, void *token, uint32_t events) {
    struct epoll_event event = { .events = events, .data.ptr = token };
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        WARD_FATAL("epoll_ctl(ADD, fd=%d) failed: %s", fd, strerror(errno));
    }
}

static void epoll_drop(int epfd, int fd) {
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) == -1 && errno != ENOENT) {
        WARD_FATAL("epoll_ctl(DEL, fd=%d) failed: %s", fd, strerror(errno));
    }
}

static void output_append(struct OutputBuffer *buffer, char const *bytes, size_t size) {
    size_t left = WARD_MAX_OUTPUT_SIZE - buffer->size;
    if (size > left) {
        size = left;
        buffer->truncated = true;
    }
    if (size == 0) {
        return;
    }
    buffer->bytes = ward_realloc(buffer->bytes, buffer->size + size);
    memcpy(buffer->bytes + buffer->size, bytes, size);
    buffer->size += size;
}

static void drain_pipe(struct Slot *slot) {
    char buffer[4096];
    for (;;) {
        ssize_t n = read(slot->readfd, buffer, sizeof(buffer));
        if (n > 0) {
            output_append(slot->output, buffer, (size_t) n);
            continue;
        }
        if (n == 0) {
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        WARD_FATAL("read from test output pipe failed: %s", strerror(errno));
    }
}

static void slot_close_fds(struct Loop *loop, struct Slot *slot) {
    epoll_drop(loop->epfd, slot->readfd);
    if (slot->pidfd >= 0) {
        epoll_drop(loop->epfd, slot->pidfd);
        close(slot->pidfd);
        slot->pidfd = -1;
    }
}

static void finish_slot(struct Loop *loop, struct Slot *slot, enum WardStatus status,
                        char const *note) {
    struct WardRunner *runner = loop->runner;
    struct WardCase *test = slot->test;

    drain_pipe(slot);
    ward_report_status(runner, test, status);
    if (note && note[0]) {
        ward_report_output(runner, note, strlen(note));
    }
    bool show_output = status != WS_PASS || runner->options.verbose;
    if (show_output && slot->output->size > 0) {
        ward_report_output(runner, slot->output->bytes, slot->output->size);
    }
    if (show_output && slot->output->truncated) {
        char note_buffer[96];
        int n = snprintf(note_buffer, sizeof(note_buffer),
                         "(output truncated after %d bytes)", WARD_MAX_OUTPUT_SIZE);
        ward_report_output(runner, note_buffer, (size_t) (n > 0 ? n : 0));
    }
    switch (status) {
        case WS_PASS:    runner->succeeded++; break;
        case WS_SKIP:    runner->ignored++;   break;
        case WS_FAIL:    runner->failed++;    break;
        case WS_CRASH:   runner->crashed++;   break;
        case WS_TIMEOUT: runner->timed_out++; break;
    }
    if (status != WS_PASS && status != WS_SKIP) {
        ward_report_reproducer(runner, test);
        loop->stop_scheduling = runner->options.fail_fast;
    }

    slot_close_fds(loop, slot);
    slot->running = false;
    slot->test = NULL;
    slot->pid = -1;
    loop->running--;
}

static void spawn_into_slot(struct Loop *loop, struct Slot *slot, struct WardCase *test) {
    struct WardRunner *runner = loop->runner;

    char const *prefix = "--exact=";
    size_t argument_size = strlen(prefix) + test->full_name.length + 1;
    char *argument = malloc(argument_size);
    if (!argument) {
        WARD_FATAL("out of memory");
    }
    snprintf(argument, argument_size, "%s%.*s", prefix,
             (int) test->full_name.length, test->full_name.text);

    char const *self = runner->options.child_program
                     ? runner->options.child_program
                     : (runner->self_path.length > 0 ? runner->self_path.text : "/proc/self/exe");
    char *argv[] = { (char *) "ward-test", (char *) "--_ward-child", argument, NULL };

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        WARD_FATAL("posix_spawn_file_actions_init failed");
    }
    int actions_error = posix_spawn_file_actions_adddup2(&actions, slot->writefd, STDOUT_FILENO);
    actions_error |= posix_spawn_file_actions_adddup2(&actions, slot->writefd, STDERR_FILENO);
    // stdin comes from /dev/null: a test that reads must not steal the runner's
    // terminal, and a read must return EOF instead of blocking until timeout.
    actions_error |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                                      O_RDONLY, 0);
    if (actions_error != 0) {
        WARD_FATAL("posix_spawn file actions failed");
    }

    // Reset handlers and the signal mask in the child, so a test that installs
    // a handler cannot poison its siblings and SIGTERM always arrives.
    // Each child gets its own process group so a timeout signals the whole
    // tree (including grandchildren), not just the direct child.
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        WARD_FATAL("posix_spawnattr_init failed");
    }
    sigset_t empty;
    sigemptyset(&empty);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF
                              | POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setsigmask(&attributes, &empty);
    posix_spawnattr_setpgroup(&attributes, 0);

    pid_t pid;
    int error = posix_spawn(&pid, self, &actions, &attributes, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    free(argument);

    if (error != 0) {
        char note[256];
        int n = snprintf(note, sizeof(note), "failed to spawn test process: %s", strerror(error));
        ward_report_status(runner, test, WS_CRASH);
        ward_report_output(runner, note, (size_t) (n > 0 ? n : 0));
        ward_report_reproducer(runner, test);
        runner->crashed++;
        loop->stop_scheduling = runner->options.fail_fast;
        return;
    }

    int pidfd = ward_pidfd_open(pid);
    if (pidfd < 0) {
        WARD_FATAL("pidfd_open(%d) failed: %s", (int) pid, strerror(errno));
    }

    size_t index = (size_t) (test - runner->cases);
    struct OutputBuffer *output = &loop->outputs[index];
    output->size = 0;
    output->truncated = false;

    runner->attempted++;
    slot->test = test;
    slot->pid = pid;
    slot->pidfd = pidfd;
    slot->output = output;
    slot->running = true;
    slot->timed_out = false;

    double timeout = test->timeout_seconds > 0 ? test->timeout_seconds
                                               : runner->options.timeout_seconds;
    // timeout == 0 means "no deadline" (opt-out).
    slot->deadline_ms = timeout > 0 ? now_ms() + (long long) (timeout * 1000.0)
                                     : ((long long) 1 << 62);
    slot->kill_deadline_ms = -1;

    epoll_add(loop->epfd, pidfd, (void *) ((uintptr_t) slot | EV_PROC), EPOLLIN);
    loop->running++;
}

static void poll_slot(struct Loop *loop, struct Slot *slot) {
    siginfo_t info = {0};
    for (;;) {
        int code = waitid(P_PIDFD, (id_t) slot->pidfd, &info, WEXITED | WNOHANG);
        if (code == -1) {
            if (errno == ECHILD || errno == EINTR) {
                return;
            }
            WARD_FATAL("waitid(P_PIDFD) failed: %s", strerror(errno));
        }
        if (info.si_code == 0) {
            return; // still running
        }

        char const *note = NULL;
        char note_buffer[192];
        enum WardStatus status = WS_PASS;

        switch (info.si_code) {
            case CLD_EXITED:
                if (info.si_status == WARD_CHILD_OK) {
                    break;
                }
                if (info.si_status == WARD_CHILD_SKIP) {
                    status = WS_SKIP;
                    break;
                }
                status = WS_FAIL;
                if (info.si_status != WARD_CHILD_FAIL) {
                    snprintf(note_buffer, sizeof(note_buffer),
                             "test process exited with status %d instead of reporting a result "
                             "(the test probably terminated the process itself)",
                             info.si_status);
                    note = note_buffer;
                }
                break;
            case CLD_KILLED:
            case CLD_DUMPED:
                status = slot->timed_out ? WS_TIMEOUT : WS_CRASH;
                snprintf(note_buffer, sizeof(note_buffer),
                         "test process died from %s (%d)%s",
                         strsignal(info.si_status), info.si_status,
                         info.si_code == CLD_DUMPED ? " (core dumped)" : "");
                note = note_buffer;
                break;
            case CLD_STOPPED:
                // A test must not stop itself; resume it and keep waiting.
                if (kill(slot->pid, SIGCONT) == -1 && errno != ESRCH) {
                    WARD_FATAL("SIGCONT failed: %s", strerror(errno));
                }
                continue;
            default:
                WARD_FATAL("unexpected waitid si_code %d", info.si_code);
        }

        finish_slot(loop, slot, status, note);
        return;
    }
}

static void cancel_slot(struct Loop *loop, struct Slot *slot) {
    (void) loop;
    // Signal the process group (negative pid), so grandchildren are killed too.
    if (slot->kill_deadline_ms < 0) {
        slot->timed_out = true;
        if (kill(-slot->pid, SIGTERM) == -1 && errno != ESRCH) {
            WARD_FATAL("SIGTERM failed: %s", strerror(errno));
        }
        slot->kill_deadline_ms = now_ms() + WARD_KILL_GRACE_MS;
        return;
    }
    if (now_ms() >= slot->kill_deadline_ms) {
        if (kill(-slot->pid, SIGKILL) == -1 && errno != ESRCH) {
            WARD_FATAL("SIGKILL failed: %s", strerror(errno));
        }
    }
}

int ward_run_parallel(struct WardRunner *runner) {
    struct Loop loop = {
        .runner = runner,
        .epfd = epoll_create1(EPOLL_CLOEXEC),
        .job_count = adjust_job_count(runner),
    };
    if (loop.epfd == -1) {
        WARD_FATAL("epoll_create1 failed: %s", strerror(errno));
    }

    loop.slots = ward_alloc((size_t) loop.job_count * sizeof(struct Slot));
    loop.outputs = ward_new_arr(&runner->arena, runner->case_count ? runner->case_count : 1,
                                struct OutputBuffer);

    for (int i = 0; i < loop.job_count; i++) {
        struct Slot *slot = &loop.slots[i];
        int pipes[2];
        if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) == -1) {
            WARD_FATAL("pipe2 failed: %s", strerror(errno));
        }
        slot->readfd = pipes[0];
        slot->writefd = pipes[1];
        slot->pidfd = -1;
        slot->pid = -1;
        slot->running = false;
        slot->test = NULL;
        epoll_add(loop.epfd, slot->readfd, (void *) ((uintptr_t) slot | EV_PIPE),
                  EPOLLIN | EPOLLET);
    }

    struct epoll_event events[WARD_MAX_PARALLELISM];
    for (;;) {
        for (int i = 0; i < loop.job_count; i++) {
            if (loop.slots[i].running) {
                continue;
            }
            if (loop.stop_scheduling || loop.next_case >= runner->selected_size) {
                break;
            }
            struct WardCase *test = runner->selected[loop.next_case++];
            if (test->ignored) {
                runner->attempted++;
                ward_report_status(runner, test, WS_SKIP);
                runner->ignored++;
                continue;
            }
            spawn_into_slot(&loop, &loop.slots[i], test);
        }

        if (loop.running == 0 && (loop.stop_scheduling || loop.next_case >= runner->selected_size)) {
            break;
        }

        long long timeout_ms = -1;
        long long now = now_ms();
        for (int i = 0; i < loop.job_count; i++) {
            struct Slot *slot = &loop.slots[i];
            if (!slot->running) {
                continue;
            }
            long long deadline = slot->kill_deadline_ms >= 0 ? slot->kill_deadline_ms
                                                             : slot->deadline_ms;
            long long left = deadline - now;
            if (timeout_ms < 0 || left < timeout_ms) {
                timeout_ms = left;
            }
        }
        if (timeout_ms < 0) {
            timeout_ms = 0;
        }

        int n = epoll_wait(loop.epfd, events, WARD_MAX_PARALLELISM,
                           loop.running ? (int) timeout_ms : 0);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            WARD_FATAL("epoll_wait failed: %s", strerror(errno));
        }

        now = now_ms();
        for (int i = 0; i < n; i++) {
            struct Slot *slot = (struct Slot *) ((uintptr_t) events[i].data.ptr & ~(uintptr_t) 3);
            uintptr_t kind = (uintptr_t) events[i].data.ptr & (uintptr_t) 3;
            if (!slot->running) {
                continue;
            }
            if (kind == EV_PIPE) {
                drain_pipe(slot);
            } else {
                poll_slot(&loop, slot);
            }
        }

        for (int i = 0; i < loop.job_count; i++) {
            struct Slot *slot = &loop.slots[i];
            if (!slot->running) {
                continue;
            }
            if (now >= slot->deadline_ms) {
                cancel_slot(&loop, slot);
            }
        }
    }

    for (int i = 0; i < loop.job_count; i++) {
        close(loop.slots[i].readfd);
        close(loop.slots[i].writefd);
    }
    for (size_t i = 0; i < runner->case_count; i++) {
        free(loop.outputs[i].bytes);
    }
    free(loop.slots);
    close(loop.epfd);
    return 0;
}
