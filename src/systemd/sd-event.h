/* SPDX-License-Identifier: LGPL-2.1+ */
#ifndef foosdeventhfoo
#define foosdeventhfoo

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <inttypes.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <time.h>

#include "_sd-common.h"

/*
  Why is this better than pure epoll?

  - Supports event source prioritization
  - Scales better with a large number of time events because it does not require one timerfd each
  - Automatically tries to coalesce timer events system-wide
  - Handles signals and child PIDs
*/

_SD_BEGIN_DECLARATIONS;

#define SD_EVENT_DEFAULT ((sd_event *)1)

typedef struct sd_event sd_event;
typedef struct sd_event_source sd_event_source;

enum
{
    SD_EVENT_OFF = 0,
    SD_EVENT_ON = 1,
    SD_EVENT_ONESHOT = -1
};

// NOTE(ywen): "sd-event" is really not just about one particular event. In fact, it represents the
// event loop which is a state machine that handles events. This enum describes the different
// states that the event loop can be in. See sd_event_wait(3).
enum
{
    // NOTE(ywen): Event loop is ready to go for the next iteration.
    SD_EVENT_INITIAL,
    // NOTE(ywen): Event loop has been prepared and no event sources were ready to be dispatched.
    SD_EVENT_ARMED,
    // NOTE(ywen): There are event sources with events pending to be handled.
    SD_EVENT_PENDING,
    // NOTE(ywen): A regular event source is currently being dispatched.
    SD_EVENT_RUNNING,
    // NOTE(ywen): Similar to `SD_EVENT_RUNNING` but is the state in effect while dispatching exit event sources.
    SD_EVENT_EXITING,
    // NOTE(ywen): The event loop has exited.
    SD_EVENT_FINISHED,
    // NOTE(ywen): An event source is currently being prepared.
    SD_EVENT_PREPARING
};

enum
{
    /* And everything in-between and outside is good too */
    SD_EVENT_PRIORITY_IMPORTANT = -100,
    SD_EVENT_PRIORITY_NORMAL = 0,
    SD_EVENT_PRIORITY_IDLE = 100
};

typedef int (*sd_event_handler_t)(sd_event_source *s, void *userdata);
typedef int (*sd_event_io_handler_t)(sd_event_source *s, int fd, uint32_t revents, void *userdata);
typedef int (*sd_event_time_handler_t)(sd_event_source *s, uint64_t usec, void *userdata);
typedef int (*sd_event_signal_handler_t)(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata);
#if defined _GNU_SOURCE || _POSIX_C_SOURCE >= 199309L
typedef int (*sd_event_child_handler_t)(sd_event_source *s, const siginfo_t *si, void *userdata);
#else
typedef void *sd_event_child_handler_t;
#endif

int sd_event_default(sd_event **e);

int sd_event_new(sd_event **e);
sd_event *sd_event_ref(sd_event *e);
sd_event *sd_event_unref(sd_event *e);

int sd_event_add_io(sd_event *e, sd_event_source **s, int fd, uint32_t events, sd_event_io_handler_t callback, void *userdata);
int sd_event_add_time(sd_event *e, sd_event_source **s, clockid_t clock, uint64_t usec, uint64_t accuracy, sd_event_time_handler_t callback, void *userdata);
int sd_event_add_signal(sd_event *e, sd_event_source **s, int sig, sd_event_signal_handler_t callback, void *userdata);
int sd_event_add_child(sd_event *e, sd_event_source **s, pid_t pid, int options, sd_event_child_handler_t callback, void *userdata);
int sd_event_add_defer(sd_event *e, sd_event_source **s, sd_event_handler_t callback, void *userdata);
int sd_event_add_post(sd_event *e, sd_event_source **s, sd_event_handler_t callback, void *userdata);
int sd_event_add_exit(sd_event *e, sd_event_source **s, sd_event_handler_t callback, void *userdata);

int sd_event_prepare(sd_event *e);
int sd_event_wait(sd_event *e, uint64_t usec);
int sd_event_dispatch(sd_event *e);
int sd_event_run(sd_event *e, uint64_t usec);
int sd_event_loop(sd_event *e);
int sd_event_exit(sd_event *e, int code);

int sd_event_now(sd_event *e, clockid_t clock, uint64_t *usec);

int sd_event_get_fd(sd_event *e);
int sd_event_get_state(sd_event *e);
int sd_event_get_tid(sd_event *e, pid_t *tid);
int sd_event_get_exit_code(sd_event *e, int *code);
int sd_event_set_watchdog(sd_event *e, int b);
int sd_event_get_watchdog(sd_event *e);
int sd_event_get_iteration(sd_event *e, uint64_t *ret);

sd_event_source *sd_event_source_ref(sd_event_source *s);
sd_event_source *sd_event_source_unref(sd_event_source *s);

sd_event *sd_event_source_get_event(sd_event_source *s);
void *sd_event_source_get_userdata(sd_event_source *s);
void *sd_event_source_set_userdata(sd_event_source *s, void *userdata);

int sd_event_source_set_description(sd_event_source *s, const char *description);
int sd_event_source_get_description(sd_event_source *s, const char **description);
int sd_event_source_set_prepare(sd_event_source *s, sd_event_handler_t callback);
int sd_event_source_get_pending(sd_event_source *s);
int sd_event_source_get_priority(sd_event_source *s, int64_t *priority);
int sd_event_source_set_priority(sd_event_source *s, int64_t priority);
int sd_event_source_get_enabled(sd_event_source *s, int *enabled);
int sd_event_source_set_enabled(sd_event_source *s, int enabled);
int sd_event_source_get_io_fd(sd_event_source *s);
int sd_event_source_set_io_fd(sd_event_source *s, int fd);
int sd_event_source_get_io_fd_own(sd_event_source *s);
int sd_event_source_set_io_fd_own(sd_event_source *s, int own);
int sd_event_source_get_io_events(sd_event_source *s, uint32_t *events);
int sd_event_source_set_io_events(sd_event_source *s, uint32_t events);
int sd_event_source_get_io_revents(sd_event_source *s, uint32_t *revents);
int sd_event_source_get_time(sd_event_source *s, uint64_t *usec);
int sd_event_source_set_time(sd_event_source *s, uint64_t usec);
int sd_event_source_get_time_accuracy(sd_event_source *s, uint64_t *usec);
int sd_event_source_set_time_accuracy(sd_event_source *s, uint64_t usec);
int sd_event_source_get_time_clock(sd_event_source *s, clockid_t *clock);
int sd_event_source_get_signal(sd_event_source *s);
int sd_event_source_get_child_pid(sd_event_source *s, pid_t *pid);

/* Define helpers so that __attribute__((cleanup(sd_event_unrefp))) and similar may be used. */
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_event, sd_event_unref);
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_event_source, sd_event_source_unref);

_SD_END_DECLARATIONS;

#endif
