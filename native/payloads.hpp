// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
namespace launcher {
inline constexpr const char* kUsbRoot = "/mnt/usb0/payloads";
struct Payload {
    std::string folder;
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    std::string filename;
    std::string path;
    std::string icon;
    std::string icon_png;
    bool remote = false;
};
struct ScanResult {
    std::vector<Payload> payloads;
    std::string message;
};
ScanResult scan(const std::string& root = kUsbRoot);
bool is_elf_file(const std::string& path);
bool is_safe_filename(const std::string& s);
// Requires ps5-payload-elfldr (or protocol-compatible daemon) at localhost:port.
// Returns a request-submission result, not proof of payload execution.
std::string request_launch(const Payload& payload, int port = 9021);
}
