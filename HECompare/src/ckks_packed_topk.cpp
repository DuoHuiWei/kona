#include "hecompare/ckks_packed_topk.hpp"

#include <stdexcept>
#include <utility>

namespace hecompare {
namespace {

bool is_power_of_two(std::size_t value)
{
    return value != 0 &&
           (value & (value - 1)) == 0;
}

void validate_candidates(
        const std::vector<PackedBinaryLabelCandidate>& candidates)
{
    if (candidates.size() < 2)
        throw std::invalid_argument(
                "Bitonic sort needs at least two candidates");

    if (!is_power_of_two(candidates.size()))
        throw std::invalid_argument(
                "Bitonic sort candidate count must be a power of two");

    const std::size_t label_bit_count =
            candidates.front().label_bits.size();

    if (label_bit_count == 0)
        throw std::invalid_argument(
                "Bitonic sort candidates have no label bits");

    for (const auto& candidate : candidates) {
        if (candidate.label_bits.size() !=
            label_bit_count) {
            throw std::invalid_argument(
                    "Bitonic sort label plane counts do not match");
        }
    }
}

} // namespace

PackedBitonicSortResult packed_bitonic_sort_ascending(
        const CkksEnvironment& environment,
        std::vector<PackedBinaryLabelCandidate> candidates,
        uint32_t active_count)
{
    validate_candidates(candidates);

    PackedBitonicSortResult result;
    const std::size_t n = candidates.size();

    for (std::size_t sequence_size = 2;
         sequence_size <= n;
         sequence_size <<= 1) {
        for (std::size_t stride =
                    sequence_size >> 1;
             stride > 0;
             stride >>= 1) {
            ++result.network_depth;

            for (std::size_t wire = 0;
                 wire < n;
                 ++wire) {
                const std::size_t partner =
                        wire ^ stride;

                if (partner <= wire)
                    continue;

                const bool ascending =
                        (wire & sequence_size) == 0;

                auto ordered =
                        packed_compare_and_swap_binary_labels_ascending(
                                environment,
                                candidates[wire],
                                candidates[partner],
                                active_count);

                ++result.comparator_count;

                if (ascending) {
                    candidates[wire] =
                            std::move(ordered.first);
                    candidates[partner] =
                            std::move(ordered.second);
                }
                else {
                    candidates[wire] =
                            std::move(ordered.second);
                    candidates[partner] =
                            std::move(ordered.first);
                }
            }
        }
    }

    result.sorted_candidates =
            std::move(candidates);
    return result;
}

} // namespace hecompare
