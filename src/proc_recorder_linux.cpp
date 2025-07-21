/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __linux__

#include <cstdio>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <map>
#include <set>
#include "proc_recorder.h"
#include "os.h"
#include "log.h"

// Process history for CPU percentage calculation
struct ProcessHistory {
    unsigned long prevCpuTotal;
    unsigned long prevTimestamp;
    bool hasHistory;

    ProcessHistory() : prevCpuTotal(0), prevTimestamp(0), hasHistory(false) {}
};

// Map to store process history (PID -> ProcessHistory)
static std::map<int, ProcessHistory> processHistoryMap;

// Helper function to check if a string is a number (PID)
static bool isNumber(const char* str) {
    if (!str || *str == '\0') return false;
    for (const char* p = str; *p; p++) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

// Helper function to read process name from /proc/{pid}/comm
static bool readProcessName(int pid, char* name, size_t name_size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);

    FILE* file = fopen(path, "r");
    if (!file) return false;

    if (fgets(name, name_size, file)) {
        // Remove trailing newline
        size_t len = strlen(name);
        if (len > 0 && name[len-1] == '\n') {
            name[len-1] = '\0';
        }
        fclose(file);
        return true;
    }

    fclose(file);
    return false;
}

// Helper function to read process stats from /proc/{pid}/stat
static bool readProcessStat(int pid, ProcessData* data) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE* file = fopen(path, "r");
    if (!file) return false;

    char buffer[1024];
    if (!fgets(buffer, sizeof(buffer), file)) {
        fclose(file);
        return false;
    }

    // Parse the stat file - we need specific fields
    // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority nice num_threads itrealvalue starttime vsize rss ...
    int parsed_pid, parsed_ppid;
    char state;
    unsigned long utime, stime, starttime;
    int threads;

    int parsed = sscanf(buffer, "%d %*s %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %d %*d %lu",
                       &parsed_pid, &state, &parsed_ppid, &utime, &stime, &threads, &starttime);

    fclose(file);

    if (parsed >= 7) {
        data->pid = parsed_pid;
        data->ppid = parsed_ppid;
        data->state = (unsigned char)state;
        data->cpuUser = utime;
        data->cpuSystem = stime;
        data->threads = (unsigned short)threads;
        data->startTime = starttime;
        return true;
    }

    return false;
}

// Helper function to read memory stats from /proc/{pid}/statm
static bool readProcessStatm(int pid, ProcessData* data) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);

    FILE* file = fopen(path, "r");
    if (!file) return false;

    unsigned long size, resident, shared, text, lib, data_stack, dt;
    int parsed = fscanf(file, "%lu %lu %lu %lu %lu %lu %lu",
                       &size, &resident, &shared, &text, &lib, &data_stack, &dt);

    fclose(file);

    if (parsed >= 6) {
        data->memSize = size;
        data->memResident = resident;
        data->memShared = shared;
        data->memText = text;
        data->memData = data_stack;  // data + stack
        return true;
    }

    return false;
}

// Helper function to read I/O stats from /proc/{pid}/io
static bool readProcessIO(int pid, ProcessData* data) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);

    FILE* file = fopen(path, "r");
    if (!file) {
        // I/O stats may not be available for all processes
        data->ioRead = 0;
        data->ioWrite = 0;
        return false;
    }

    char line[256];
    unsigned long read_bytes = 0, write_bytes = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "read_bytes:", 11) == 0) {
            sscanf(line + 11, "%lu", &read_bytes);
        } else if (strncmp(line, "write_bytes:", 12) == 0) {
            sscanf(line + 12, "%lu", &write_bytes);
        }
    }

    fclose(file);
    data->ioRead = read_bytes;
    data->ioWrite = write_bytes;
    return true;
}

// Helper function to count file descriptors
static unsigned short countFileDescriptors(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);

    DIR* fd_dir = opendir(path);
    if (!fd_dir) return 0;

    unsigned short count = 0;
    struct dirent* entry;
    while ((entry = readdir(fd_dir))) {
        if (entry->d_name[0] != '.') {  // Skip . and ..
            count++;
        }
    }

    closedir(fd_dir);
    return count;
}

// Helper function to read UID from /proc/{pid}/status
static bool readProcessUID(int pid, ProcessData* data) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE* file = fopen(path, "r");
    if (!file) return false;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            unsigned int uid;
            if (sscanf(line + 4, "%u", &uid) == 1) {
                data->uid = uid;
                fclose(file);
                return true;
            }
        }
    }

    fclose(file);
    return false;
}

// Helper function to calculate CPU percentage
static float calculateCpuPercent(ProcessData* data) {
    int pid = data->pid;
    unsigned long currentCpuTotal = data->cpuUser + data->cpuSystem;
    unsigned long currentTime = data->lastUpdate;

    // Get or create process history
    ProcessHistory& history = processHistoryMap[pid];

    if (!history.hasHistory) {
        // First time seeing this process - no percentage available
        history.prevCpuTotal = currentCpuTotal;
        history.prevTimestamp = currentTime;
        history.hasHistory = true;
        return 0.0f;
    }

    // Calculate deltas
    unsigned long deltaCpu = currentCpuTotal - history.prevCpuTotal;
    unsigned long deltaTime = currentTime - history.prevTimestamp;

    float cpuPercent = 0.0f;

    if (deltaTime > 0) {
        // Convert CPU ticks to nanoseconds
        long clockTicksPerSec = sysconf(_SC_CLK_TCK);  // Usually 100 on Linux
        unsigned long cpuTimeNs = deltaCpu * (1000000000UL / clockTicksPerSec);

        // Calculate percentage: (CPU time used / Wall time elapsed) * 100
        cpuPercent = ((float)cpuTimeNs / deltaTime) * 100.0f;

        // Cap at reasonable maximum (e.g., 1000% for highly multi-threaded processes)
        if (cpuPercent > 1000.0f) {
            cpuPercent = 1000.0f;
        }
    }

    // Update history for next calculation
    history.prevCpuTotal = currentCpuTotal;
    history.prevTimestamp = currentTime;

    return cpuPercent;
}

// Helper function to clean up old process history entries
static void cleanupProcessHistory(const int* activePids, int pidCount) {
    // Create set of active PIDs for quick lookup
    std::set<int> activePidSet;
    for (int i = 0; i < pidCount; i++) {
        activePidSet.insert(activePids[i]);
    }

    // Remove history entries for processes that no longer exist
    auto it = processHistoryMap.begin();
    while (it != processHistoryMap.end()) {
        if (activePidSet.find(it->first) == activePidSet.end()) {
            it = processHistoryMap.erase(it);
        } else {
            ++it;
        }
    }
}

// Helper function to get all process PIDs
static void getAllProcessPids(int* pids, int* count, int max_pids) {
    *count = 0;

    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return;

    struct dirent* entry;
    while ((entry = readdir(proc_dir)) && *count < max_pids) {
        if (entry->d_type == DT_DIR && isNumber(entry->d_name)) {
            pids[*count] = atoi(entry->d_name);
            (*count)++;
        }
    }

    closedir(proc_dir);
}

// Platform-specific implementation for Linux
bool ProcRecorder::readProcessData(int pid, ProcessData* data) {
    // Initialize the struct
    *data = ProcessData();

    // Read process name
    if (!readProcessName(pid, data->name, sizeof(data->name))) {
        snprintf(data->name, sizeof(data->name), "pid-%d", pid);
    }

    // Read basic stats (pid, ppid, state, cpu times, threads, start time)
    if (!readProcessStat(pid, data)) {
        return false;  // Critical failure
    }

    // Read memory stats
    readProcessStatm(pid, data);

    // Read I/O stats (optional)
    readProcessIO(pid, data);

    // Read UID (optional)
    readProcessUID(pid, data);

    // Count file descriptors (optional, can be expensive)
    data->fds = countFileDescriptors(pid);

    // Set last update timestamp
    data->lastUpdate = OS::nanotime();

    // Calculate CPU percentage based on previous measurements
    data->cpuPercent = calculateCpuPercent(data);

    return true;
}

void ProcRecorder::getAllProcessPidsImpl(int* pids, int* count, int max_pids) {
    getAllProcessPids(pids, count, max_pids);
}

void ProcRecorder::cleanupProcessHistoryImpl(const int* activePids, int pidCount) {
    cleanupProcessHistory(activePids, pidCount);
}

size_t ProcRecorder::getProcessHistorySize() {
    return processHistoryMap.size();
}

#endif // __linux__
