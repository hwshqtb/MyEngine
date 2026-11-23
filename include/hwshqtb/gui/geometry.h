#ifndef HWSHQTB__GUI__GEOMETRY_H
#define HWSHQTB__GUI__GEOMETRY_H

/// @file geometry.h
/// @brief The integer geometry value types of the GUI layer: point_t, extent_t and rect_t.
///
/// @par Header-only by design
/// These are plain value types whose members must be usable in constant expressions (layout
/// arithmetic checked at compile time). Defining the members out of line in a .cpp would make every
/// call non-constexpr, so this header is the ONE sanctioned exception to the project rule
/// "declarations in headers, bodies in .cpp".
///
/// @par Coordinates
/// The GUI layer uses a top-left origin: x grows to the right, y grows downwards, and all values are
/// whole pixels.
///
/// @par Half-open rectangles
/// rect_t follows the half-open convention: right() == x() + width(), bottom() == y() + height(),
/// and a point on the right or bottom edge is NOT contained. Every later hit test, clip and fill
/// operation relies on this (Qt's QRect uses closed rectangles instead, so its arithmetic must not
/// be copied over).

namespace hwshqtb::gui {
    /// @brief The four edge gaps of a box (the QMargins equivalent): left, top, right, bottom.
    ///
    /// A layout subtracts horizontal() from the available width and vertical() from the available
    /// height, and offsets the content origin by left()/top(). The order of the constructor arguments
    /// is the Qt one (left, top, right, bottom).
    class margins_t {
        int _left = 0;
        int _top = 0;
        int _right = 0;
        int _bottom = 0;

    public:
        /// @brief Construct zero margins on every edge.
        constexpr margins_t() noexcept = default;

        /// @brief Construct the four edge gaps.
        /// @param left The left gap.
        /// @param top The top gap.
        /// @param right The right gap.
        /// @param bottom The bottom gap.
        constexpr margins_t(int left, int top, int right, int bottom) noexcept:
            _left(left),
            _top(top),
            _right(right),
            _bottom(bottom) { }

        /// @brief The left gap.
        constexpr int left() const noexcept {
            return _left;
        }

        /// @brief The top gap.
        constexpr int top() const noexcept {
            return _top;
        }

        /// @brief The right gap.
        constexpr int right() const noexcept {
            return _right;
        }

        /// @brief The bottom gap.
        constexpr int bottom() const noexcept {
            return _bottom;
        }

        /// @brief The total horizontal gap: left() + right().
        constexpr int horizontal() const noexcept {
            return _left + _right;
        }

        /// @brief The total vertical gap: top() + bottom().
        constexpr int vertical() const noexcept {
            return _top + _bottom;
        }

        /// @brief Whether every edge gap is equal.
        friend constexpr bool operator==(margins_t a, margins_t b) noexcept {
            return a._left == b._left && a._top == b._top && a._right == b._right && a._bottom == b._bottom;
        }
    };

    /// @brief A position in the two-dimensional integer coordinate system (the QPoint equivalent).
    class point_t {
        int _x = 0;
        int _y = 0;

    public:
        /// @brief Construct the origin (0, 0).
        constexpr point_t() noexcept = default;

        /// @brief Construct the point (x, y).
        /// @param x The horizontal coordinate (grows to the right).
        /// @param y The vertical coordinate (grows downwards).
        constexpr point_t(int x, int y) noexcept:
            _x(x),
            _y(y) { }

        /// @brief The horizontal coordinate.
        constexpr int x() const noexcept {
            return _x;
        }

        /// @brief The vertical coordinate.
        constexpr int y() const noexcept {
            return _y;
        }

        /// @brief Set the horizontal coordinate.
        constexpr void set_x(int x) noexcept {
            _x = x;
        }

        /// @brief Set the vertical coordinate.
        constexpr void set_y(int y) noexcept {
            _y = y;
        }

        /// @brief Add @p offset component-wise.
        /// @return *this.
        constexpr point_t& operator+=(point_t offset) noexcept {
            _x += offset._x;
            _y += offset._y;
            return *this;
        }

        /// @brief Subtract @p offset component-wise.
        /// @return *this.
        constexpr point_t& operator-=(point_t offset) noexcept {
            _x -= offset._x;
            _y -= offset._y;
            return *this;
        }

        /// @brief The component-wise sum of two points.
        friend constexpr point_t operator+(point_t a, point_t b) noexcept {
            return point_t{ a._x + b._x, a._y + b._y };
        }

        /// @brief The component-wise difference of two points.
        friend constexpr point_t operator-(point_t a, point_t b) noexcept {
            return point_t{ a._x - b._x, a._y - b._y };
        }

        /// @brief Whether both coordinates are equal.
        friend constexpr bool operator==(point_t a, point_t b) noexcept {
            return a._x == b._x && a._y == b._y;
        }

        /// @brief The Manhattan length |x()| + |y()|.
        /// @note A component equal to INT_MIN would overflow the negation; coordinates are pixel
        ///       offsets, so this is not guarded against.
        constexpr int manhattan_length() const noexcept {
            const int ax = _x < 0 ? -_x : _x;
            const int ay = _y < 0 ? -_y : _y;
            return ax + ay;
        }
    };

    /// @brief A width/height pair (the QSize equivalent).
    /// @note Deliberately not named size_t: that name would clash with std::size_t in every
    ///       translation unit that uses both namespaces.
    class extent_t {
        int _w = 0;
        int _h = 0;

    public:
        /// @brief Construct the empty extent (0 x 0).
        constexpr extent_t() noexcept = default;

        /// @brief Construct the extent w x h.
        /// @param w The width.
        /// @param h The height.
        constexpr extent_t(int w, int h) noexcept:
            _w(w),
            _h(h) { }

        /// @brief The width.
        constexpr int width() const noexcept {
            return _w;
        }

        /// @brief The height.
        constexpr int height() const noexcept {
            return _h;
        }

        /// @brief Set the width.
        constexpr void set_width(int w) noexcept {
            _w = w;
        }

        /// @brief Set the height.
        constexpr void set_height(int h) noexcept {
            _h = h;
        }

        /// @brief Whether either side is non-positive.
        /// @note A negative extent is empty as well: width() <= 0 || height() <= 0.
        constexpr bool isEmpty() const noexcept {
            return _w <= 0 || _h <= 0;
        }

        /// @brief The component-wise sum of two extents (QSize + QSize).
        friend constexpr extent_t operator+(extent_t a, extent_t b) noexcept {
            return extent_t{ a._w + b._w, a._h + b._h };
        }

        /// @brief Whether both sides are equal.
        friend constexpr bool operator==(extent_t a, extent_t b) noexcept {
            return a._w == b._w && a._h == b._h;
        }
    };

    /// @brief An axis-aligned integer rectangle: a top-left position plus an extent.
    ///
    /// @par Half-open semantics
    /// The rectangle covers the pixels with left() <= x < right() and top() <= y < bottom(), i.e.
    /// right() == x() + width() and bottom() == y() + height(). A point exactly on the right or
    /// bottom edge is outside. This differs from QRect, whose right() is x() + width() - 1.
    ///
    /// @par Empty rectangles
    /// A rectangle is empty when width() <= 0 or height() <= 0 (a negative extent is empty too). An
    /// empty rectangle contains no point, it is ignored by united(), and intersected() with it is
    /// empty.
    class rect_t {
        int _x = 0;
        int _y = 0;
        int _w = 0;
        int _h = 0;

    public:
        /// @brief Construct an empty rectangle at the origin.
        constexpr rect_t() noexcept = default;

        /// @brief Construct from a position and an extent.
        /// @param x The left coordinate.
        /// @param y The top coordinate.
        /// @param w The width.
        /// @param h The height.
        constexpr rect_t(int x, int y, int w, int h) noexcept:
            _x(x),
            _y(y),
            _w(w),
            _h(h) { }

        /// @brief Construct from a top-left corner and an extent.
        /// @param top_left The left/top corner.
        /// @param size The width/height pair.
        constexpr rect_t(point_t top_left, extent_t size) noexcept:
            _x(top_left.x()),
            _y(top_left.y()),
            _w(size.width()),
            _h(size.height()) { }

        /// @brief The left coordinate.
        constexpr int x() const noexcept {
            return _x;
        }

        /// @brief The top coordinate.
        constexpr int y() const noexcept {
            return _y;
        }

        /// @brief The width.
        constexpr int width() const noexcept {
            return _w;
        }

        /// @brief The height.
        constexpr int height() const noexcept {
            return _h;
        }

        /// @brief The left coordinate (the same value as x()).
        constexpr int left() const noexcept {
            return _x;
        }

        /// @brief The top coordinate (the same value as y()).
        constexpr int top() const noexcept {
            return _y;
        }

        /// @brief One past the last covered column: x() + width().
        /// @note Half-open: this coordinate is NOT covered by the rectangle.
        constexpr int right() const noexcept {
            return _x + _w;
        }

        /// @brief One past the last covered row: y() + height().
        /// @note Half-open: this coordinate is NOT covered by the rectangle.
        constexpr int bottom() const noexcept {
            return _y + _h;
        }

        /// @brief The top-left corner (x(), y()).
        constexpr point_t top_left() const noexcept {
            return point_t{ _x, _y };
        }

        /// @brief The integer centre.
        /// @note Computed as x() + width() / 2, y() + height() / 2 (the same formula as
        ///       QRect::center()): an even extent gives the exact centre, an odd extent truncates
        ///       the division towards zero.
        constexpr point_t center() const noexcept {
            return point_t{ _x + _w / 2, _y + _h / 2 };
        }

        /// @brief Whether the rectangle has no area: width() <= 0 || height() <= 0.
        constexpr bool isEmpty() const noexcept {
            return _w <= 0 || _h <= 0;
        }

        /// @brief Whether @p p lies inside the half-open cell.
        /// @param p The point to test.
        /// @return true when left() <= p.x() < right() and top() <= p.y() < bottom().
        /// @note The right() and bottom() coordinates are outside, and an empty rectangle contains
        ///       nothing.
        constexpr bool contains(point_t p) const noexcept {
            return p.x() >= left() && p.x() < right() && p.y() >= top() && p.y() < bottom();
        }

        /// @brief The intersection of this rectangle with @p other.
        /// @param other The rectangle to intersect with.
        /// @return The half-open rectangle covered by both operands; a default-constructed empty
        ///         rectangle at the origin when they do not overlap (touching edges count as
        ///         non-overlapping) or when either operand is empty.
        constexpr rect_t intersected(rect_t other) const noexcept {
            const int l = left() > other.left() ? left() : other.left();
            const int t = top() > other.top() ? top() : other.top();
            const int r = right() < other.right() ? right() : other.right();
            const int b = bottom() < other.bottom() ? bottom() : other.bottom();
            if (r <= l || b <= t) {
                return rect_t{};
            }
            return rect_t{ l, t, r - l, b - t };
        }

        /// @brief The smallest rectangle covering both operands.
        /// @param other The rectangle to unite with.
        /// @return The bounding rectangle of the two. An empty operand is ignored, so uniting with
        ///         an empty rectangle returns the other operand unchanged (both empty gives an
        ///         empty rectangle).
        constexpr rect_t united(rect_t other) const noexcept {
            if (isEmpty()) {
                return other;
            }
            if (other.isEmpty()) {
                return *this;
            }
            const int l = left() < other.left() ? left() : other.left();
            const int t = top() < other.top() ? top() : other.top();
            const int r = right() > other.right() ? right() : other.right();
            const int b = bottom() > other.bottom() ? bottom() : other.bottom();
            return rect_t{ l, t, r - l, b - t };
        }

        /// @brief This rectangle shifted by @p offset.
        /// @param offset The translation; the extent is unchanged.
        constexpr rect_t translated(point_t offset) const noexcept {
            return rect_t{ _x + offset.x(), _y + offset.y(), _w, _h };
        }

        /// @brief Whether position and extent are all equal.
        friend constexpr bool operator==(rect_t a, rect_t b) noexcept {
            return a._x == b._x && a._y == b._y && a._w == b._w && a._h == b._h;
        }
    };
}

#endif
