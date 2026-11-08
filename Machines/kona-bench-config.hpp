#ifndef KONA_BENCH_CONFIG_HPP
#define KONA_BENCH_CONFIG_HPP
#include <cstddef>
#include <cstdint>
#include <limits>

// Single source for maintained experiment/protocol defaults. The Python
// organizer reads these decimal constants instead of duplicating their values.
// Keep DCF comparisons and multiplication items as separate units.
namespace KonaBenchConfig {
constexpr std::size_t DCF_CHUNK = 16384;
constexpr std::size_t MUL_CHUNK = 16384;
constexpr std::size_t MATRIX_CHUNKING = 0; // 0=whole batch, 1=bounded
// Runtime override is scoped to explicitly opted-in experiment processes.
// Other schemes retain the historical constants above. INT_MAX also respects
// Compare64's signed indexing; supported vectors must fit that index range.
inline bool& chunking_enabled() { static thread_local bool enabled = true; return enabled; }
inline std::size_t dcf_chunk() { return chunking_enabled() ? DCF_CHUNK : std::numeric_limits<int>::max(); }
inline std::size_t mul_chunk() { return chunking_enabled() ? MUL_CHUNK : std::numeric_limits<int>::max(); }
struct ScopedChunking {
    bool previous;
    explicit ScopedChunking(bool enabled) : previous(chunking_enabled()) { chunking_enabled() = enabled; }
    ~ScopedChunking() { chunking_enabled() = previous; }
    ScopedChunking(const ScopedChunking&) = delete;
    ScopedChunking& operator=(const ScopedChunking&) = delete;
};
constexpr std::size_t LAN_THRESHOLD = 180;
constexpr std::size_t WAN_THRESHOLD = 26000;

constexpr std::uint64_t chunks(std::uint64_t items, std::uint64_t limit) {
    return items / limit + (items % limit != 0);
}
}
#endif
