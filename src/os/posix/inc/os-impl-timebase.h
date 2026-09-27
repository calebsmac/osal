/************************************************************************
 * NASA Docket No. GSC-18,719-1, and identified as “core Flight System: Bootes”
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
 *
 * \ingroup  posix
 *
 */

#ifndef OS_IMPL_TIMEBASE_H
#define OS_IMPL_TIMEBASE_H

#include "osconfig.h"
#include <pthread.h>
#include <signal.h>
#ifdef __APPLE__
#include "os-impl-darwin-compat.h" /* timer_t */
#endif

typedef struct
{
    pthread_t       handler_thread;
    pthread_mutex_t handler_mutex;
    timer_t         host_timerid;
    int             assigned_signal;
    sigset_t        sigset;
    sig_atomic_t    reset_flag;
    struct timespec softsleep;
#ifdef __APPLE__
    /*
     * Darwin has no real-time signals, so a simulated timer tick is not a
     * queued signal to the handler thread: it increments tick_pending and
     * signals tick_cond (see the NOS OSAL's NOS_timer_fire()).
     */
    pthread_mutex_t tick_mutex;
    pthread_cond_t  tick_cond;
    unsigned long   tick_pending;
#endif
} OS_impl_timebase_internal_record_t;

/****************************************************************************************
                                   GLOBAL DATA
 ***************************************************************************************/

extern OS_impl_timebase_internal_record_t OS_impl_timebase_table[OS_MAX_TIMEBASES];

#endif /* OS_IMPL_TIMEBASE_H */
