/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include<cstdio>
#include "proc_recorder.h"

Error ProcRecorder::start(Arguments& args) {
    fprintf(stderr, "ProcRecorder::start");
    // todo: start
    return Error::OK;
}

void ProcRecorder::stop() {
  fprintf(stderr, "ProcRecorder::stop");
  // todo: implement
}
