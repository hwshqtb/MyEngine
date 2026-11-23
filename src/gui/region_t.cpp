#include "hwshqtb/gui/region_t.h"

#include <algorithm>
#include <utility>

namespace hwshqtb::gui {
    namespace {
        /// @brief Whether @p outer covers every pixel of @p inner.
        bool contains(rect_t outer, rect_t inner) noexcept {
            return outer.left() <= inner.left() && inner.right() <= outer.right() && outer.top() <= inner.top() && inner.bottom() <= outer.bottom();
        }

        /// @brief Whether uniting @p a and @p b yields exactly a rectangle (no extra pixel).
        ///
        /// That holds in two cases only: one rectangle covers the other, or they share a full edge
        /// (same extent across it). Partial overlaps and zero-width contact must stay separate —
        /// their bounding box would cover pixels that neither operand covers.
        bool merges_exactly(rect_t a, rect_t b) noexcept {
            if (a.isEmpty() || b.isEmpty()) {
                return false;
            }
            if (contains(a, b) || contains(b, a)) {
                return true;
            }
            // A full vertical edge is shared: same top and height, touching left-to-right.
            if (a.top() == b.top() && a.height() == b.height() && (a.right() == b.left() || b.right() == a.left())) {
                return true;
            }
            // A full horizontal edge is shared: same left and width, touching top-to-bottom.
            if (a.left() == b.left() && a.width() == b.width() && (a.bottom() == b.top() || b.bottom() == a.top())) {
                return true;
            }
            return false;
        }

        /// @brief The order the invariants prescribe: by y(), then by x(), with the extent as a
        ///        tie-break so that even a transient duplicate is placed deterministically.
        bool rect_before(rect_t a, rect_t b) noexcept {
            if (a.y() != b.y()) {
                return a.y() < b.y();
            }
            if (a.x() != b.x()) {
                return a.x() < b.x();
            }
            if (a.width() != b.width()) {
                return a.width() < b.width();
            }
            return a.height() < b.height();
        }

        /// @brief Append the at most four rectangles that @p src covers but @p cut does not.
        /// @param src The rectangle to carve; empty ones are dropped.
        /// @param cut The rectangle to remove; an empty one leaves @p src whole.
        /// @param out The list to append to; @p src is appended unchanged when it does not overlap.
        void append_subtracted(rect_t src, rect_t cut, std::vector<rect_t>& out) {
            if (src.isEmpty()) {
                return;
            }
            const rect_t overlap = src.intersected(cut);
            if (overlap.isEmpty()) {
                out.push_back(src);
                return;
            }
            // The band above and below span the full width; the two side pieces fill the rest of
            // the overlap's rows, so the four are disjoint and their union is src minus overlap.
            if (overlap.top() > src.top()) {
                out.push_back(rect_t{ src.left(), src.top(), src.width(), overlap.top() - src.top() });
            }
            if (overlap.bottom() < src.bottom()) {
                out.push_back(rect_t{ src.left(), overlap.bottom(), src.width(), src.bottom() - overlap.bottom() });
            }
            if (overlap.left() > src.left()) {
                out.push_back(rect_t{ src.left(), overlap.top(), overlap.left() - src.left(), overlap.height() });
            }
            if (overlap.right() < src.right()) {
                out.push_back(rect_t{ overlap.right(), overlap.top(), src.right() - overlap.right(), overlap.height() });
            }
        }

        /// @brief The smallest rectangle covering every rectangle in @p rects.
        /// @pre @p rects is not empty.
        rect_t bounds_of(const std::vector<rect_t>& rects) noexcept {
            rect_t result = rects.front();
            for (const rect_t& rect : rects) {
                result = result.united(rect);
            }
            return result;
        }
    }

    region_t::region_t(rect_t rect) {
        set(rect);
    }

    void region_t::set(rect_t rect) {
        _rects.clear();
        if (!rect.isEmpty()) {
            _rects.push_back(rect);
        }
    }

    void region_t::unite(rect_t rect) {
        if (rect.isEmpty()) {
            return;
        }
        // Already covered: the union is the region itself, and keeping the existing rectangle
        // avoids splitting it into the pieces a subtraction would produce.
        for (const rect_t& existing : _rects) {
            if (contains(existing, rect)) {
                return;
            }
        }
        std::vector<rect_t> next;
        next.reserve(_rects.size() + 4);
        for (const rect_t& existing : _rects) {
            append_subtracted(existing, rect, next);
        }
        next.push_back(rect);
        _rects = std::move(next);
        normalize();
    }

    void region_t::unite(const region_t& other) {
        if (other._rects.empty() || &other == this) {
            return;
        }
        std::vector<rect_t> next = _rects;
        for (const rect_t& added : other._rects) {
            bool covered = false;
            for (const rect_t& existing : next) {
                if (contains(existing, added)) {
                    covered = true;
                    break;
                }
            }
            if (covered) {
                continue;
            }
            std::vector<rect_t> carved;
            carved.reserve(next.size() + 4);
            for (const rect_t& existing : next) {
                append_subtracted(existing, added, carved);
            }
            carved.push_back(added);
            next = std::move(carved);
        }
        _rects = std::move(next);
        normalize();
    }

    void region_t::intersect(rect_t rect) {
        if (rect.isEmpty()) {
            _rects.clear();
            return;
        }
        std::vector<rect_t> next;
        next.reserve(_rects.size());
        for (const rect_t& existing : _rects) {
            const rect_t overlap = existing.intersected(rect);
            if (!overlap.isEmpty()) {
                next.push_back(overlap);
            }
        }
        _rects = std::move(next);
        normalize();
    }

    region_t region_t::subtracted(rect_t rect) const {
        region_t result;
        if (rect.isEmpty() || _rects.empty()) {
            result._rects = _rects;
            return result;
        }
        result._rects.reserve(_rects.size() + 4);
        for (const rect_t& existing : _rects) {
            append_subtracted(existing, rect, result._rects);
        }
        result.normalize();
        return result;
    }

    region_t region_t::subtracted(const region_t& other) const {
        region_t result = *this;
        for (const rect_t& cut : other._rects) {
            if (result._rects.empty()) {
                break;
            }
            result = result.subtracted(cut);
        }
        return result;
    }

    region_t region_t::intersected(const region_t& other) const {
        region_t result;
        if (_rects.empty() || other._rects.empty()) {
            return result; // no pixel is covered by both
        }
        // Pairwise rectangle intersections, united into the result: every resulting rectangle is
        // covered by both operands and every shared pixel is produced by exactly one pair.
        for (const rect_t& left : _rects) {
            for (const rect_t& right : other._rects) {
                const rect_t overlap = left.intersected(right);
                if (!overlap.isEmpty()) {
                    result.unite(overlap);
                }
            }
        }
        return result;
    }

    region_t region_t::translated(int dx, int dy) const {
        region_t result;
        if (dx == 0 && dy == 0) {
            result._rects = _rects;
            return result;
        }
        result._rects.reserve(_rects.size());
        for (const rect_t& rect : _rects) {
            result._rects.push_back(rect.translated(point_t{ dx, dy }));
        }
        result.normalize(); // translating preserves the invariants; normalize keeps them explicit
        return result;
    }

    region_t region_t::translated_within(rect_t area, int dx, int dy) const {
        if (area.isEmpty() || (dx == 0 && dy == 0) || _rects.empty()) {
            return *this;
        }
        // Everything outside the moved area describes pixels that did not move: it keeps its place.
        region_t result = subtracted(area);
        // Everything inside moved with its pixels; what lands outside the area is gone, because the
        // shift only moves pixels that stay inside it.
        for (const rect_t& existing : _rects) {
            const rect_t inside = existing.intersected(area);
            if (inside.isEmpty()) {
                continue;
            }
            const rect_t moved =
                inside.translated(point_t{ dx, dy }).intersected(area);
            if (!moved.isEmpty()) {
                result.unite(moved);
            }
        }
        return result;
    }

    bool region_t::is_empty() const noexcept {
        return _rects.empty();
    }

    rect_t region_t::bounds() const noexcept {
        if (_rects.empty()) {
            return rect_t{};
        }
        return bounds_of(_rects);
    }

    std::span<const rect_t> region_t::rects() const noexcept {
        return std::span<const rect_t>{ _rects };
    }

    std::size_t region_t::area() const noexcept {
        std::size_t total = 0;
        for (const rect_t& rect : _rects) {
            total += static_cast<std::size_t>(rect.width()) * static_cast<std::size_t>(rect.height());
        }
        return total;
    }

    std::size_t region_t::rectangle_count() const noexcept {
        return _rects.size();
    }

    void region_t::normalize() {
        _rects.erase(std::remove_if(_rects.begin(), _rects.end(), [](rect_t rect) { return rect.isEmpty(); }), _rects.end());
        if (_rects.empty()) {
            return;
        }

        // Merge one pair at a time and re-sort after every merge: a merged rectangle can become
        // mergeable with a partner that neither operand touched, so a single scan is not enough.
        for (;;) {
            std::sort(_rects.begin(), _rects.end(), rect_before);
            bool merged = false;
            for (std::size_t i = 0; i < _rects.size() && !merged; ++i) {
                for (std::size_t j = i + 1; j < _rects.size(); ++j) {
                    if (!merges_exactly(_rects[i], _rects[j])) {
                        continue;
                    }
                    _rects[i] = _rects[i].united(_rects[j]);
                    _rects.erase(_rects.begin() + static_cast<std::ptrdiff_t>(j));
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                break;
            }
        }

        // Too many rectangles to keep the algebra affordable: degrade to the bounding rectangle.
        // That covers MORE area than the exact set, which is the safe direction for a repaint -
        // extra pixels may be drawn, a united pixel can never be lost.
        if (_rects.size() > max_rectangles()) {
            _rects.assign(1, bounds_of(_rects));
        }
    }
}
