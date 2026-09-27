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
 * In-process replacements for the POSIX facilities macOS lacks; see
 * os-impl-darwin-compat.h.  Built only on __APPLE__.
 *
 * Every blocking wait is a pthread_cond_wait()/pthread_cond_timedwait(),
 * which are cancellation points like the calls they replace; a cleanup
 * handler releases the lock if a waiting task is deleted (pthread_cancel).
 */

#include "os-impl-darwin-compat.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Defaults Linux applies when mq_open() gets no attributes */
#define OS_DARWIN_MQ_DEFAULT_MAXMSG  10
#define OS_DARWIN_MQ_DEFAULT_MSGSIZE 8192

struct OS_Darwin_mq
{
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    long            maxmsg;
    long            msgsize;
    long            head;  /* slot of the oldest queued message */
    long            count; /* messages queued */
    size_t *        lengths;
    char *          data; /* maxmsg slots of msgsize bytes */
};

static void OS_Darwin_UnlockCleanup(void *arg)
{
    pthread_mutex_unlock((pthread_mutex_t *)arg);
}

static int OS_Darwin_TimespecValid(const struct timespec *ts)
{
    return ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000L;
}

/*
 * Waits on cond until *pred becomes nonzero.  abs_timeout (CLOCK_REALTIME)
 * may be NULL to wait forever.  Called and returns with the lock held.
 * Returns 0 or ETIMEDOUT.
 */
static int OS_Darwin_WaitUntil(pthread_cond_t *cond, pthread_mutex_t *lock, const long *pred,
                               const struct timespec *abs_timeout)
{
    int rc = 0;

    while (*pred == 0)
    {
        if (abs_timeout == NULL)
        {
            pthread_cond_wait(cond, lock);
        }
        else
        {
            rc = pthread_cond_timedwait(cond, lock, abs_timeout);
            if (rc == ETIMEDOUT)
            {
                /* the condition may have become true at the deadline */
                rc = (*pred != 0) ? 0 : ETIMEDOUT;
                break;
            }
            rc = 0;
        }
    }

    return rc;
}

/****************************************************************************************
                                   MESSAGE QUEUES
 ***************************************************************************************/

mqd_t OS_Darwin_mq_open(const char *name, int oflag, ...)
{
    struct OS_Darwin_mq *q;
    struct mq_attr *     attr = NULL;
    long                 maxmsg;
    long                 msgsize;
    va_list              ap;

    (void)name; /* queues are process-private and never reopened by name */

    if ((oflag & O_CREAT) == 0)
    {
        errno = ENOENT;
        return (mqd_t)(-1);
    }

    va_start(ap, oflag);
    (void)va_arg(ap, int); /* mode (mode_t is promoted to int) */
    attr = va_arg(ap, struct mq_attr *);
    va_end(ap);

    maxmsg  = OS_DARWIN_MQ_DEFAULT_MAXMSG;
    msgsize = OS_DARWIN_MQ_DEFAULT_MSGSIZE;
    if (attr != NULL)
    {
        maxmsg  = attr->mq_maxmsg;
        msgsize = attr->mq_msgsize;
    }
    if (maxmsg <= 0 || msgsize <= 0)
    {
        errno = EINVAL;
        return (mqd_t)(-1);
    }

    q = calloc(1, sizeof(*q));
    if (q == NULL)
    {
        errno = ENOMEM;
        return (mqd_t)(-1);
    }

    q->maxmsg  = maxmsg;
    q->msgsize = msgsize;
    q->lengths = calloc((size_t)maxmsg, sizeof(size_t));
    q->data    = malloc((size_t)maxmsg * (size_t)msgsize);
    if (q->lengths == NULL || q->data == NULL)
    {
        free(q->lengths);
        free(q->data);
        free(q);
        errno = ENOMEM;
        return (mqd_t)(-1);
    }

    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return q;
}

int OS_Darwin_mq_close(mqd_t q)
{
    if (q == NULL || q == (mqd_t)(-1))
    {
        errno = EBADF;
        return -1;
    }

    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->lock);
    free(q->lengths);
    free(q->data);
    free(q);
    return 0;
}

int OS_Darwin_mq_unlink(const char *name)
{
    (void)name; /* nothing is registered by name */
    return 0;
}

int OS_Darwin_mq_getattr(mqd_t q, struct mq_attr *attr)
{
    if (q == NULL || q == (mqd_t)(-1))
    {
        errno = EBADF;
        return -1;
    }

    pthread_mutex_lock(&q->lock);
    memset(attr, 0, sizeof(*attr));
    attr->mq_maxmsg  = q->maxmsg;
    attr->mq_msgsize = q->msgsize;
    attr->mq_curmsgs = q->count;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

ssize_t OS_Darwin_mq_timedreceive(mqd_t q, char *msg_ptr, size_t msg_len, unsigned int *msg_prio,
                                  const struct timespec *abs_timeout)
{
    ssize_t result = -1;
    int     err    = 0;
    size_t  len;

    if (q == NULL || q == (mqd_t)(-1))
    {
        errno = EBADF;
        return -1;
    }

    /* As POSIX: the buffer must hold the largest message the queue allows */
    if (msg_len < (size_t)q->msgsize)
    {
        errno = EMSGSIZE;
        return -1;
    }

    if (abs_timeout != NULL && !OS_Darwin_TimespecValid(abs_timeout))
    {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&q->lock);
    pthread_cleanup_push(OS_Darwin_UnlockCleanup, &q->lock);

    err = OS_Darwin_WaitUntil(&q->not_empty, &q->lock, &q->count, abs_timeout);
    if (err == 0)
    {
        len = q->lengths[q->head];
        memcpy(msg_ptr, q->data + (size_t)q->head * (size_t)q->msgsize, len);
        q->head = (q->head + 1) % q->maxmsg;
        --q->count;
        if (msg_prio != NULL)
        {
            *msg_prio = 0;
        }
        result = (ssize_t)len;
        pthread_cond_signal(&q->not_full);
    }

    pthread_cleanup_pop(1);

    if (err != 0)
    {
        errno = err;
    }
    return result;
}

ssize_t OS_Darwin_mq_receive(mqd_t q, char *msg_ptr, size_t msg_len, unsigned int *msg_prio)
{
    return OS_Darwin_mq_timedreceive(q, msg_ptr, msg_len, msg_prio, NULL);
}

int OS_Darwin_mq_timedsend(mqd_t q, const char *msg_ptr, size_t msg_len, unsigned int msg_prio,
                           const struct timespec *abs_timeout)
{
    int  err = 0;
    long space;
    long tail;

    (void)msg_prio; /* delivery is FIFO (the OSAL sends every message at one priority) */

    if (q == NULL || q == (mqd_t)(-1))
    {
        errno = EBADF;
        return -1;
    }

    if (msg_len > (size_t)q->msgsize)
    {
        errno = EMSGSIZE;
        return -1;
    }

    if (abs_timeout != NULL && !OS_Darwin_TimespecValid(abs_timeout))
    {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&q->lock);
    pthread_cleanup_push(OS_Darwin_UnlockCleanup, &q->lock);

    /* "space" is recomputed as the predicate each time the queue changes */
    space = q->maxmsg - q->count;
    while (space == 0 && err == 0)
    {
        if (abs_timeout == NULL)
        {
            pthread_cond_wait(&q->not_full, &q->lock);
        }
        else if (pthread_cond_timedwait(&q->not_full, &q->lock, abs_timeout) == ETIMEDOUT)
        {
            err = (q->maxmsg - q->count) > 0 ? 0 : ETIMEDOUT;
        }
        space = q->maxmsg - q->count;
    }

    if (err == 0)
    {
        tail = (q->head + q->count) % q->maxmsg;
        memcpy(q->data + (size_t)tail * (size_t)q->msgsize, msg_ptr, msg_len);
        q->lengths[tail] = msg_len;
        ++q->count;
        pthread_cond_signal(&q->not_empty);
    }

    pthread_cleanup_pop(1);

    if (err != 0)
    {
        errno = err;
        return -1;
    }
    return 0;
}

int OS_Darwin_mq_send(mqd_t q, const char *msg_ptr, size_t msg_len, unsigned int msg_prio)
{
    return OS_Darwin_mq_timedsend(q, msg_ptr, msg_len, msg_prio, NULL);
}

/****************************************************************************************
                                     SEMAPHORES
 ***************************************************************************************/

int OS_Darwin_sem_init(OS_Darwin_sem_t *sem, int pshared, unsigned int value)
{
    if (pshared != 0)
    {
        /* process-shared semaphores are not needed by the OSAL */
        errno = ENOSYS;
        return -1;
    }

    if (pthread_mutex_init(&sem->lock, NULL) != 0)
    {
        errno = ENOSPC;
        return -1;
    }
    if (pthread_cond_init(&sem->cond, NULL) != 0)
    {
        pthread_mutex_destroy(&sem->lock);
        errno = ENOSPC;
        return -1;
    }
    sem->value = value;
    return 0;
}

int OS_Darwin_sem_destroy(OS_Darwin_sem_t *sem)
{
    pthread_cond_destroy(&sem->cond);
    pthread_mutex_destroy(&sem->lock);
    return 0;
}

int OS_Darwin_sem_post(OS_Darwin_sem_t *sem)
{
    pthread_mutex_lock(&sem->lock);
    ++sem->value;
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->lock);
    return 0;
}

static int OS_Darwin_SemTake(OS_Darwin_sem_t *sem, const struct timespec *abs_timeout)
{
    int err = 0;

    pthread_mutex_lock(&sem->lock);
    pthread_cleanup_push(OS_Darwin_UnlockCleanup, &sem->lock);

    while (sem->value == 0 && err == 0)
    {
        if (abs_timeout == NULL)
        {
            pthread_cond_wait(&sem->cond, &sem->lock);
        }
        else if (pthread_cond_timedwait(&sem->cond, &sem->lock, abs_timeout) == ETIMEDOUT)
        {
            err = (sem->value != 0) ? 0 : ETIMEDOUT;
        }
    }

    if (err == 0)
    {
        --sem->value;
    }

    pthread_cleanup_pop(1);

    if (err != 0)
    {
        errno = err;
        return -1;
    }
    return 0;
}

int OS_Darwin_sem_wait(OS_Darwin_sem_t *sem)
{
    return OS_Darwin_SemTake(sem, NULL);
}

int OS_Darwin_sem_trywait(OS_Darwin_sem_t *sem)
{
    int rc = 0;

    pthread_mutex_lock(&sem->lock);
    if (sem->value > 0)
    {
        --sem->value;
    }
    else
    {
        rc = -1;
    }
    pthread_mutex_unlock(&sem->lock);

    if (rc != 0)
    {
        errno = EAGAIN;
    }
    return rc;
}

int OS_Darwin_sem_timedwait(OS_Darwin_sem_t *sem, const struct timespec *abs_timeout)
{
    if (abs_timeout == NULL || !OS_Darwin_TimespecValid(abs_timeout))
    {
        errno = EINVAL;
        return -1;
    }
    return OS_Darwin_SemTake(sem, abs_timeout);
}

int OS_Darwin_sem_getvalue(OS_Darwin_sem_t *sem, int *sval)
{
    pthread_mutex_lock(&sem->lock);
    *sval = (int)sem->value;
    pthread_mutex_unlock(&sem->lock);
    return 0;
}

/****************************************************************************************
                                  TIME AND MUTEXES
 ***************************************************************************************/

int OS_Darwin_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *req, struct timespec *rem)
{
    struct timespec rel;
    struct timespec now;

    if (!OS_Darwin_TimespecValid(req) || req->tv_sec < 0)
    {
        return EINVAL;
    }

    if ((flags & TIMER_ABSTIME) != 0)
    {
        if (clock_gettime(clock_id, &now) != 0)
        {
            return errno;
        }
        rel.tv_sec  = req->tv_sec - now.tv_sec;
        rel.tv_nsec = req->tv_nsec - now.tv_nsec;
        if (rel.tv_nsec < 0)
        {
            rel.tv_nsec += 1000000000L;
            --rel.tv_sec;
        }
        if (rel.tv_sec < 0)
        {
            return 0; /* already past */
        }
        rem = NULL; /* not updated for absolute sleeps */
    }
    else
    {
        rel = *req;
    }

    if (nanosleep(&rel, rem) != 0)
    {
        return errno;
    }
    return 0;
}

int OS_Darwin_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abs_timeout)
{
    struct timespec now;
    struct timespec pause;
    int             rc;

    for (;;)
    {
        rc = pthread_mutex_trylock(mutex);
        if (rc != EBUSY)
        {
            return rc;
        }

        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > abs_timeout->tv_sec ||
            (now.tv_sec == abs_timeout->tv_sec && now.tv_nsec >= abs_timeout->tv_nsec))
        {
            return ETIMEDOUT;
        }

        /* the lock is expected to be held only briefly: poll every 100 us */
        pause.tv_sec  = 0;
        pause.tv_nsec = 100000;
        nanosleep(&pause, NULL);
    }
}
