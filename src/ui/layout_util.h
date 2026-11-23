#ifndef HWSHQTB__UI__DETAIL__LAYOUT_UTIL_H
#define HWSHQTB__UI__DETAIL__LAYOUT_UTIL_H

/// @file layout_util.h
/// @brief Shared integer arithmetic of the box and grid layouts (internal, not installed).

#include <span>

namespace hwshqtb::ui::detail {
    /// @brief Split @p extra whole pixels over @p weights so that the shares sum to exactly @p extra.
    ///
    /// The proportional share is floor(extra * weight / sum(weights)); the leftover units (strictly
    /// fewer than the number of positive weights) are handed out one each in index order, so the
    /// result is deterministic and never leaves a pixel unassigned. A weight <= 0 receives nothing;
    /// when every weight is <= 0 the spare pixels are left unused (every share stays 0), which is how
    /// a layout with no expanding child keeps its hint sizes. A non-positive @p extra zeroes every
    /// share.
    ///
    /// @param extra The pixels to distribute.
    /// @param weights The per-slot weights (a slot with weight 0 does not grow).
    /// @param shares Receives one share per weight; extra trailing slots of @p shares stay untouched.
    void distribute_by_weight(int extra, std::span<const int> weights, std::span<int> shares);

    /// @brief @p hint clamped to at least @p minimum and to at least zero.
    /// @param hint The natural extent.
    /// @param minimum The minimum extent.
    /// @return max(hint, minimum, 0).
    int clamped_extent(int hint, int minimum);

    /// @brief Resolve the per-child extents of one axis from hints, bounds and growth weights.
    ///
    /// The shared core of box_layout_t and grid_layout_t. Every slot starts at its hint clamped into
    /// [minimums[i], maximums[i]], and then one of three cases applies:
    ///  - the hints plus the gaps leave space over: the remainder is distributed over @p weights,
    ///    never raising a slot above its maximum; pixels a slot cannot take because its maximum is
    ///    reached are offered to the remaining growing slots in a further round, and stay unused only
    ///    when no slot can take them;
    ///  - the hints do not fit but every minimum does: the excess is removed in proportion to the
    ///    room above the minimums, so a slot whose minimum already equals its hint does not shrink.
    ///    Qt splits this band evenly instead, after taking the slots whose minimum reaches their hint
    ///    out of the pool; the outcome for those slots is the same (they keep their hint), only the
    ///    split among the remaining ones differs;
    ///  - not even the minimums fit ("overfull"): the Qt qGeomCalc rule below cuts the biggest slots
    ///    first, below their minimum when necessary, so the axis still fits @p available instead of
    ///    overflowing it. No extent ever goes negative, and the maximum of every slot is honoured.
    ///
    /// @par The overfull rule
    /// From qtbase/src/widgets/kernel/qlayoutengine.cpp, qGeomCalc: "Less space than minimumSize;
    /// take from the biggest first". The minimums are sorted and the smallest common cap T with
    /// sum(min(minimums[i], T)) <= available - gaps is looked up; the slots below T keep their
    /// minimum, and the slots at or above T are capped at T minus their share of the pixels still
    /// missing (the deficit is split evenly, the first slots taking the rounding remainder). The
    /// biggest slot therefore loses the most, and a slot whose minimum is below T is untouched.
    /// The rule is reproduced as it stands, except for the shrink of the SPACERS: this layout has no
    /// spacer items, @p gap is the caller's uniform spacing and the caller positions the slots with
    /// it, so it cannot be reported back from here. A gap therefore keeps its full value; when the
    /// gaps alone fill the axis every slot becomes empty and the axis is overrun by the gaps, which
    /// is the one case where the result does not fit @p available.
    ///
    /// All spans must have the same length; minimums[i] is in [0, maximums[i]]; @p gap is >= 0.
    ///
    /// @param available The extent to fill.
    /// @param hints The natural extents.
    /// @param minimums The compression floors (effective_minimum_size()).
    /// @param maximums The hard upper bounds per slot.
    /// @param weights The growth weights (only meaningful when there is a remainder).
    /// @param gap The gap between neighbours.
    /// @param sizes Receives the resolved extents (exactly @c hints.size() entries).
    void resolve_extents(int available,
                         std::span<const int> hints,
                         std::span<const int> minimums,
                         std::span<const int> maximums,
                         std::span<const int> weights,
                         int gap,
                         std::span<int> sizes);
}

#endif
