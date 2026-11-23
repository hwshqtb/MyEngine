#ifndef HWSHQTB__GUI__TRANSFORM_T_H
#define HWSHQTB__GUI__TRANSFORM_T_H

/// @file transform_t.h
/// @brief The 2x3 affine transform value type of the GUI layer (the minimum QTransform subset).

#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    /// @brief A two-dimensional affine transformation: a 2x2 linear part plus a translation.
    ///
    /// @par Matrix layout
    /// The six coefficients are the first two columns of a homogeneous 3x3 matrix:
    /// @code
    /// | m11  m21  dx |      x' = m11 * x + m21 * y + dx
    /// | m12  m22  dy |      y' = m12 * x + m22 * y + dy
    /// |  0    0    1 |
    /// @endcode
    /// Points are column vectors, so composition reads naturally: `a * b` is the transform that
    /// applies @p b first and @p a second. (QTransform multiplies row vectors and therefore composes
    /// in the opposite order; that quirk is deliberately not copied.)
    ///
    /// @par Header-only by design
    /// A constexpr value type, so — like geometry.h and color_t.h — it is the sanctioned exception to
    /// "declarations in headers, bodies in .cpp": layout arithmetic must be evaluable at compile time
    /// in every translation unit.
    ///
    /// @par Scope (H2)
    /// This is the real 2x3 affine, not a placeholder: it stores rotation and skew coefficients and
    /// composes them exactly. Only the factories the paint state needs exist (identity, translation,
    /// uniform-per-axis scale); rotation helpers and inverted() are G1 work and are deliberately
    /// absent.
    class transform_t {
        double _m11 = 1.0;
        double _m12 = 0.0;
        double _m21 = 0.0;
        double _m22 = 1.0;
        double _dx = 0.0;
        double _dy = 0.0;

    public:
        /// @brief Construct the identity transform (m11 = m22 = 1, everything else 0).
        constexpr transform_t() noexcept = default;

        /// @brief Construct from all six coefficients.
        /// @param m11 Coefficient of x in x'.
        /// @param m12 Coefficient of x in y'.
        /// @param m21 Coefficient of y in x'.
        /// @param m22 Coefficient of y in y'.
        /// @param dx The x translation.
        /// @param dy The y translation.
        constexpr transform_t(double m11, double m12, double m21, double m22, double dx, double dy) noexcept:
            _m11(m11),
            _m12(m12),
            _m21(m21),
            _m22(m22),
            _dx(dx),
            _dy(dy) { }

        /// @brief The coefficient of x in x'.
        constexpr double m11() const noexcept {
            return _m11;
        }

        /// @brief The coefficient of x in y'.
        constexpr double m12() const noexcept {
            return _m12;
        }

        /// @brief The coefficient of y in x'.
        constexpr double m21() const noexcept {
            return _m21;
        }

        /// @brief The coefficient of y in y'.
        constexpr double m22() const noexcept {
            return _m22;
        }

        /// @brief The x translation.
        constexpr double dx() const noexcept {
            return _dx;
        }

        /// @brief The y translation.
        constexpr double dy() const noexcept {
            return _dy;
        }

        /// @brief The identity transform (the could-not-be-simpler static instance of this class).
        static constexpr transform_t identity_transform() noexcept {
            return transform_t{};
        }

        /// @brief A pure translation by (dx, dy).
        /// @param dx The x offset added to every mapped point.
        /// @param dy The y offset added to every mapped point.
        static constexpr transform_t from_translate(double dx, double dy) noexcept {
            return transform_t{ 1.0, 0.0, 0.0, 1.0, dx, dy };
        }

        /// @brief An axis-aligned scale about the origin.
        /// @param sx The x scale factor (1 leaves x unchanged).
        /// @param sy The y scale factor (1 leaves y unchanged).
        /// @note A factor of 0 is a legal (degenerate) transform; this factory does not reject it.
        static constexpr transform_t from_scale(double sx, double sy) noexcept {
            return transform_t{ sx, 0.0, 0.0, sy, 0.0, 0.0 };
        }

        /// @brief Map a point through the transform.
        /// @param p The integer point to map.
        /// @return The mapped point, rounded to the nearest whole pixel; an exact half is rounded
        ///         away from zero (x = 1.5 becomes 2, x = -1.5 becomes -2).
        /// @note The rounding happens at the GUI layer's pixel boundary; sub-pixel geometry
        ///       (point_f_t / rect_f_t) arrives with G1 and will carry its own mapping. Coordinates
        ///       large enough to overflow int after rounding are not guarded against — they are pixel
        ///       positions.
        constexpr point_t map(point_t p) const noexcept {
            return point_t{ round_coordinate(_m11 * p.x() + _m21 * p.y() + _dx),
                            round_coordinate(_m12 * p.x() + _m22 * p.y() + _dy) };
        }

        /// @brief Compose two transforms: the result applies @p other first, then *this.
        /// @param other The transform applied first.
        /// @return The composed transform (the matrix product *this * other).
        constexpr transform_t operator*(transform_t other) const noexcept {
            return transform_t{
                _m11 * other._m11 + _m21 * other._m12,
                _m12 * other._m11 + _m22 * other._m12,
                _m11 * other._m21 + _m21 * other._m22,
                _m12 * other._m21 + _m22 * other._m22,
                _m11 * other._dx + _m21 * other._dy + _dx,
                _m12 * other._dx + _m22 * other._dy + _dy,
            };
        }

        /// @brief Whether the transform is the identity.
        /// @return true when m11() == m22() == 1 and m12() == m21() == dx() == dy() == 0.
        /// @note The comparison is exact. The factories store their arguments unchanged, so this is
        ///       the expected test for them; a computed transform should be compared with a tolerance
        ///       by its user instead.
        constexpr bool is_identity() const noexcept {
            return _m11 == 1.0 && _m12 == 0.0 && _m21 == 0.0 && _m22 == 1.0 && _dx == 0.0 && _dy == 0.0;
        }

        /// @brief Whether all six coefficients are equal.
        friend constexpr bool operator==(transform_t a, transform_t b) noexcept {
            return a._m11 == b._m11 && a._m12 == b._m12 && a._m21 == b._m21 && a._m22 == b._m22
                && a._dx == b._dx && a._dy == b._dy;
        }

    private:
        /// @brief Round to the nearest integer, halves away from zero, without <cmath> (constexpr).
        static constexpr int round_coordinate(double value) noexcept {
            return value >= 0.0 ? static_cast<int>(value + 0.5) : -static_cast<int>(-value + 0.5);
        }
    };
}

#endif
