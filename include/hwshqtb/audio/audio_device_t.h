#ifndef HWSHQTB__AUDIO__AUDIO_DEVICE_T_H
#define HWSHQTB__AUDIO__AUDIO_DEVICE_T_H

/// @file audio_device_t.h
/// @brief audio_device_t: one output device as the application sees it (QAudioDevice's subset).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/audio/audio_format_t.h"
#include "hwshqtb/audio/export.h"

namespace hwshqtb::audio {
    /// @brief One audio OUTPUT device.
    ///
    /// @par Value type, cheap to copy
    /// A device is an index plus the description and the preferred layout the backend reported. Holding
    /// a copy is safe even after the device list is refreshed; only opening a stream talks to the
    /// backend again.
    ///
    /// @par Null device
    /// A default-constructed device is NULL (QAudioDevice::isNull()): the machine has no output, so a
    /// caller can hold "no device" as a value instead of a nullptr.
    ///
    /// @par Not annotated with the export marker members
    /// The class is exported for its out-of-line members only; the inline accessors are header-only.
    class HWSHQTB_AUDIO_EXPORT audio_device_t {
    public:
        /// @brief Construct the NULL device.
        audio_device_t() = default;

        /// @brief Construct a device the backend described.
        /// @param index The backend's own device index (used when opening).
        /// @param id The backend's stable identifier, if it has one (diagnostics only).
        /// @param description The human-readable name the backend reported.
        /// @param preferred The layout the backend prefers; may be invalid.
        /// @param is_default Whether this is the system default output.
        audio_device_t(int index, std::string id, std::string description, audio_format_t preferred,
                       bool is_default);

        /// @brief Whether this is the NULL device (no output on this machine).
        /// @note noexcept.
        bool is_null() const noexcept {
            return _null;
        }

        /// @brief Whether this is the system default output.
        /// @note noexcept.
        bool is_default() const noexcept {
            return _is_default;
        }

        /// @brief The backend's own index.
        /// @note noexcept; -1 for the NULL device.
        int index() const noexcept {
            return _index;
        }

        /// @brief The backend's stable identifier, for diagnostics.
        /// @note noexcept.
        const std::string& id() const noexcept {
            return _id;
        }

        /// @brief The human-readable name.
        /// @note noexcept.
        const std::string& description() const noexcept {
            return _description;
        }

        /// @brief The layout the backend prefers; invalid for the NULL device.
        /// @note noexcept.
        const audio_format_t& preferred_format() const noexcept {
            return _preferred;
        }

        /// @brief Whether the device accepts @p format as-is.
        /// @param format The wanted layout.
        /// @note A device accepts any VALID format: the backend resamples or converts what it must, which
        ///       is why nearest_format() usually returns the request unchanged.
        // HWSHQTB_AUDIO_EXPORT (class-level) covers this out-of-line member.
        bool is_format_supported(const audio_format_t& format) const noexcept;

        /// @brief The closest layout to @p format that this device accepts.
        /// @param format The wanted layout.
        /// @return @p format when it is supported, otherwise the device's preferred layout.
        audio_format_t nearest_format(const audio_format_t& format) const;

        /// @brief The lowest channel count the device can be opened with.
        /// @note noexcept; 1 for the NULL device, so callers need no special case.
        int minimum_channel_count() const noexcept {
            return 1;
        }

        /// @brief The highest channel count the device can be opened with.
        /// @note noexcept; 0 for the NULL device.
        int maximum_channel_count() const noexcept {
            return is_null() ? 0 : 8;
        }

    private:
        /// @brief Whether this is the NULL device.
        /// @note A separate flag, not index < 0: -1 is also the SENTINEL for the system default when
        ///       opening, so deriving nullness from the index would report the default device as absent.
        bool _null = true;
        /// @brief The backend's device index; -1 means the system default.
        int _index = -1;
        /// @brief The backend's stable identifier (diagnostics only).
        std::string _id{};
        /// @brief The human-readable name.
        std::string _description{};
        /// @brief The layout the backend prefers.
        audio_format_t _preferred{};
        /// @brief Whether this is the system default output.
        bool _is_default = false;
    };

    /// @brief Every output device the backend reports, the default one first.
    /// @return The devices; EMPTY when the machine has no audio output (a headless runner), which is a
    ///         normal answer and not an error.
    HWSHQTB_AUDIO_EXPORT std::vector<audio_device_t> audio_outputs();

    /// @brief The system default output.
    /// @return The default device, or the NULL device when there is no audio output at all.
    HWSHQTB_AUDIO_EXPORT audio_device_t default_audio_output();
}

#endif
