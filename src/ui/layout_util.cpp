#include "layout_util.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hwshqtb::ui::detail {
    namespace {
        /// @brief Qt's qGeomCalc overfull rule: cut the biggest slots first until they fit.
        ///
        /// The minimums are sorted and the smallest threshold T with sum(min(minimum, T)) <=
        /// @p space is looked up: the slots below T keep their minimum, the slots at or above it are
        /// capped at T minus their share of the pixels still missing (the first slots take the
        /// rounding remainder, one pixel each). A slot therefore ends below its own minimum whenever
        /// the minimums do not fit, and the biggest one loses the most.
        ///
        /// @param space The pixels left for the slots themselves, with the gaps already taken out; a
        ///        value <= 0 leaves every slot empty.
        /// @param minimums The compression floors; sum(minimums) is expected to exceed @p space.
        /// @param sizes Receives one extent per minimum, never negative.
        void cut_the_biggest_first(int space, std::span<const int> minimums, std::span<int> sizes) {
            const std::size_t count = minimums.size();
            if (space <= 0) {
                // Only the gaps fit: every slot is empty (nothing can be taken from a slot).
                for (std::size_t i = 0; i < count; ++i) {
                    sizes[i] = 0;
                }
                return;
            }

            std::vector<int> ordered(minimums.begin(), minimums.end());
            std::sort(ordered.begin(), ordered.end());

            const int slots = static_cast<int>(count);
            int index = 0;
            std::int64_t below = 0;
            std::int64_t used = 0;
            int threshold = 0;
            while (index < slots && used < space) {
                threshold = ordered[static_cast<std::size_t>(index)];
                used = below + static_cast<std::int64_t>(threshold) * (slots - index);
                below += threshold;
                ++index;
            }
            // space > 0 runs the loop once, and the minimums do not fit, so it stops on "used >=
            // space": index names the threshold slot and at least one slot is cut.
            --index;

            const std::int64_t deficit = used - space;
            const int cut_slots = slots - index;
            const int per_slot = static_cast<int>(deficit / cut_slots);
            const int remainder = static_cast<int>(deficit % cut_slots);
            const int common_cap = threshold - per_slot;

            int rest = 0;
            for (std::size_t i = 0; i < count; ++i) {
                int cap = common_cap;
                rest += remainder;
                if (rest >= cut_slots) {
                    --cap;
                    rest -= cut_slots;
                }
                const int size = minimums[i] < cap ? minimums[i] : cap;
                sizes[i] = size > 0 ? size : 0;
            }
        }
    }

    void distribute_by_weight(int extra, std::span<const int> weights, std::span<int> shares) {
        const std::size_t count = weights.size() < shares.size() ? weights.size() : shares.size();
        for (std::size_t i = 0; i < count; ++i) {
            shares[i] = 0;
        }
        if (extra <= 0 || count == 0) {
            return;
        }

        std::int64_t weight_sum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (weights[i] > 0) {
                weight_sum += weights[i];
            }
        }
        if (weight_sum <= 0) {
            // No slot may grow: the spare pixels stay unused (the caller keeps its hint sizes).
            return;
        }

        int given = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (weights[i] <= 0) {
                continue;
            }
            shares[i] = static_cast<int>(static_cast<std::int64_t>(extra) * weights[i] / weight_sum);
            given += shares[i];
        }
        // The floors lost at most one unit per positive weight, so this loop always finishes.
        int leftover = extra - given;
        for (std::size_t i = 0; i < count && leftover > 0; ++i) {
            if (weights[i] > 0) {
                ++shares[i];
                --leftover;
            }
        }
    }

    int clamped_extent(int hint, int minimum) {
        int result = hint > minimum ? hint : minimum;
        return result > 0 ? result : 0;
    }

    void resolve_extents(int available,
                         std::span<const int> hints,
                         std::span<const int> minimums,
                         std::span<const int> maximums,
                         std::span<const int> weights,
                         int gap,
                         std::span<int> sizes) {
        const std::size_t count = hints.size();
        const int spacing = count > 1 ? (gap > 0 ? gap : 0) * static_cast<int>(count - 1) : 0;
        int needed = 0;
        int floors = 0;
        for (std::size_t i = 0; i < count; ++i) {
            int size = hints[i];
            if (size < minimums[i]) {
                size = minimums[i];
            }
            if (size > maximums[i]) {
                size = maximums[i];
            }
            sizes[i] = size;
            needed += size;
            floors += minimums[i];
        }
        needed += spacing;
        floors += spacing;

        if (needed < available) {
            int remaining = available - needed;
            std::vector<int> growth_weights(count, 0);
            std::vector<int> shares(count, 0);
            while (remaining > 0) {
                bool can_grow = false;
                for (std::size_t i = 0; i < count; ++i) {
                    growth_weights[i] = weights[i] > 0 && sizes[i] < maximums[i] ? weights[i] : 0;
                    can_grow = can_grow || growth_weights[i] > 0;
                }
                if (!can_grow) {
                    // Every growing slot reached its maximum (or there is none): the rest stays unused.
                    break;
                }
                distribute_by_weight(remaining, growth_weights, shares);
                int given = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    if (shares[i] <= 0) {
                        continue;
                    }
                    const int room = maximums[i] - sizes[i];
                    const int take = shares[i] < room ? shares[i] : room;
                    sizes[i] += take;
                    given += take;
                }
                if (given <= 0) {
                    break;
                }
                remaining -= given;
            }
        } else if (needed > available) {
            if (floors > available) {
                // Not even the floors fit: the overfull rule cuts the biggest slots first, below
                // their minimum if it has to, so the axis still fits the available extent instead of
                // overflowing it. A gap is not a slot: it keeps its full value, so the slots get what
                // is left over and are all empty when the gaps alone fill the axis.
                cut_the_biggest_first(available - spacing, minimums, sizes);
            } else {
                // Above the floors but below the hints: remove the excess in proportion to the room
                // above the minimums, so a slot whose floor already equals its hint does not shrink.
                // The total room is positive here (needed > available >= floors); a caller that broke
                // that would get every share 0 from distribute_by_weight() and no change.
                const int excess = needed - available;
                std::vector<int> slack(count, 0);
                for (std::size_t i = 0; i < count; ++i) {
                    slack[i] = sizes[i] - minimums[i];
                }
                std::vector<int> reductions(count, 0);
                distribute_by_weight(excess, slack, reductions);
                for (std::size_t i = 0; i < count; ++i) {
                    sizes[i] = sizes[i] - reductions[i];
                }
            }
        }
        // A caller that passed a hint outside [minimum, maximum] still gets a legal extent. The lower
        // bound is applied on entry; only the upper bound is re-applied here, because the overfull
        // rule deliberately places slots below their minimum.
        for (std::size_t i = 0; i < count; ++i) {
            if (sizes[i] > maximums[i]) {
                sizes[i] = maximums[i];
            }
        }
    }
}
