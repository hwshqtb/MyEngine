// W1: widget_t / widget_window_t and the painter prerequisites they rest on.
//
// The acceptance criteria as executable checks:
//  1. geometry: three nested widgets, map_to_global() is the accumulated offset, round-trip exact;
//  2. hit testing: the later overlapping child wins, a point inside the parent but outside every
//     child yields nullptr (the parent), outside yields nullptr, hidden/disabled children are skipped;
//  3. visibility: hide() skips hit and render, is_visible_in_window() follows the ancestor chain;
//  4. offscreen pixels: three nested coloured panels asserted region by region, a child drawn outside
//     its parent is clipped, a hidden child is not drawn;
//  5. non-widget children: a plain object_t child is neither rendered nor hit, and the child list of
//     the object tree stays intact;
//  6. interact() is host-neutral: a test widget receives the exact interaction_t it was given (the
//     host computes the local mouse position; interact() never translates);
//  7. update()/set_geometry() reach the repaint sink, including from a nested widget;
//  8. the W1 properties (visible / enabled) are real reflected properties; geometry is not (yet);
//  9. the painter prerequisites: the translate feature bit (SDL does not declare it, so the CPU path
//     must run), CPU translation of glyph runs and rectangles, the clip stack, save()/restore().
//
// No window and no video subsystem are created here: everything is offscreen through image_t, plus a
// recording paint engine for the parts that are invisible in pixels (glyph positions, the engine
// state). The window host (widget_window_t) is exercised by examples/ui_demo.
//
// Note on CHECK: variadic, because several expressions build braced value types inline
// (rect_t{ 5, 4, 6, 5 }); the preprocessor separates on those commas, __VA_ARGS__ joins them back.

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using hwshqtb::ui::widget_t;

namespace ui_test {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

/// Compile-time proof of the W1 core contract: object_t gained one virtual (is_widget) and did NOT
/// grow (stage S kept it at 32 bytes), and widget_t is one.
static_assert(sizeof(object_t) == 32);
static_assert(sizeof(::hwshqtb::ui::widget_t) > sizeof(object_t));
static_assert(std::is_base_of_v<object_t, ::hwshqtb::ui::widget_t>);
static_assert(std::is_polymorphic_v<object_t>);

/// Counts the pixels of @p image that have exactly @p color.
int count_color(const image_t& image, color_t color) {
    const extent_t size = image.size();
    int count = 0;
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            if (image.pixel(x, y) == color) {
                ++count;
            }
        }
    }
    return count;
}

/// Whether every pixel of @p rect has exactly @p expected colour (the caller keeps @p rect inside the
/// image; an outside coordinate would read as transparent and fail the check).
bool region_is(const image_t& image, rect_t rect, color_t expected) {
    for (int y = rect.top(); y < rect.bottom(); ++y) {
        for (int x = rect.left(); x < rect.right(); ++x) {
            if (!(image.pixel(x, y) == expected)) {
                return false;
            }
        }
    }
    return true;
}

/// A widget whose paint_event() fills its whole rectangle with one colour. It derives from widget_t
/// without an OBJECT table (a test fixture, not a reflected class), which is the documented way for a
/// concrete control to specialise only the painting hook.
class solid_widget_t final: public widget_t {
public:
    explicit solid_widget_t(color_t color, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) { }

protected:
    void paint_event(::hwshqtb::ui::paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(_color);
        painter.fill_rect(rect());
    }

private:
    color_t _color;
};

/// A widget that records the interaction_t it receives; used to prove interact() is host-neutral.
class recording_widget_t final: public widget_t {
public:
    using widget_t::widget_t;

    bool interact(const interaction_t& in) override {
        last = in;
        ++calls;
        return consumed;
    }

    interaction_t last{};
    bool consumed = false;
    int calls = 0;
};

/// A recording paint engine: rasterises nothing, records what the painter asked for. Its features are
/// set by the test, which is how the engine-translate path and the CPU-translate fallback are told
/// apart without a second backend.
class recording_paint_engine_t final: public paint_engine_t {
public:
    engine_feature_t features() const noexcept override {
        return supports_translate ? engine_feature_t{ engine_feature_flag::TRANSLATE } : engine_feature_t{};
    }

    bool begin(paint_device_t&) override {
        active = true;
        ++begins;
        return true;
    }

    bool end() override {
        active = false;
        ++ends;
        return true;
    }

    void update_state(const engine_state_t& state) override {
        last_state = state;
        ++state_updates;
    }

    void draw_rects(const rect_t* rects, int count, bool fill) override {
        for (int i = 0; i < count; ++i) {
            this->rects.push_back(rects[i]);
        }
        last_fill = fill;
    }

    void draw_glyphs(const glyph_run_t& run) override {
        // A run no longer carries a separate origin: the first glyph's baseline IS where it starts.
        origin = run.count > 0 ? run.positions[0] : point_t{};
        positions.assign(run.positions, run.positions + run.count);
        ++glyph_runs;
    }
    void draw_image(const rect_t&, const image_view_t&) override { }

    bool supports_translate = false;
    bool active = false;
    int begins = 0;
    int ends = 0;
    int state_updates = 0;
    int glyph_runs = 0;
    bool last_fill = false;
    engine_state_t last_state;
    std::vector<rect_t> rects;
    point_t origin;
    std::vector<point_t> positions;
};

/// A paint device that hands out the recording engine (never rasterises, never touches SDL).
class recording_device_t final: public paint_device_t {
public:
    explicit recording_device_t(extent_t size):
        _size(size) { }

    extent_t size() const noexcept override {
        return _size;
    }

    paint_engine_t* paint_engine() noexcept override {
        return &_engine;
    }

    recording_paint_engine_t& engine() noexcept {
        return _engine;
    }

private:
    extent_t _size;
    recording_paint_engine_t _engine;
};

}  // namespace ui_test

using namespace ui_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    std::printf("== W1: widget_t, widget_window_t and the painter prerequisites ==\n");

    // No application object here: register the builtin names the reflected widget properties are
    // resolved through (visible / enabled are bool).
    (void)meta_type_t::register_type<bool>();

    // ---- (0) the core marker: object_t is not a widget, widget_t is ----
    {
        object_t plain_object;
        CHECK(!plain_object.is_widget());
        widget_t plain_widget;
        CHECK(plain_widget.is_widget());
        // Through the base pointer, which is how the widget layer sees its children.
        object_t* const base = &plain_widget;
        CHECK(base->is_widget());
    }

    // ---- (1) geometry: nesting accumulates, mapping round-trips ----
    {
        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 150 });
        widget_t middle{ &root };
        middle.set_geometry(rect_t{ 10, 20, 120, 100 });
        widget_t leaf{ &middle };
        leaf.set_geometry(rect_t{ 5, 7, 40, 30 });

        CHECK(root.geometry() == rect_t{ 0, 0, 200, 150 });
        CHECK(middle.pos() == point_t{ 10, 20 });
        CHECK(middle.size() == extent_t{ 120, 100 });
        CHECK(leaf.rect() == rect_t{ 0, 0, 40, 30 });
        CHECK(leaf.pos() == point_t{ 5, 7 });

        CHECK(root.map_to_global(point_t{ 0, 0 }) == point_t{ 0, 0 });
        CHECK(middle.map_to_global(point_t{ 0, 0 }) == point_t{ 10, 20 });
        CHECK(leaf.map_to_global(point_t{ 0, 0 }) == point_t{ 15, 27 });
        CHECK(leaf.map_to_global(point_t{ 3, 4 }) == point_t{ 18, 31 });
        CHECK(leaf.map_to_global(point_t{ -2, 9 }) == point_t{ 13, 36 });

        // Round trip, in both directions, at several levels.
        CHECK(leaf.map_from_global(point_t{ 18, 31 }) == point_t{ 3, 4 });
        CHECK(leaf.map_from_global(leaf.map_to_global(point_t{ 4, 9 })) == point_t{ 4, 9 });
        CHECK(middle.map_from_global(middle.map_to_global(point_t{ 1, 2 })) == point_t{ 1, 2 });
        CHECK(root.map_from_global(point_t{ 15, 27 }) == point_t{ 15, 27 });

        // The tree view is filtered (nothing but widgets here) and parented as constructed.
        CHECK(leaf.parent_widget() == &middle);
        CHECK(middle.parent_widget() == &root);
        CHECK(root.parent_widget() == nullptr);
        CHECK(root.item_count() == 1);
        CHECK(middle.item_count() == 1);
        CHECK(leaf.item_count() == 0);
        CHECK(root.item_at_index(0) == &middle);
        CHECK(root.item_at_index(1) == nullptr);
        CHECK(middle.item_at_index(0) == &leaf);
        CHECK(middle.item_at_index(1) == nullptr);
        CHECK(leaf.item_at_index(0) == nullptr);
    }

    // ---- (2) hit testing: reverse order, clipping to the parent, hidden/disabled skipped ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 80 });
        widget_t first{ &parent };
        first.set_geometry(rect_t{ 10, 10, 40, 30 });
        widget_t second{ &parent };
        second.set_geometry(rect_t{ 20, 20, 40, 30 });

        CHECK(parent.item_count() == 2);
        // Overlap: the later child (painted last, i.e. on top) wins.
        CHECK(parent.hit(point_t{ 30, 30 }) == &second);
        CHECK(parent.hit(point_t{ 12, 12 }) == &first);
        // Half-open edges: a point on a right/bottom edge is outside that child.
        CHECK(parent.hit(point_t{ 10, 10 }) == &first);
        CHECK(parent.hit(point_t{ 50, 10 }) == nullptr);
        CHECK(parent.hit(point_t{ 20, 50 }) == nullptr);
        // Inside the parent, outside every child: no child, so the parent itself is the hit.
        CHECK(parent.hit(point_t{ 90, 70 }) == nullptr);
        CHECK(parent.rect().contains(point_t{ 90, 70 }));
        // Outside the parent: nullptr and not the parent either.
        CHECK(parent.hit(point_t{ 150, 150 }) == nullptr);
        CHECK(!parent.rect().contains(point_t{ 150, 150 }));

        // Hidden children are not hit (and neither is their subtree).
        first.set_visible(false);
        CHECK(parent.hit(point_t{ 12, 12 }) == nullptr);
        CHECK(parent.hit(point_t{ 30, 30 }) == &second);
        first.set_visible(true);
        CHECK(parent.hit(point_t{ 12, 12 }) == &first);

        // Disabled children are not hit either, but stay visible (they are still rendered).
        first.set_enabled(false);
        CHECK(parent.hit(point_t{ 12, 12 }) == nullptr);
        first.set_enabled(true);
        CHECK(parent.hit(point_t{ 12, 12 }) == &first);

        // The deepest widget wins: a grandchild inside `second`.
        widget_t inner{ &second };
        inner.set_geometry(rect_t{ 5, 5, 10, 10 });
        CHECK(parent.hit(point_t{ 28, 28 }) == &inner);
        CHECK(parent.hit(point_t{ 45, 45 }) == &second);

        // A child that sticks out of its parent is clipped when it is painted, so it is not a hit
        // outside the parent either.
        widget_t overhanging{ &parent };
        overhanging.set_geometry(rect_t{ 90, 70, 50, 50 });
        CHECK(parent.hit(point_t{ 95, 75 }) == &overhanging);
        CHECK(parent.hit(point_t{ 105, 75 }) == nullptr);
        CHECK(!parent.rect().contains(point_t{ 105, 75 }));
    }

    // ---- (3) visibility: hide() hides the subtree, is_visible_in_window() follows the chain ----
    {
        widget_t root;
        widget_t child{ &root };
        widget_t grandchild{ &child };
        CHECK(root.is_visible());
        CHECK(child.is_visible());
        CHECK(child.is_visible_in_window());
        CHECK(grandchild.is_visible_in_window());

        child.set_visible(false);
        CHECK(!child.is_visible());
        CHECK(!child.is_visible_in_window());
        CHECK(!grandchild.is_visible_in_window());  // an ancestor is hidden
        CHECK(root.is_visible_in_window());          // the root itself is fine

        child.set_visible(true);
        CHECK(grandchild.is_visible_in_window());
        grandchild.set_visible(false);
        CHECK(!grandchild.is_visible_in_window());
        CHECK(child.is_visible_in_window());
    }

    // ---- (4) rendering into an image_t: nested colours, clipping, hidden subtrees ----
    {
        // root 60x50 black; panel at (10,10) 30x20 green; `over` at (5,5) 40x30 red inside panel, so it
        // sticks out on the right and bottom and must be clipped to the panel; `hidden` is blue, at the
        // bottom, and hidden.
        solid_widget_t root{ color_t::black_color() };
        root.set_geometry(rect_t{ 0, 0, 60, 50 });
        solid_widget_t panel{ color_t::green_color(), &root };
        panel.set_geometry(rect_t{ 10, 10, 30, 20 });
        solid_widget_t over{ color_t::red_color(), &panel };
        over.set_geometry(rect_t{ 5, 5, 40, 30 });
        solid_widget_t hidden{ color_t::blue_color(), &root };
        hidden.set_geometry(rect_t{ 10, 40, 10, 10 });
        hidden.set_visible(false);

        image_t image{ 60, 50 };
        {
            painter_t painter{ image };
            CHECK(painter.begin());
            painter.clear(color_t::white_color());
            root.render(painter);
            // The paint() stack must be balanced: this fill lands at the device origin with the
            // colour set here, not with the deep translate/clip the tree left behind.
            painter.set_color(color_t::cyan_color());
            painter.fill_rect(rect_t{ 0, 0, 2, 2 });
            painter.end();
        }

        const color_t black = color_t::black_color();
        const color_t green = color_t::green_color();
        const color_t red = color_t::red_color();
        const color_t cyan = color_t::cyan_color();

        // Regions: red is over ∩ panel = (15,15)-(40,30); green is panel minus red; everything else
        // outside the panel stays root-black.
        CHECK(region_is(image, rect_t{ 15, 15, 25, 15 }, red));
        CHECK(region_is(image, rect_t{ 10, 10, 5, 5 }, green));
        CHECK(region_is(image, rect_t{ 10, 15, 5, 15 }, green));
        CHECK(region_is(image, rect_t{ 15, 10, 25, 5 }, green));
        CHECK(region_is(image, rect_t{ 0, 0, 2, 2 }, cyan));
        CHECK(region_is(image, rect_t{ 2, 0, 58, 10 }, black));
        // `over` sticks out of the panel: the parts outside are clipped away.
        CHECK(region_is(image, rect_t{ 40, 15, 10, 15 }, black));
        CHECK(region_is(image, rect_t{ 15, 30, 25, 10 }, black));
        // The hidden child left no blue anywhere.
        CHECK(count_color(image, color_t::blue_color()) == 0);
        // Exact coverage: root 60x50 = 3000 = black + green (600-375) + red (375).
        CHECK(count_color(image, green) == 30 * 20 - 25 * 15);
        CHECK(count_color(image, red) == 25 * 15);
        CHECK(count_color(image, black) == 60 * 50 - 30 * 20 - 2 * 2);
        CHECK(count_color(image, cyan) == 2 * 2);
        CHECK(count_color(image, color_t::white_color()) == 0);
        // Edges of the panel clip, half-open.
        CHECK(image.pixel(14, 14) == green);
        CHECK(image.pixel(15, 15) == red);
        CHECK(image.pixel(39, 29) == red);
        CHECK(image.pixel(40, 29) == black);
        CHECK(image.pixel(39, 30) == black);
        CHECK(image.pixel(1, 1) == cyan);   // the 2x2 probe at the device origin, half-open

        // Hiding a whole subtree skips it entirely: hide the panel, repaint, and only the root remains.
        image_t hidden_panel{ 60, 50 };
        panel.set_visible(false);
        {
            painter_t painter{ hidden_panel };
            painter.begin();
            painter.clear(color_t::white_color());
            root.render(painter);
            painter.end();
        }
        CHECK(count_color(hidden_panel, green) == 0);
        CHECK(count_color(hidden_panel, red) == 0);
        CHECK(count_color(hidden_panel, black) == 60 * 50);
        panel.set_visible(true);
    }

    // ---- (5) non-widget children: not rendered, not hit, child list intact ----
    {
        solid_widget_t host{ color_t::black_color() };
        host.set_geometry(rect_t{ 0, 0, 50, 50 });
        solid_widget_t visual{ color_t::green_color(), &host };
        visual.set_geometry(rect_t{ 5, 5, 20, 20 });

        object_t* const plain = new object_t;
        plain->set_parent(&host);
        CHECK(!plain->is_widget());
        // The object tree owns both; the widget view owns one.
        CHECK(host.children().size() == 2);
        CHECK(host.item_count() == 1);
        CHECK(host.item_at_index(0) == &visual);
        CHECK(host.item_at_index(1) == nullptr);
        CHECK(host.hit(point_t{ 10, 10 }) == &visual);
        CHECK(host.hit(point_t{ 45, 45 }) == nullptr);  // the plain object is never a hit
        CHECK(host.children()[1] == plain);                  // ... but it is still in the object tree

        image_t image{ 50, 50 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(color_t::white_color());
            host.render(painter);  // must skip `plain` and not break the child walk
            painter.end();
        }
        CHECK(region_is(image, rect_t{ 5, 5, 20, 20 }, color_t::green_color()));
        CHECK(count_color(image, color_t::black_color()) == 50 * 50 - 20 * 20);

        // Destroying the non-widget child detaches it cleanly and leaves the widget view unchanged.
        delete plain;
        CHECK(host.children().size() == 1);
        CHECK(host.item_count() == 1);
        CHECK(host.hit(point_t{ 10, 10 }) == &visual);
        CHECK(host.is_widget());
    }

    // ---- (6) interact() is host-neutral: the widget sees exactly what the host computed ----
    {
        recording_widget_t widget;
        widget.set_geometry(rect_t{ 100, 50, 80, 40 });  // a non-zero origin must not leak in

        interaction_t in;
        in.mouse = point_t{ 7, 9 };                       // already local, as the host computed it
        in.mouse_down = true;
        in.mouse_pressed = true;
        in.mouse_released = false;
        in.hovered = true;
        in.modifiers = modifier_bits(modifier_t{ modifier_flag::SHIFT } | modifier_flag::CONTROL);

        CHECK(!widget.interact(in));                      // default-constructed `consumed` is false
        CHECK(widget.calls == 1);
        CHECK(widget.last.mouse == point_t{ 7, 9 });      // no translation happened
        CHECK(widget.last.mouse_down);
        CHECK(widget.last.mouse_pressed);
        CHECK(!widget.last.mouse_released);
        CHECK(widget.last.hovered);
        CHECK(widget.last.modifiers == modifier_bits(modifier_t{ modifier_flag::SHIFT } | modifier_flag::CONTROL));
        CHECK(has_modifier(widget.last.modifiers, modifier_flag::SHIFT));
        CHECK(has_modifier(widget.last.modifiers, modifier_flag::CONTROL));
        CHECK(!has_modifier(widget.last.modifiers, modifier_flag::ALT));
        CHECK(!has_modifier(widget.last.modifiers, modifier_flag::GUI));

        widget.consumed = true;
        CHECK(widget.interact(in));                       // the return value is the host contract
        CHECK(widget.calls == 2);

        // The base implementation consumes nothing (a host can rely on that).
        widget_t passive;
        CHECK(!passive.interact(in));
    }

    // ---- (7) update(): requests go to the tree's request receiver, including from nested widgets ----
    {
        // The tree keeps ONE receiver (an object_t), set on its root: the sink callables this replaced
        // were a third mechanism. A local recorder stands in for the host.
        struct recorder_t final: ::hwshqtb::metaobject::object_t {
            int repaints = 0;
            bool event(::hwshqtb::metaobject::event_t* e) override {
                if (e != nullptr && e->type() == ::hwshqtb::metaobject::event_type_enum::PAINT) {
                    ++repaints;
                    return true;
                }
                return object_t::event(e);
            }
        };
        recorder_t recorder;
        widget_t root;
        root.set_request_receiver(&recorder);
        widget_t child{ &root };
        widget_t grandchild{ &child };

        CHECK(recorder.repaints == 0);
        child.set_geometry(rect_t{ 1, 2, 3, 4 });     // set_geometry must request a repaint
        CHECK(recorder.repaints == 1);
        grandchild.set_geometry(rect_t{ 0, 0, 1, 1 }); // walks up two levels to the tree's receiver
        CHECK(recorder.repaints == 2);
        grandchild.update();
        CHECK(recorder.repaints == 3);
        grandchild.set_visible(false);
        CHECK(recorder.repaints == 4);
        grandchild.set_visible(false);                 // unchanged: no request, no signal
        CHECK(recorder.repaints == 4);
        grandchild.set_enabled(false);
        CHECK(recorder.repaints == 5);
        grandchild.set_enabled(true);
        CHECK(recorder.repaints == 6);

        // A tree without a receiver ignores update() (the host may simply not need repaints).
        widget_t orphan;
        orphan.set_geometry(rect_t{ 0, 0, 5, 5 });
        orphan.update();
        CHECK(recorder.repaints == 6);

        // Removing the receiver stops the requests.
        root.set_request_receiver(nullptr);
        child.set_geometry(rect_t{ 5, 5, 5, 5 });
        CHECK(recorder.repaints == 6);

        // The receiver belongs to the TREE, not to a subtree: configuring it from a nested widget
        // configures the same single receiver, so a request from the grandchild now reaches it and the
        // previous recorder hears nothing (the per-subtree sinks this replaced are gone on purpose).
        recorder_t nested_recorder;
        child.set_request_receiver(&nested_recorder);
        grandchild.update();
        CHECK(nested_recorder.repaints == 1);
        CHECK(recorder.repaints == 6);
        child.update();
        CHECK(nested_recorder.repaints == 2);
    }

    // ---- (8) the W1 reflected properties: visible/enabled yes, geometry deferred ----
    {
        widget_t widget;
        const meta_object_t* const meta = widget.meta_object();
        CHECK(meta != nullptr);
        CHECK(meta->class_name() == "widget_t");

        const meta_property_t visible_property = meta->property("visible");
        const meta_property_t enabled_property = meta->property("enabled");
        CHECK(visible_property.is_valid());
        CHECK(visible_property.is_valid());         // readable: a valid row always has a reader
        CHECK(visible_property.is_writable());
        CHECK(visible_property.is_notifiable());
        CHECK(enabled_property.is_valid());
        CHECK(enabled_property.is_valid());         // readable: a valid row always has a reader
        CHECK(enabled_property.is_writable());
        CHECK(enabled_property.is_notifiable());
        // geometry is NOT a property in W1 (rect_t has no metatype registration yet).
        CHECK(!meta->property("geometry").is_valid());

        CHECK(visible_property.read(widget).get<bool>());
        CHECK(widget.set_property("visible", any_t(false)));
        CHECK(!widget.is_visible());
        CHECK(!visible_property.read(widget).get<bool>());
        CHECK(widget.set_property("visible", any_t(true)));
        CHECK(widget.is_visible());
        CHECK(widget.set_property("enabled", any_t(false)));
        CHECK(!widget.is_enabled());
        CHECK(!enabled_property.read(widget).get<bool>());
        CHECK(widget.set_property("enabled", any_t(true)));
        CHECK(widget.is_enabled());

        // The reader/writer are resolved by reflection against the real declarations, so the property
        // values are the widget's own state (that is what the annotations above guarantee).
        CHECK(widget.property("visible").get<bool>());
        CHECK(widget.property("enabled").get<bool>());

        // The signal the property names is a real signal of the table.
        CHECK(meta->signal("visible_changed(bool) const").is_valid());
        CHECK(meta->signal("enabled_changed(bool) const").is_valid());
        CHECK(meta->method("set_visible(bool)").is_valid());
        CHECK(meta->method("set_enabled(bool)").is_valid());

        // The state notifications live on item_t now (the state moved with them): every REAL change
        // emits exactly one signal carrying the new value, and an unchanged value announces nothing.
        // A property whose writer never announces would still read back correctly, so this is what
        // pins "the move kept the notification", not just "the field is writable".
        int visible_signals = 0;
        int enabled_signals = 0;
        bool visible_argument = true;
        bool enabled_argument = true;
        widget.connect("visible_changed(bool) const",
                       [&visible_signals, &visible_argument](bool value) {
                           ++visible_signals;
                           visible_argument = value;
                       });
        widget.connect("enabled_changed(bool) const",
                       [&enabled_signals, &enabled_argument](bool value) {
                           ++enabled_signals;
                           enabled_argument = value;
                       });
        widget.set_visible(false);
        CHECK(visible_signals == 1);
        CHECK(!visible_argument);
        widget.set_visible(false); // the same value again
        CHECK(visible_signals == 1);
        widget.set_enabled(false);
        CHECK(enabled_signals == 1);
        CHECK(!enabled_argument);
        widget.set_enabled(true);
        CHECK(enabled_signals == 2);
        CHECK(enabled_argument);
        widget.set_visible(true);
        CHECK(visible_signals == 2);
    }

    // ---- (9) painter prerequisites: the translate feature bit and the CPU fallback ----
    {
        // The SDL engine (image_t) declares primitive_transform / pixmap_transform / clip / blend and
        // NOT translate, which is what keeps the CPU fallback on the tested path.
        image_t image{ 4, 4 };
        paint_engine_t* const engine = image.paint_engine();
        CHECK(engine != nullptr);
        const engine_feature_t features = engine->features();
        CHECK(!bool(features & engine_feature_flag::TRANSLATE));
        CHECK(bool(features & engine_feature_flag::PRIMITIVE_TRANSFORM));
        CHECK(bool(features & engine_feature_flag::PIXMAP_TRANSFORM));
        CHECK(bool(features & engine_feature_flag::CLIP));
        CHECK(bool(features & engine_feature_flag::BLEND));

        // Engine path: a backend that declares translate receives the offset in the state snapshot and
        // the coordinates stay untouched.
        recording_device_t engine_device{ extent_t{ 40, 30 } };
        engine_device.engine().supports_translate = true;
        {
            painter_t painter{ engine_device };
            CHECK(painter.begin());
            painter.set_color(color_t::red_color());
            painter.translate(point_t{ 10, 5 });
            painter.fill_rect(rect_t{ 0, 0, 4, 3 });
            painter.end();
        }
        CHECK(engine_device.engine().glyph_runs == 0);
        CHECK(engine_device.engine().rects.size() == 1);
        CHECK(engine_device.engine().rects[0] == rect_t{ 0, 0, 4, 3 });  // not shifted here
        CHECK(engine_device.engine().last_state.translate() == point_t{ 10, 5 });
        CHECK((engine_device.engine().last_state.dirty_flags() & engine_state_t::dirty_translate) != 0);
        CHECK(engine_device.engine().ends == 1);

        // Glyph path, CPU fallback: the engine does not declare translate, so the painter shifts the
        // laid-out glyph positions (and the run origin) itself.
        recording_device_t text_device{ extent_t{ 100, 40 } };
        {
            painter_t painter{ text_device };
            CHECK(painter.begin());
            painter.translate(point_t{ 10, 5 });
            painter.draw_text(point_t{ 2, 12 }, "AB");
            painter.end();
        }
        CHECK(text_device.engine().glyph_runs == 1);
        CHECK(text_device.engine().origin == point_t{ 12, 17 });
        CHECK(text_device.engine().origin == text_device.engine().positions.front());
        CHECK(text_device.engine().positions.size() == 2);
        CHECK(text_device.engine().positions[1] == point_t{ 18, 17 });  // one 6-pixel advance later

        // Engine path for glyphs: unchanged positions, offset in the state.
        recording_device_t text_engine_device{ extent_t{ 100, 40 } };
        text_engine_device.engine().supports_translate = true;
        {
            painter_t painter{ text_engine_device };
            painter.begin();
            painter.translate(point_t{ 10, 5 });
            painter.draw_text(point_t{ 2, 12 }, "AB");
            painter.end();
        }
        CHECK(text_engine_device.engine().glyph_runs == 1);
        CHECK(text_engine_device.engine().origin == point_t{ 2, 12 });
        CHECK(text_engine_device.engine().last_state.translate() == point_t{ 10, 5 });

        // Pixel path: accumulated translation of rectangles (the path every widget render uses).
        image_t translated{ 30, 16 };
        {
            painter_t painter{ translated };
            CHECK(painter.begin());
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.translate(point_t{ 5, 2 });
            painter.fill_rect(rect_t{ 0, 0, 4, 3 });     // device (5,2)
            painter.translate(point_t{ 3, 1 });          // accumulates to (8,3)
            painter.set_color(color_t::green_color());
            painter.fill_rect(rect_t{ 12, 2, 4, 3 });    // device (20,5)
            painter.save();
            painter.translate(point_t{ 20, 20 });
            painter.set_color(color_t::blue_color());
            painter.fill_rect(rect_t{ 0, 0, 4, 3 });     // outside the device: nothing
            painter.restore();                            // back to translate (8,3), colour green
            painter.fill_rect(rect_t{ 2, 6, 4, 3 });     // device (10,9)
            painter.end();
        }
        CHECK(region_is(translated, rect_t{ 5, 2, 4, 3 }, color_t::red_color()));
        CHECK(region_is(translated, rect_t{ 20, 5, 4, 3 }, color_t::green_color()));
        CHECK(region_is(translated, rect_t{ 10, 9, 4, 3 }, color_t::green_color()));
        CHECK(translated.pixel(0, 0) == color_t::white_color());
        CHECK(count_color(translated, color_t::blue_color()) == 0);
        CHECK(count_color(translated, color_t::red_color()) == 4 * 3);
        CHECK(count_color(translated, color_t::green_color()) == 2 * 4 * 3);

        // Clip: nested set_clip() intersects, clear_clip() removes the clip.
        image_t clipped{ 20, 20 };
        {
            painter_t painter{ clipped };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.set_clip(rect_t{ 2, 2, 10, 8 });      // (2,2)-(12,10)
            painter.fill_rect(rect_t{ 0, 0, 20, 20 });
            painter.set_clip(rect_t{ 6, 4, 10, 10 });     // intersect -> (6,4)-(12,10)
            painter.set_color(color_t::green_color());
            painter.fill_rect(rect_t{ 0, 0, 20, 20 });
            painter.clear_clip();
            painter.set_color(color_t::blue_color());
            painter.fill_rect(rect_t{ 0, 16, 4, 4 });
            painter.end();
        }
        CHECK(region_is(clipped, rect_t{ 2, 2, 4, 2 }, color_t::red_color()));
        CHECK(region_is(clipped, rect_t{ 6, 4, 6, 6 }, color_t::green_color()));
        CHECK(region_is(clipped, rect_t{ 0, 16, 4, 4 }, color_t::blue_color()));
        CHECK(clipped.pixel(0, 0) == color_t::white_color());
        CHECK(clipped.pixel(12, 4) == color_t::white_color());
        CHECK(count_color(clipped, color_t::green_color()) == 6 * 6);

        // Clip + translate: the clip argument is in the painter's current coordinates, so it moves with
        // the accumulated translation and stays a DEVICE rectangle afterwards.
        image_t moved{ 20, 20 };
        {
            painter_t painter{ moved };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.translate(point_t{ 5, 5 });
            painter.set_clip(rect_t{ 0, 0, 4, 4 });       // device (5,5)-(9,9)
            painter.fill_rect(rect_t{ 0, 0, 20, 20 });    // would be (5,5)-(25,25)
            painter.end();
        }
        CHECK(region_is(moved, rect_t{ 5, 5, 4, 4 }, color_t::red_color()));
        CHECK(count_color(moved, color_t::red_color()) == 4 * 4);
        CHECK(moved.pixel(4, 4) == color_t::white_color());
        CHECK(moved.pixel(9, 9) == color_t::white_color());

        // save()/restore() pops the clip and the colour together.
        image_t popped{ 16, 8 };
        {
            painter_t painter{ popped };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.set_clip(rect_t{ 0, 0, 8, 8 });
            painter.save();
            painter.clear_clip();                          // replace, not intersect
            painter.set_clip(rect_t{ 8, 0, 8, 8 });
            painter.set_color(color_t::green_color());
            painter.fill_rect(rect_t{ 0, 0, 16, 8 });      // right half only
            painter.restore();                              // clip + colour back to left half / red
            painter.fill_rect(rect_t{ 0, 0, 16, 8 });      // left half only
            painter.end();
        }
        CHECK(region_is(popped, rect_t{ 0, 0, 8, 8 }, color_t::red_color()));
        CHECK(region_is(popped, rect_t{ 8, 0, 8, 8 }, color_t::green_color()));
        CHECK(count_color(popped, color_t::white_color()) == 0);

        // An unbalanced restore() is the documented no-op: state is untouched, drawing continues.
        image_t unbalanced{ 4, 4 };
        {
            painter_t painter{ unbalanced };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.restore();                              // empty stack: no-op, no crash
            painter.fill_rect(rect_t{ 0, 0, 2, 2 });
            painter.end();
        }
        CHECK(region_is(unbalanced, rect_t{ 0, 0, 2, 2 }, color_t::red_color()));

        // save() during an inactive painter and begin() resetting the stack: a save from a previous
        // pass must not leak into the next one.
        image_t reused{ 6, 6 };
        {
            painter_t painter{ reused };
            painter.save();                                 // before begin(): harmless
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.restore();                              // begin() cleared the stack: no-op
            painter.fill_rect(rect_t{ 0, 0, 2, 2 });
            painter.end();
        }
        CHECK(region_is(reused, rect_t{ 0, 0, 2, 2 }, color_t::red_color()));
    }

    if (g_failures == 0) {
        std::printf("ui: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("ui: %d checks, %d failure(s)\n", g_checks, g_failures);
    return 1;
}
