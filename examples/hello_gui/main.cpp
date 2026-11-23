// H4: hello_gui — the vertical slice demo.
//
// Default mode is a persistent, QWidget-like window: it stays open until the user closes it or
// presses ESC, it repaints whenever the OS asks it to (resize, expose), and a small frame counter
// plus a one-pixel-per-frame marker prove that the event-loop-driven repaint path really runs.
// --duration <ms> closes it after a bounded time (scripted verification), and --screenshot <path>
// renders exactly one deterministic frame (frame 0), writes it as a BMP and exits — no event loop,
// no shown window, so it also works without a display.
//
// Event flow, end to end: SDL events enter through the library's event dispatcher (gui_application_t
// installs SDL's pump; core_application owns the loop); window_t's native event filter sees the
// platform events for its own window id and queues resize/move/expose events while calling
// close_event()/key_press_event() directly. The queued events reach the window's virtual hooks on a
// later loop round; a resize or expose also posts a paint request, and only when the loop dispatches
// it does window_t clear the frame, call paint_event() and present. The demo never polls SDL and
// never runs its own loop.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/window_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_t.h"
#include "hwshqtb/metaobject/object_t.h"

using hwshqtb::gui::color_t;
using hwshqtb::gui::extent_t;
using hwshqtb::gui::gui_application_t;
using hwshqtb::gui::painter_t;
using hwshqtb::gui::paint_event_t;
using hwshqtb::gui::point_t;
using hwshqtb::gui::rect_t;
using hwshqtb::gui::resize_event_t;
using hwshqtb::gui::window_t;
using hwshqtb::metaobject::core_application;
using hwshqtb::metaobject::object_t;
using hwshqtb::metaobject::timer_event_t;

namespace {
    /// @brief The demo window: white background, "hello world", a frame counter and a moving marker.
    ///
    /// paint_event() is the only drawing entry point. It is called with a fresh painter and a frame
    /// that is already cleared to white, so the frame is fully deterministic: frame 0 always looks
    /// the same, which is what the screenshot mode captures.
    class hello_window_t final: public window_t {
    public:
        hello_window_t():
            window_t("hello_gui", extent_t{ 320, 120 }) { }

        /// @brief How many frames paint_event() has drawn (the first one is frame 0).
        int painted_frames() const noexcept {
            return _frame;
        }

    protected:
        void paint_event(paint_event_t* event) override {
            painter_t& painter = event->painter();
            const int width = size().width();
            const int height = size().height();
            painter.set_color(color_t::black_color());
            // The headline sits near the left, with the baseline low enough for the 6-row ascent.
            painter.draw_text(point_t{ 12, 80 }, "hello world");
            // Proof of life: this text changes with every repaint, so a stale single frame would be
            // obvious on screen.
            const std::string counter = "frame " + std::to_string(_frame);
            painter.draw_text(point_t{ 12, 104 }, counter);
            // Proof of motion: a 4x3 marker that advances one pixel per frame and wraps.
            const int marker_x = 12 + (_frame * 7) % (width > 140 ? width - 140 : 80);
            painter.fill_rect(rect_t{ marker_x, height - 8, 4, 3 });
            std::printf("[hello_gui] paint #%d %dx%d\n", _frame, width, height);
            ++_frame;
        }

        void resize_event(resize_event_t* event) override {
            const extent_t new_size = event->size();
            std::printf("[hello_gui] resize_event %dx%d\n", new_size.width(), new_size.height());
        }

    private:
        int _frame = 0;
    };

    /// @brief Drives the demo from timers on the library event loop.
    ///
    /// One timer asks for a repaint ten times a second (so the frame counter visibly advances); an
    /// optional one-shot timer closes the window, which ends the loop (--duration).
    class demo_pump_t final: public object_t {
    public:
        demo_pump_t(window_t& window, int duration_ms):
            _window(&window) {
            _repaint_timer = start_timer(100);
            if (duration_ms > 0) {
                _duration_timer = start_timer(duration_ms, false);
            }
        }

    protected:
        void timer_event(timer_event_t* event) override {
            if (event == nullptr || _window == nullptr) {
                return;
            }
            if (_duration_timer != 0 && event->timer_id() == _duration_timer) {
                std::printf("[hello_gui] duration elapsed, closing\n");
                _window->close();
                return;
            }
            if (event->timer_id() == _repaint_timer) {
                _window->update();
            }
        }

    private:
        window_t* _window = nullptr;
        int _repaint_timer = 0;
        int _duration_timer = 0;
    };

    /// @brief Parsed command line.
    struct options_t {
        std::string screenshot_path;
        int duration_ms = 0;
    };

    void print_usage() {
        std::printf(
            "usage: hello_gui [--duration <ms>] [--screenshot <path>]\n"
            "  (no arguments)       persistent window; close it or press ESC to exit\n"
            "  --duration <ms>      close the window automatically after <ms> milliseconds\n"
            "  --screenshot <path>  render one deterministic frame (frame 0) to a BMP and exit\n");
    }

    /// @brief Parse argv; false means the arguments were rejected (the caller exits with 2).
    bool parse_options(int argc, char** argv, options_t& options) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--screenshot") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "hello_gui: --screenshot needs a path\n");
                    return false;
                }
                options.screenshot_path = argv[++i];
            } else if (argument == "--duration") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "hello_gui: --duration needs a millisecond count\n");
                    return false;
                }
                const std::string value = argv[++i];
                char* end = nullptr;
                const long parsed = std::strtol(value.c_str(), &end, 10);
                if (end == value.c_str() || *end != '\0' || parsed < 0) {
                    std::fprintf(stderr, "hello_gui: invalid --duration value '%s'\n", value.c_str());
                    return false;
                }
                options.duration_ms = static_cast<int>(parsed);
            } else if (argument == "--help" || argument == "-h") {
                print_usage();
                std::exit(0);
            } else {
                std::fprintf(stderr, "hello_gui: unknown argument '%.*s'\n", static_cast<int>(argument.size()),
                             argument.data());
                return false;
            }
        }
        return true;
    }
}

int main(int argc, char** argv) {
    options_t options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }

    try {
        // The application owns the object model and the event loop; the GUI object owns the platform
        // side (video and the native event pump). The platform scope is inner on purpose: every
        // window must be gone before ~gui_application_t stops SDL, and the platform side must be
        // gone before ~core_application.
        core_application application{ argc, argv };
        int exit_code = 0;
        {
            gui_application_t gui;
            const std::string driver{ gui_application_t::video_driver_name() };
            std::printf("[hello_gui] video driver: %s\n", driver.empty() ? "(none)" : driver.c_str());

            hello_window_t window;

            if (!options.screenshot_path.empty()) {
                // Deterministic single frame: save_screenshot() paints frame 0 with a fresh painter,
                // reads it back with SDL_RenderReadPixels and writes the BMP. No loop, no show().
                window.save_screenshot(options.screenshot_path);
                const std::filesystem::path absolute =
                    std::filesystem::absolute(std::filesystem::path{ options.screenshot_path });
                std::printf("[hello_gui] wrote %s (%dx%d, frame 0)\n", absolute.string().c_str(),
                            window.size().width(), window.size().height());
                std::printf("[hello_gui] painted %d frame(s) without an event loop\n", window.painted_frames());
            } else {
                demo_pump_t pump{ window, options.duration_ms };
                window.show();
                exit_code = core_application::exec();
                std::printf("[hello_gui] event loop returned %d; painted %d frames, last size %dx%d\n",
                            exit_code, window.painted_frames(), window.size().width(), window.size().height());
            }
        }
        return exit_code;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "hello_gui: %s\n", error.what());
        return 1;
    }
}
