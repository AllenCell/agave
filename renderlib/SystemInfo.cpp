#include "SystemInfo.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#else // macOS
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

namespace SystemInfo {

std::uint64_t
availableMemoryBytes()
{
#ifdef _WIN32
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof(statex);
  if (GlobalMemoryStatusEx(&statex)) {
    return static_cast<std::uint64_t>(statex.ullAvailPhys);
  }
  return 0;
#elif defined(__linux__)
  // Prefer MemAvailable from /proc/meminfo: this is the kernel's own
  // estimate of how much memory can be allocated without swapping, and
  // unlike sysinfo.freeram it includes reclaimable page cache and slab.
  // On a typical box freeram alone is under 5% of total even when most
  // memory is reclaimable.
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo.is_open()) {
    std::string line;
    while (std::getline(meminfo, line)) {
      // Format: "MemAvailable:    12345678 kB"
      if (line.rfind("MemAvailable:", 0) == 0) {
        std::istringstream iss(line.substr(13));
        std::uint64_t kb = 0;
        iss >> kb;
        if (kb > 0) {
          return kb * 1024ULL;
        }
        break;
      }
    }
  }
  // Older kernels (<3.14) don't expose MemAvailable. Fall back to
  // sysinfo, and at least add bufferram to freeram so we don't grossly
  // under-report.
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    std::uint64_t bytes = static_cast<std::uint64_t>(info.freeram) + static_cast<std::uint64_t>(info.bufferram);
    return bytes * static_cast<std::uint64_t>(info.mem_unit);
  }
  return 0;
#else // macOS
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vm_stats;
  mach_port_t host_port = mach_host_self();

  if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
    vm_size_t page_size = 0;
    host_page_size(host_port, &page_size);

    // free_count alone is what's truly free right now; macOS keeps that
    // small on purpose and uses inactive/purgeable/speculative pages as
    // reclaimable buffer (this is what Activity Monitor reports as
    // "Available"). Including them avoids massive under-reporting.
    std::uint64_t reclaimablePages = static_cast<std::uint64_t>(vm_stats.free_count) +
                                     static_cast<std::uint64_t>(vm_stats.inactive_count) +
                                     static_cast<std::uint64_t>(vm_stats.purgeable_count) +
                                     static_cast<std::uint64_t>(vm_stats.speculative_count);
    return reclaimablePages * static_cast<std::uint64_t>(page_size);
  }
  return 0;
#endif
}

std::uint64_t
availableDiskBytes(const std::string& path)
{
  if (path.empty()) {
    return 0;
  }
  std::error_code ec;
  std::filesystem::space_info info = std::filesystem::space(path, ec);
  if (ec) {
    // path may not exist yet; try its parent.
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
      return 0;
    }
    info = std::filesystem::space(parent, ec);
    if (ec) {
      return 0;
    }
  }
  return static_cast<std::uint64_t>(info.available);
}

} // namespace SystemInfo
