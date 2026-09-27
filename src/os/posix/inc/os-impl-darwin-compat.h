/************************************************************************
 * NASA Docket No. GSC-18,719-1, and identified as "core Flight System: Bootes"
 *
 * Copyright (c) 2020 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/**
 * \file
 * \ingroup  posix
 *
 * POSIX facilities the OSAL POSIX-family implementations rely on that
 * macOS (Darwin) does not provide, supplied in-process:
 *
 *  - message queues (<mqueue.h> does not exist): a mutex, two condition
 *    variables and a ring buffer per queue.  Every OSAL queue (all cFE SB
 *    pipes) is private to the process and unlinked right after creation,
 *    so a queue name is never looked up again;
 *  - unnamed semaphores (sem_init() fails with ENOSYS): a mutex and
 *    condition variable counter, in place of the Darwin sem_t;
 *  - timer_t and struct itimerspec, for the NOS simulated timers;
 *  - clock_nanosleep() and pthread_mutex_timedlock().
 *
 * The standard names are mapped onto the OS_Darwin_* versions with macros.
 * This header is only used on __APPLE__ and only by the OSAL implementation
 * (never by applications); it must be included after the system headers it
 * replaces, which it includes itself.
 */

#ifndef OS_IMPL_DARWIN_COMPAT_H
#define OS_IMPL_DARWIN_COMPAT_H

#ifdef __APPLE__

#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>

/*
 * Message queues
 */
typedef struct OS_Darwin_mq *mqd_t;

struct mq_attr
{
    long mq_flags;   /* ignored */
    long mq_maxmsg;  /* maximum number of messages */
    long mq_msgsize; /* maximum message size */
    long mq_curmsgs; /* messages currently queued (mq_getattr only) */
};

mqd_t   OS_Darwin_mq_open(const char *name, int oflag, ...);
int     OS_Darwin_mq_close(mqd_t mqdes);
int     OS_Darwin_mq_unlink(const char *name);
int     OS_Darwin_mq_getattr(mqd_t mqdes, struct mq_attr *attr);
ssize_t OS_Darwin_mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio);
ssize_t OS_Darwin_mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio,
                                  const struct timespec *abs_timeout);
int     OS_Darwin_mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio);
int     OS_Darwin_mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio,
                               const struct timespec *abs_timeout);

#define mq_open         OS_Darwin_mq_open
#define mq_close        OS_Darwin_mq_close
#define mq_unlink       OS_Darwin_mq_unlink
#define mq_getattr      OS_Darwin_mq_getattr
#define mq_receive      OS_Darwin_mq_receive
#define mq_timedreceive OS_Darwin_mq_timedreceive
#define mq_send         OS_Darwin_mq_send
#define mq_timedsend    OS_Darwin_mq_timedsend

/*
 * Unnamed (process-private) counting semaphores
 */
typedef struct
{
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    unsigned int    value;
} OS_Darwin_sem_t;

int OS_Darwin_sem_init(OS_Darwin_sem_t *sem, int pshared, unsigned int value);
int OS_Darwin_sem_destroy(OS_Darwin_sem_t *sem);
int OS_Darwin_sem_post(OS_Darwin_sem_t *sem);
int OS_Darwin_sem_wait(OS_Darwin_sem_t *sem);
int OS_Darwin_sem_trywait(OS_Darwin_sem_t *sem);
int OS_Darwin_sem_timedwait(OS_Darwin_sem_t *sem, const struct timespec *abs_timeout);
int OS_Darwin_sem_getvalue(OS_Darwin_sem_t *sem, int *sval);

#define sem_t         OS_Darwin_sem_t
#define sem_init      OS_Darwin_sem_init
#define sem_destroy   OS_Darwin_sem_destroy
#define sem_post      OS_Darwin_sem_post
#define sem_wait      OS_Darwin_sem_wait
#define sem_trywait   OS_Darwin_sem_trywait
#define sem_timedwait OS_Darwin_sem_timedwait
#define sem_getvalue  OS_Darwin_sem_getvalue

/*
 * POSIX timers: types only (the NOS OSAL simulates the timers themselves)
 */
#include "os-impl-darwin-timer.h"

/*
 * clock_nanosleep: relative sleeps use nanosleep(); TIMER_ABSTIME sleeps
 * until the given time on the given clock.  Returns an error number, as
 * clock_nanosleep() does (not -1/errno).
 */
int OS_Darwin_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *req, struct timespec *rem);
#define clock_nanosleep OS_Darwin_clock_nanosleep

/*
 * pthread_mutex_timedlock: abs_timeout is on CLOCK_REALTIME.
 */
int OS_Darwin_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abs_timeout);
#define pthread_mutex_timedlock OS_Darwin_pthread_mutex_timedlock

#endif /* __APPLE__ */

#endif /* OS_IMPL_DARWIN_COMPAT_H */
