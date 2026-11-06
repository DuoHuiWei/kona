#ifndef KONA_BENCH_CONFIG_HPP
#define KONA_BENCH_CONFIG_HPP
#include <cstddef>
#include <cstdint>

// Single source for maintained experiment/protocol defaults. The Python
// organizer reads these decimal constants instead of duplicating their values.
// Keep DCF comparisons and multiplication items as separate units.
namespace KonaBenchConfig {
constexpr std::size_t DCF_CHUNK = 16384;
constexpr std::size_t MUL_CHUNK = 16384;
constexpr std::size_t LAN_THRESHOLD = 180;
constexpr std::size_t WAN_THRESHOLD = 6000;

constexpr std::uint64_t chunks(std::uint64_t items, std::uint64_t limit) {
    return items / limit + (items % limit != 0);
}
}
#endif
