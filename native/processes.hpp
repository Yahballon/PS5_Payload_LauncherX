// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace launcher {
struct RunningProcess {
    int pid = 0;
    std::string name;
    double memoryMiB = 0;
    bool daemon = false;
};
struct ProcessSnapshot {
    bool available = false;
    std::string message;
    std::vector<RunningProcess> processes;
};

// Uses ps5-payload-manager's existing localhost process API (default 8084).
// Without that running manager the result is explicitly unavailable.
ProcessSnapshot fetch_running_processes(int port = 8084);
bool parse_process_snapshot(std::string_view json, ProcessSnapshot& result);

// Only a confirmed ELF daemon from a freshly fetched snapshot is stoppable.
// Protect the loader and process manager, which host these operations.
bool process_is_stoppable(const RunningProcess& process);
bool kill_running_process(const RunningProcess& process, std::string& message, int port = 8084);
}  // namespace launcher
