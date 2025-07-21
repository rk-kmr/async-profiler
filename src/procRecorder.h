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

// Forward declarations
class FlightRecorder;

// Process data structure combining static info and dynamic metrics
struct ProcessData {
    // Static Process Information
    int pid;
    int ppid;
    char name[16];              // Process name from /proc/{pid}/comm
    unsigned int uid;           // User ID
    unsigned long startTime;    // Process start time (clock ticks since boot)
    unsigned char state;        // Process state (R, S, D, Z, T, etc.)

    // Dynamic CPU Metrics
    unsigned long cpuUser;      // User CPU time (clock ticks)
    unsigned long cpuSystem;    // System CPU time (clock ticks)
    float cpuPercent;           // CPU utilization percentage
    unsigned short threads;     // Number of threads

    // Memory Metrics (from /proc/{pid}/statm, in pages)
    unsigned long memSize;      // Total virtual memory size
    unsigned long memResident;  // Physical memory in RAM
    unsigned long memShared;    // Shared memory pages
    unsigned long memText;      // Code/executable pages
    unsigned long memData;      // Data + stack pages

    // I/O Metrics
    unsigned long ioRead;       // Bytes read from storage
    unsigned long ioWrite;      // Bytes written to storage

    // File Descriptors
    unsigned short fds;         // Number of open file descriptors

    // Cache management
    unsigned long lastUpdate;   // Timestamp of last update

    ProcessData() : pid(0), ppid(0), uid(0), startTime(0), state(0),
                   cpuUser(0), cpuSystem(0), cpuPercent(0.0f), threads(0),
                   memSize(0), memResident(0), memShared(0), memText(0), memData(0),
                   ioRead(0), ioWrite(0), fds(0), lastUpdate(0) {
        name[0] = '\0';
    }
};

class ProcRecorder : public Engine {
  private:
    static volatile bool _running;
    static long _interval;  // Collection interval in milliseconds

    pthread_t _timer_thread;

    void timerLoop();
    void collectProcessMetrics();

    // Platform-specific methods (implemented in proc_recorder_*.cpp)
    bool readProcessData(int pid, ProcessData* data);
    void getAllProcessPidsImpl(int* pids, int* count, int max_pids);
    void cleanupProcessHistoryImpl(const int* activePids, int pidCount);
    size_t getProcessHistorySize();

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
