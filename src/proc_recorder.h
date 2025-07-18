/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _PROCRECORDER_H
#define _PROCRECORDER_H

#include <stdint.h>
#include "engine.h"
#include "event.h"
#include "mutex.h"
#include "trap.h"

class ProcRecorder : public Engine {
  private:
  /*
    static u64 _interval;
    static bool _nofree;
    static volatile u64 _allocated_bytes;

    static Mutex _patch_lock;
    static int _patched_libs;
    static bool _initialized;
  */
    static volatile bool _running;

  public:
    const char* type() {
        return "Proc Recorder";
    }

    const char* title() {
        return "Process Records";
    }

    Error start(Arguments& args);
    void stop();

    static inline bool running() {
        return _running;
    }

};

#endif // _PROCRECORDER_H
