#ifndef HWSHQTB__GUI__REGION_T_H
#define HWSHQTB__GUI__REGION_T_H

/// @file region_t.h
/// @brief region_t: a set of disjoint integer rectangles and the algebra over them.

#include <cstddef>
#include <span>
#include <vector>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    /// @brief A set of DISJOINT rectangles in one coordinate system (the Qt QRegion equivalent).
    ///
    /// @par Invariants
    /// Every operation re-establishes the following, so every region at rest satisfies all of them:
    ///  - the rectangles never overlap, and touching ones (sharing a full edge) are merged;
    ///  - the order is deterministic: by y(), then by x();
    ///  - an empty or inverted rectangle (width <= 0 or height <= 0) is ignored by every operation;
    ///  - when the rectangle count would exceed max_rectangles(), the region degrades to its bounding
    ///    rectangle. Degrading to MORE area is always safe, never to less: a consumer that repaints
    ///    whatever the region covers may repaint extra pixels, but it can never miss a pixel that was
    ///    united in.
    ///
    /// @par Accuracy versus the rectangle cap
    /// area() is exact while the rectangles are disjoint, i.e. always except after degradation,
    /// where it is the bounding rectangle's area and therefore an upper bound on the covered area.
    /// rectangle_count() <= max_rectangles() always holds, and that cap is what bounds the quadratic
    /// subtracted(const region_t&).
    ///
    /// @par Coordinate system
    /// The rectangles live in the caller's coordinate system; the region stores them verbatim and
    /// performs no mapping. rect_t is half-open — right() and bottom() are one past the last covered
    /// pixel — and the algebra here relies on it: rectangles whose edges merely touch with zero-width
    /// contact do not overlap each other.
    ///
    /// @par Validity of rects()
    /// The span returned by rects() points into the region and is invalidated by the next mutation
    /// (set / unite / intersect) or by the region's destruction.
    ///
    /// @par Thread safety
    /// Not thread safe: one region must be used from one thread at a time.
    class HWSHQTB_GUI_EXPORT region_t {
        std::vector<rect_t> _rects;  ///< The covered rectangles: disjoint, merged, ordered by y() then x().

        /// @brief Re-establish the invariants after a mutation: drop empty rectangles, merge the
        ///        mergeable ones, order the rest by y() then x(), and enforce the rectangle cap.
        void normalize();

    public:
        /// @brief Construct an empty region: no rectangle, bounds() == rect_t{}, area() == 0.
        region_t() = default;

        /// @brief Construct a region covering exactly @p rect.
        /// @param rect The rectangle to cover; an empty or inverted rectangle gives an empty region.
        explicit region_t(rect_t rect);

        /// @brief Replace the contents with the single rectangle @p rect.
        /// @param rect The new covered rectangle; an empty or inverted rectangle empties the region.
        void set(rect_t rect);

        /// @brief Add the area of @p rect to the region.
        /// @param rect The rectangle to add. An empty or inverted rectangle is ignored, so the region
        ///        is left unchanged rather than emptied.
        void unite(rect_t rect);

        /// @brief Add the whole area of @p other to this region.
        /// @param other The region to add; an empty one is ignored. Uniting with *this leaves the
        ///        region unchanged.
        void unite(const region_t& other);

        /// @brief Restrict the region to the area that @p rect covers as well.
        /// @param rect The rectangle to intersect with. An empty or inverted rectangle empties the
        ///        region, because no pixel is covered by both.
        void intersect(rect_t rect);

        /// @brief This region minus the area of @p rect, as a new region.
        /// @param rect The rectangle to remove. An empty or inverted rectangle, and one that does not
        ///        overlap the region, leave the result equal to *this.
        /// @return A region covering exactly the pixels of this one that @p rect does not cover: at
        ///         most four rectangles per input rectangle.
        region_t subtracted(rect_t rect) const;

        /// @brief This region minus the area of @p other, as a new region.
        /// @param other The region to remove; an empty one gives a copy of *this.
        /// @return A region covering exactly the pixels of this one that @p other does not cover.
        /// @note The cost is quadratic in the rectangle count, which is why the count is capped; a
        ///       loop of subtracted(rect_t) is the cheaper equivalent when @p other has few
        ///       rectangles.
        region_t subtracted(const region_t& other) const;

        /// @brief This region intersected with the area of @p other, as a new region.
        /// @param other The region to intersect with; an empty one gives an empty result.
        /// @return A region covering exactly the pixels covered by both.
        /// @note The cost is quadratic in the rectangle counts, like subtracted(const region_t&); a
        ///       loop of intersect(rect_t) is the cheaper equivalent when @p other has few
        ///       rectangles.
        region_t intersected(const region_t& other) const;

        /// @brief This region with every rectangle moved by (@p dx, @p dy), as a new region.
        /// @param dx The horizontal shift: a pixel at x() ends up at x() + @p dx.
        /// @param dy The vertical shift: a pixel at y() ends up at y() + @p dy.
        /// @return The translated region. Nothing is clipped: unlike translated_within(), a pixel
        ///         that leaves the original coordinates is kept where it moved to.
        /// @note The paint traversal uses it to carry a damage region into an item's coordinates.
        region_t translated(int dx, int dy) const;

        /// @brief This region with the part inside @p area moved by (@p dx, @p dy).
        /// @param area The rectangle whose pixels moved (the scrolled rectangle).
        /// @param dx The horizontal shift: a pixel at x() ends up at x() + @p dx.
        /// @param dy The vertical shift: a pixel at y() ends up at y() + @p dy.
        /// @return The transformed region: the part outside @p area keeps its place, the part inside
        ///         is moved and clipped back into @p area (a moved pixel that lands outside it is
        ///         dropped, because nothing of it can be painted any more).
        /// @note Damage describes pixels: when an area's pixels move, the damage inside that area has
        ///       to move with them, or the frame repaints the place the content LEFT and the stale
        ///       band stays. A scroll applies this to the damage it has pending BEFORE it unites the
        ///       band the shift exposed — that band is new damage and must not move a second time.
        region_t translated_within(rect_t area, int dx, int dy) const;

        /// @brief Whether the region covers no pixel.
        /// @return true when the region holds no rectangle.
        bool is_empty() const noexcept;

        /// @brief The smallest rectangle covering every rectangle of the region.
        /// @return The bounding rectangle, or a default-constructed rect_t{} when the region is empty.
        rect_t bounds() const noexcept;

        /// @brief The covered rectangles.
        /// @return A view over the normalized rectangles, ordered by y() then x(); empty when the
        ///         region is empty. The span is invalidated by the next mutation of the region.
        std::span<const rect_t> rects() const noexcept;

        /// @brief The covered area in square pixels.
        /// @return The sum of the rectangle areas: exact while the rectangles are disjoint, and the
        ///         bounding rectangle's area after degradation.
        std::size_t area() const noexcept;

        /// @brief The number of rectangles in rects().
        /// @return A value in [0, max_rectangles()].
        std::size_t rectangle_count() const noexcept;

        /// @brief The rectangle count above which a region degrades to its bounding rectangle.
        /// @return The cap, 64.
        static constexpr std::size_t max_rectangles() noexcept {
            return 64;
        }
    };
}

#endif
