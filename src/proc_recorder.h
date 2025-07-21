/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _PROCRECORDER_H
#define _PROCRECORDER_H

#include <stdint.h>
#include <pthread.h>
#include "engine.h"
#include "event.h"
#include "mutex.h"
#include "trap.h"
#include "os.h"

class ProcRecorder : public Engine {
  private:
    static volatile bool _running;
    static int _interval;  // Collection interval in milliseconds

    pthread_t _timer_thread;

    void timerLoop();
    void collectProcessMetrics();

    static void* threadEntry(void* proc_recorder) {
        ((ProcRecorder*)proc_recorder)->timerLoop();
        return NULL;
    }

  public:
    const char* type() {
        return "proc";
    }

    const char* title() {
        return "Process Metrics";
    }

    const char* units() {
        return "processes";
    }

    Error check(Arguments& args);
    Error start(Arguments& args);
    void stop();

    static inline bool running() {
        return _running;
    }
};

#endif // _PROCRECORDER_H
