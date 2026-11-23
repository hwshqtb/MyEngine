// A5: audio_demo — the audio layer's vertical slice, driven by real widgets.
//
// Two INDEPENDENT push-mode output streams on the default device:
//   - "music": loops "The Ice Giants.mp3" forever, refilled a little on every frame (a real streaming loop);
//   - "click": fires sfx_click.wav once per button press, on its own stream, so the effect OVERLAPS the
//     music instead of interrupting it. That overlap is exactly what two outputs on one device buy.
//
// The window is an ordinary widget_window_t with a label and two push buttons: the demo links ui AND
// audio, and neither of those layers knows about the other.
//
// Modes:
//   (default)      interactive, loops until the window is closed
//   --duration <ms>  closes after a bounded time (scripted verification)
//   --self-test    headless: decodes both files, opens the streams, pumps a few frames WITHOUT playing
//                  (a machine with no sound card still passes: a NULL output is a supported state)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "hwshqtb/audio/audio_clip_t.h"
#include "hwshqtb/audio/audio_decoder_t.h"
#include "hwshqtb/audio/audio_device_t.h"
#include "hwshqtb/audio/audio_output_t.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/ui/label_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/widget_window_t.h"

using hwshqtb::audio::audio_clip_t;
using hwshqtb::audio::audio_device_t;
using hwshqtb::audio::audio_output_t;
using hwshqtb::audio::audio_state_enum;
using hwshqtb::audio::default_audio_output;
using hwshqtb::audio::load_audio;
using hwshqtb::metaobject::core_application;
using hwshqtb::ui::label_t;
using hwshqtb::ui::push_button_t;
using hwshqtb::ui::vbox_layout_t;
using hwshqtb::ui::widget_t;
using hwshqtb::ui::widget_window_t;

namespace {
    /// @brief Where a sound file lives, relative to wherever the executable was started from.
    /// @param name The bare file name ("The Ice Giants.mp3").
    /// @return The first candidate that exists, or an empty string.
    /// @note The sounds are copied next to the executable by CMake, so "beside the exe" is tried first;
    ///       the source-tree spellings keep a run from the repository root working too.
    std::string find_sound(std::string_view name) {
        for (const char* const prefix : { "", "examples/audio_demo/", "../examples/audio_demo/",
                                          "../../examples/audio_demo/", "../../../examples/audio_demo/",
                                          "../../../../examples/audio_demo/" }) {
            const std::string candidate = std::string(prefix) + std::string(name);
            if (std::FILE* const probe = std::fopen(candidate.c_str(), "rb")) {
                std::fclose(probe);
                return candidate;
            }
        }
        return {};
    }

    /// @brief The demo's root: a caption, the two buttons, and a line of state.
    class audio_root_t final: public widget_t {
    public:
        explicit audio_root_t(widget_t* parent = nullptr):
            widget_t(parent) {
            auto* const layout = new vbox_layout_t;
            layout->set_margins(hwshqtb::gui::margins_t{ 16, 16, 16, 16 });
            layout->set_spacing(12);
            set_layout(layout);

            _status = new label_t{ this };
            _status->set_text("music: (loading)");
            _music_button = new push_button_t{ this };
            _music_button->set_text("Play / Pause music");
            _click_button = new push_button_t{ this };
            _click_button->set_text("Play click (overlaps the music)");
            _click_label = new label_t{ this };
            _click_label->set_text("clicks played: 0");
        }

        /// @brief The status label (updated by the demo's loop).
        label_t* status() const noexcept {
            return _status;
        }

        /// @brief The music toggle.
        push_button_t* music_button() const noexcept {
            return _music_button;
        }

        /// @brief The click trigger.
        push_button_t* click_button() const noexcept {
            return _click_button;
        }

        /// @brief The click counter label.
        label_t* click_label() const noexcept {
            return _click_label;
        }

    private:
        label_t* _status = nullptr;
        push_button_t* _music_button = nullptr;
        push_button_t* _click_button = nullptr;
        label_t* _click_label = nullptr;
    };

    /// @brief The demo's audio side: two streams and the loop that keeps the music fed.
    class audio_driver_t {
    public:
        /// @brief Decode both files and open both outputs.
        /// @return true when BOTH streams opened; false means no sound card (a supported state).
        bool open() {
            try {
                _music = load_audio(find_sound("The Ice Giants.mp3"));
                _click = load_audio(find_sound("sfx_click.wav"));
            } catch (const std::exception& error) {
                _note = error.what();
                return false;
            }
            const audio_device_t device = default_audio_output();
            if (device.is_null()) {
                _note = std::string("no audio output device: ") + SDL_GetError();
                return false;
            }
            _music_output = std::make_unique<audio_output_t>(device, _music.format());
            _click_output = std::make_unique<audio_output_t>(device, _click.format());
            if (_music_output->is_null() || _click_output->is_null()) {
                _note = std::string("no audio output device: ") + SDL_GetError();
                return false;
            }
            _music_output->start();
            _click_output->start();
            return true;
        }

        /// @brief Push more of the music, looping at the end. Call once per frame.
        void pump() {
            if (_music_output == nullptr) {
                return;
            }
            for (int guard = 0; guard < 8; ++guard) {
                if (_music_output->write_some(_music, _offset) > 0) {
                    if (_offset < static_cast<std::int64_t>(_music.samples().size())) {
                        continue; // more room, more data
                    }
                }
                if (_offset >= static_cast<std::int64_t>(_music.samples().size())) {
                    _offset = 0; // loop: rewind and keep feeding
                    ++_loops;
                    continue;
                }
                break; // the buffer is full: wait for the next frame
            }
        }

        /// @brief Fire the click sound on its own stream (overlapping the music).
        /// @return true when the click was queued.
        bool fire_click() {
            if (_click_output == nullptr) {
                return false;
            }
            // A one-shot: restart the stream so a second press does not wait for the first to drain.
            _click_output->stop();
            _click_output->start();
            std::int64_t offset = 0;
            (void)_click_output->write_some(_click, offset);
            ++_clicks;
            return true;
        }

        /// @brief Toggle the music between playing and paused.
        /// @return The new state.
        audio_state_enum toggle() {
            if (_music_output == nullptr) {
                return audio_state_enum::STOPPED;
            }
            if (_paused) {
                _music_output->resume();
                _paused = false;
            } else {
                _music_output->suspend();
                _paused = true;
            }
            return _music_output->state();
        }

        /// @brief Whether an output stream is open.
        bool ready() const noexcept {
            return _music_output != nullptr;
        }

        /// @brief How many times the music looped.
        std::int64_t loops() const noexcept {
            return _loops;
        }

        /// @brief How many clicks were fired.
        std::int64_t clicks() const noexcept {
            return _clicks;
        }

        /// @brief The music clip (for the status line).
        const audio_clip_t& music() const noexcept {
            return _music;
        }

        /// @brief Why open() failed, when it did.
        const std::string& note() const noexcept {
            return _note;
        }

    private:
        /// @brief The decoded music, pushed repeatedly.
        audio_clip_t _music{};
        /// @brief The decoded click, fired once per press.
        audio_clip_t _click{};
        /// @brief The music stream.
        std::unique_ptr<audio_output_t> _music_output;
        /// @brief The click stream, separate so the two MIX.
        std::unique_ptr<audio_output_t> _click_output;
        /// @brief How far into the music the loop has pushed.
        std::int64_t _offset = 0;
        /// @brief How many times the music rewound.
        std::int64_t _loops = 0;
        /// @brief How many clicks were fired.
        std::int64_t _clicks = 0;
        /// @brief Whether the music is suspended.
        bool _paused = false;
        /// @brief The failure reason, when open() returned false.
        std::string _note;
    };

    /// @brief The window: a widget host plus the per-frame audio pump.
    class audio_window_t final: public widget_window_t {
    public:
        /// @brief Construct the demo window.
        audio_window_t():
            widget_window_t("audio_demo", hwshqtb::gui::extent_t{ 460, 260 }) { }

        /// @brief Adopt @p root as the tree and remember it for the status helpers.
        void adopt(audio_root_t* root) {
            _root = root;
            set_root_widget(root); // the window takes ownership
        }

        /// @brief The demo's root widget.
        audio_root_t* audio_root() const noexcept {
            return _root;
        }

        /// @brief The audio side.
        audio_driver_t& audio() noexcept {
            return _audio;
        }

        /// @brief Called by the demo's loop once per frame: keep the music fed.
        void pump_audio() {
            _audio.pump();
        }

    private:
        /// @brief The demo's root widget (owned by the window through set_root_widget()).
        audio_root_t* _root = nullptr;
        /// @brief The two output streams.
        audio_driver_t _audio;
    };

    /// @brief Print the state line: it is the demo's proof that playback is really running.
    void report(audio_window_t& window) {
        audio_driver_t& audio = window.audio();
        if (!audio.ready()) {
            window.audio_root()->status()->set_text("audio: " + audio.note() +
                                                    " (the UI still runs)");
            return;
        }
        const audio_clip_t& music = audio.music();
        const std::string state = "music: " + std::to_string(music.format().sample_rate()) + " Hz, "
            + std::to_string(music.format().channel_count()) + " ch, "
            + std::to_string(music.duration_us() / 1'000'000) + " s, loops="
            + std::to_string(audio.loops());
        window.audio_root()->status()->set_text(state);
        window.audio_root()->click_label()->set_text("clicks played: " + std::to_string(audio.clicks()));
    }

    /// @brief The frame pacer: a timer on the library event loop that refills the music stream.
    ///
    /// A push-mode stream needs somebody to push, and the event loop is where that belongs (Qt's
    /// QAudioSink wants the same kind of periodic refill). 40 ms is one frame of headroom for the
    /// default one-second buffer.
    class audio_pacer_t final: public hwshqtb::metaobject::object_t {
    public:
        /// @brief Start pacing.
        /// @param window The demo window whose audio to pump and report.
        explicit audio_pacer_t(audio_window_t& window):
            _window(window) {
            _timer = start_timer(40);
        // NO quit timer here: a 0 ms timer fires at once, and its id would then match the quit branch,
        // closing the window immediately (which is what this placeholder used to do).
        // arm_quit() creates it only when a bounded run was asked for.
        }

        /// @brief Close the demo after @p milliseconds (0 = never).
        void arm_quit(std::int64_t milliseconds) {
            if (milliseconds > 0) {
                kill_timer(_quit_timer);
                _quit_delay = static_cast<int>(milliseconds);
                _quit_timer = start_timer(_quit_delay, false);
            }
        }

    protected:
        void timer_event(hwshqtb::metaobject::timer_event_t* event) override {
            if (event == nullptr) {
                return;
            }
            if (event->timer_id() == _timer) {
                _window.pump_audio();
                report(_window);
            } else if (event->timer_id() == _quit_timer) {
                hwshqtb::metaobject::core_application::quit();
            }
        }

    private:
        /// @brief The window whose audio is pumped.
        audio_window_t& _window;
        /// @brief The 40 ms pump timer.
        int _timer = 0;
        /// @brief The one-shot quit timer.
        int _quit_timer = 0;
        /// @brief The quit delay in milliseconds (0 = never).
        int _quit_delay = 0;
    };

    /// @brief The headless check: decode both files, open the streams, pump without playing.
    /// @return 0 on success.
    int self_test() {
        bool ok = true;
        audio_driver_t driver;
        const bool ready = driver.open();
        std::printf("[audio_demo] outputs open: %s%s%s\n", ready ? "yes" : "no",
                    ready ? "" : " (", ready ? "" : driver.note().c_str());
        if (!ready) {
            std::printf("%s\n", ")");
        }
        if (driver.music().is_empty()) {
            std::printf("[audio_demo] FAIL: music did not decode\n");
            ok = false;
        } else {
            std::printf("[audio_demo] music: %d Hz %d ch %.2f s\n",
                        driver.music().format().sample_rate(),
                        driver.music().format().channel_count(),
                        static_cast<double>(driver.music().duration_us()) / 1'000'000.0);
        }
        if (driver.ready()) {
            for (int frame = 0; frame < 5; ++frame) {
                driver.pump();
            }
            std::printf("[audio_demo] pumped 5 frames, loops=%lld\n",
                        static_cast<long long>(driver.loops()));
        }
        std::printf("[audio_demo] self-test: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
}

int main(int argc, char* argv[]) {
    std::int64_t duration_ms = 0;
    bool self = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--self-test") == 0) {
            self = true;
        } else if (std::strcmp(argv[index], "--duration") == 0 && index + 1 < argc) {
            duration_ms = std::atoll(argv[++index]);
        }
    }
    if (self) {
        return self_test();
    }

    core_application application{ argc, argv };
    hwshqtb::gui::gui_application_t gui;
    audio_window_t window;
    auto* const root = new audio_root_t;
    window.adopt(root);
    // The window's own audio side needs the same root, so wire it once: the widget is created above and
    // the window only stores a pointer for the reporting helpers.
    {
        audio_driver_t& audio = window.audio();
        const std::string music_path = find_sound("The Ice Giants.mp3");
        const std::string click_path = find_sound("sfx_click.wav");
        std::printf("[audio_demo] music file: %s\n", music_path.empty() ? "(missing)" : music_path.c_str());
        std::printf("[audio_demo] click file: %s\n", click_path.empty() ? "(missing)" : click_path.c_str());
        (void)audio.open();
        report(window);
    }

    // Wire the two buttons to the two streams. The connection lives HERE, not in the widgets: a control
    // knows nothing about audio, and the demo is the only place that holds both (the same separation the
    // layers keep).
    root->music_button()->connect("clicked_changed(bool) const", [&window](bool clicked) {
        // clicked is a PER-FRAME result: the click frame raises clicked_changed(true) and the next driven
        // frame raises (false). Only the true edge is a click.
        if (!clicked) {
            return;
        }
        const audio_state_enum state = window.audio().toggle();
        window.audio_root()->status()->set_text(state == audio_state_enum::SUSPENDED
                                                    ? "music: paused"
                                                    : "music: playing");
    });
    root->click_button()->connect("clicked_changed(bool) const", [&window](bool clicked) {
        if (!clicked) {
            return;
        }
        (void)window.audio().fire_click();
        window.audio_root()->click_label()->set_text("clicks played: "
                                                     + std::to_string(window.audio().clicks()));
    });
    window.show();

    // One timer drives both the audio pump and the status line: the event loop is the natural place to
    // refill a push-mode stream (QAudioSink wants the same kind of periodic refill).
    audio_pacer_t pacer{ window };
    pacer.arm_quit(duration_ms);
    const int code = core_application::exec();
    return code;
}
