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
 * The POSIX timer types Darwin does not define (timer_t, struct itimerspec,
 * TIMER_ABSTIME).  Types only: the NOS OSAL simulates the timers on NOS
 * Engine time.  Kept apart from os-impl-darwin-compat.h (which also maps
 * queue and semaphore calls) so that NOS-time.h users outside the OSAL,
 * such as the NOS PSP, get the types and nothing else.
 */

#ifndef OS_IMPL_DARWIN_TIMER_H
#define OS_IMPL_DARWIN_TIMER_H

#ifdef __APPLE__

#include <time.h>

typedef void *timer_t;

struct itimerspec
{
    struct timespec it_interval;
    struct timespec it_value;
};

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

#endif /* __APPLE__ */

#endif /* OS_IMPL_DARWIN_TIMER_H */
