/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __APPLE__

#include "proc_recorder.h"

// macOS implementation
bool ProcRecorder::readProcessData(int pid, ProcessData* data) { return false; }
void ProcRecorder::getAllProcessPidsImpl(int* pids, int* count, int max_pids) { *count = 0; }
void ProcRecorder::cleanupProcessHistoryImpl(const int* activePids, int pidCount) { }
size_t ProcRecorder::getProcessHistorySize() { return 0; }

#endif // __APPLE__
