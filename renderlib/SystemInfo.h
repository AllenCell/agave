#pragma once

#include <cstdint>
#include <string>

namespace SystemInfo {

// Returns an estimate of physical memory that can be allocated without
// swapping, in bytes. Returns 0 if the value cannot be determined.
std::uint64_t
availableMemoryBytes();

// Returns bytes available to a non-privileged user on the filesystem
// containing `path`. Returns 0 if `path` is empty or the value cannot
// be determined.
std::uint64_t
availableDiskBytes(const std::string& path);

} // namespace SystemInfo
