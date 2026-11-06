#ifndef KONA_BENCH_AFFINITY_HPP
#define KONA_BENCH_AFFINITY_HPP

#include <sched.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Benchmark setup only. Never call from a protocol primitive or timed region.
// Fixed project mapping: P0 -> logical CPU 0; P1 -> logical CPU 2.
// Affinity alone does not reserve a physical core or isolate its SMT siblings.
namespace KonaBenchAffinity {
inline int cpu_for_party(int party)
{
    if (party != 0 && party != 1)
        throw std::invalid_argument("benchmark party must be 0 or 1");
    return party == 0 ? 0 : 2;
}
inline std::string topology(int cpu, const char* name)
{
    std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/" + name);
    std::string value;
    return std::getline(f, value) ? value : "UNKNOWN";
}
inline void bind_fixed_cpu(int party)
{
    const int cpu = cpu_for_party(party);
    cpu_set_t before;
    if (sched_getaffinity(0, sizeof(before), &before) != 0)
        throw std::runtime_error(std::string("sched_getaffinity failed: ") + std::strerror(errno));
    if (!CPU_ISSET(cpu, &before))
        throw std::runtime_error("fixed CPU " + std::to_string(cpu) +
                " is excluded by inherited affinity/cpuset; refusing to change the project mapping");
    const bool inherited = CPU_COUNT(&before) == 1;
    if (!inherited)
    {
        cpu_set_t target;
        CPU_ZERO(&target); CPU_SET(cpu, &target);
        if (sched_setaffinity(0, sizeof(target), &target) != 0)
            throw std::runtime_error(std::string("sched_setaffinity failed: ") + std::strerror(errno));
    }
    cpu_set_t actual;
    if (sched_getaffinity(0, sizeof(actual), &actual) != 0 ||
            CPU_COUNT(&actual) != 1 || !CPU_ISSET(cpu, &actual))
        throw std::runtime_error("fixed benchmark CPU affinity verification failed");
    const auto package0 = topology(0, "physical_package_id");
    const auto package1 = topology(2, "physical_package_id");
    const auto core0 = topology(0, "core_id");
    const auto core1 = topology(2, "core_id");
    const bool known = package0 != "UNKNOWN" && package1 != "UNKNOWN" && core0 != "UNKNOWN" && core1 != "UNKNOWN";
    if (known && package0 == package1 && core0 == core1)
        throw std::runtime_error("fixed CPUs 0 and 2 share one visible physical core; do not substitute other CPUs");
    std::cout << "BENCH_AFFINITY party=" << party << " cpu=" << cpu
              << " source=" << (inherited ? "inherited_verified" : "cpp_bench")
              << " affinity_check=PASS package=" << topology(cpu, "physical_package_id")
              << " core=" << topology(cpu, "core_id")
              << " thread_siblings=" << topology(cpu, "thread_siblings_list")
              << " physical_core_separation=" << (known ? "VISIBLE_TOPOLOGY_PASS" : "UNKNOWN")
              << " core_exclusivity=NOT_VERIFIED" << std::endl;
}
} // namespace KonaBenchAffinity
#endif
