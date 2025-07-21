/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdio>
#include <pthread.h>
#include "proc_recorder.h"
#include "os.h"
#include "log.h"
#include "profiler.h"

volatile bool ProcRecorder::_running = false;
int ProcRecorder::_interval = 30000; // Default 30 seconds.

Error ProcRecorder::check(Arguments& args) {
    // Check if process profiling is supported on this platform
#ifdef __linux__
    return Error::OK;
#elif defined(__APPLE__)
    return Error("Process metrics collection is not yet implemented on macOS");
#elif defined(_WIN32)
    return Error("Process metrics collection is not yet implemented on Windows");
#else
    return Error("Process metrics collection is not supported on this platform");
#endif
}

Error ProcRecorder::start(Arguments& args) {
    // Double-check platform support (safety net)
#ifndef __linux__
    return Error("Process metrics collection is only supported on Linux");
#endif

    Log::info("Starting process metrics collection");

    // Set collection interval from arguments (convert nanoseconds to milliseconds)
    _interval = (int)(args._proc_interval / 1000000);  // Convert ns to ms
    if (_interval < 1000) {
        _interval = 1000;  // Minimum 1 second
    }

    Log::info("Process metrics collection interval set to %d ms", _interval);

    _running = true;

    int result = pthread_create(&_timer_thread, NULL, threadEntry, this);
    if (result != 0) {
        _running = false;
        Log::error("Failed to create process monitoring thread, error: %d", result);
        return Error("Unable to create process monitoring thread");
    }

    Log::info("Process monitoring thread created successfully");
    return Error::OK;
}

void ProcRecorder::stop() {
    Log::info("Stopping process metrics collection");

    if (_running) {
        _running = false;

        // Wake up the timer thread
        pthread_kill(_timer_thread, WAKEUP_SIGNAL);

        // Wait for thread to finish
        pthread_join(_timer_thread, NULL);
    }
}

void ProcRecorder::timerLoop() {
    Log::info("Process metrics collection thread started - entering timer loop");
    fprintf(stderr, "[PROC] Timer loop started\n");

    int loop_count = 0;
    while (_running) {
        loop_count++;
        fprintf(stderr, "[PROC] Timer loop iteration %d\n", loop_count);

        // Collect process metrics
        collectProcessMetrics();

        // Only sleep if we're still running
        if (_running) {
            fprintf(stderr, "[PROC] Sleeping for %d ms\n", _interval);
            // Sleep for the specified interval
            // Convert milliseconds to nanoseconds for OS::sleep
            OS::sleep((u64)_interval * 1000000);
        }
    }

    Log::info("Process metrics collection thread stopped - exiting timer loop");
    fprintf(stderr, "[PROC] Timer loop stopped\n");
}

void ProcRecorder::collectProcessMetrics() {
    fprintf(stderr, "[PROC] Collecting process metrics from system\n");

    static int collection_count = 0;
    collection_count++;

    // Get all process PIDs using platform-specific implementation
    const int MAX_PIDS = 100; // Limit for testing
    int pids[MAX_PIDS];
    int pid_count = 0;

    getAllProcessPidsImpl(pids, &pid_count, MAX_PIDS);

    fprintf(stderr, "[PROC] Found %d processes (limited to %d)\n", pid_count, MAX_PIDS);

    // Clean up history for processes that no longer exist
    cleanupProcessHistoryImpl(pids, pid_count);

    // Get Profiler instance
    Profiler* profiler = Profiler::instance();
    bool jfr_active = (profiler != NULL);

    if (jfr_active) {
        fprintf(stderr, "[PROC] Profiler instance available, will record process events\n");
    } else {
        fprintf(stderr, "[PROC] Profiler instance not available, only logging to stderr\n");
    }

    // Process first 10 processes for detailed logging and JFR recording
    int processes_to_log = (pid_count > 10) ? 10 : pid_count;
    int successful_reads = 0;

    for (int i = 0; i < processes_to_log; i++) {
        int pid = pids[i];
        ProcessData data;

        // Read complete process data using platform-specific implementation
        if (readProcessData(pid, &data)) {
            successful_reads++;

            // Log to stderr for debugging (now includes CPU percentage)
            fprintf(stderr, "[PROC] PID: %d, Name: %s, PPID: %d, State: %c, "
                           "CPU(U/S): %lu/%lu, CPU%%: %.2f%%, Mem(Size/Res/Shared): %lu/%lu/%lu pages, "
                           "I/O(R/W): %lu/%lu bytes, Threads: %d, FDs: %d\n",
                   data.pid, data.name, data.ppid, (char)data.state,
                   data.cpuUser, data.cpuSystem, data.cpuPercent,
                   data.memSize, data.memResident, data.memShared,
                   data.ioRead, data.ioWrite, data.threads, data.fds);

            // Record to JFR if profiler is available
            if (jfr_active) {
                ProcessEvent event(&data);
                profiler->recordEventOnly(PROCESS_SAMPLE, &event);
            }
        } else {
            fprintf(stderr, "[PROC] PID: %d - failed to read process data\n", pid);
        }
    }

    if (pid_count > processes_to_log) {
        fprintf(stderr, "[PROC] ... and %d more processes (not shown)\n", pid_count - processes_to_log);
    }

    fprintf(stderr, "[PROC] Process metrics collection #%d completed - successfully read %d/%d processes, tracking %zu process histories\n",
           collection_count, successful_reads, processes_to_log, getProcessHistorySize());
}
