#ifndef HWSHQTB__METAOBJECT__SIGNAL_SPY_H
#define HWSHQTB__METAOBJECT__SIGNAL_SPY_H

/// @file signal_spy_t.h
/// @brief Records the emissions of one signal (the QSignalSpy equivalent).

#include "any_t.h"
#include "object_t.h"
#include "export.h"

#include <memory>
#include <vector>

namespace hwshqtb::metaobject {
    /// @brief Records every emission of one signal, with its arguments.
    ///
    /// The spy disconnects itself when destroyed.
    ///
    /// @code
    /// signal_spy_t spy(&obj, "valueChanged(double)");
    /// obj.set_value(1.5);
    /// assert(spy.count() == 1);
    /// auto records = spy.take_all();          // records[0][0] holds the double
    /// @endcode
    class HWSHQTB_EXPORT signal_spy_t {
    public:
        /// @brief Start recording.
        /// @param obj The sender.
        /// @param signal_signature The signal's normalised signature.
        /// @note Does nothing when the object is null or the signal does not exist; check is_valid().
        signal_spy_t(object_t* obj, std::string_view signal_signature);

        /// @brief Disconnect the spy.
        ~signal_spy_t();

        /// @brief Copy and move are disabled: the spy owns one connection.
        signal_spy_t(const signal_spy_t&) = delete;
        signal_spy_t& operator=(const signal_spy_t&) = delete;
        signal_spy_t(signal_spy_t&&) = delete;
        signal_spy_t& operator=(signal_spy_t&&) = delete;

        /// @brief Whether the spy is attached to a real signal.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief How many emissions have been recorded.
        /// @note noexcept.
        std::size_t count() const noexcept;

        /// @brief Move out every record and clear the list.
        /// @return One entry per emission; the i-th entry holds that emission's arguments in order.
        std::vector<std::vector<any_t>> take_all();

    private:
        /// @brief The functor connection that feeds the records.
        connection_handle_t _handle;
        /// @brief Shared with the connection's lambda, which appends the records.
        std::shared_ptr<std::vector<std::vector<any_t>>> _records;
    };
}

#endif
