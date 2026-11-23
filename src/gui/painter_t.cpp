#include "hwshqtb/gui/painter_t.h"

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/paint_engine_t.h"
#include "hwshqtb/gui/text_layout_t.h"

#include <cstddef>
#include <vector>

namespace hwshqtb::gui {
    painter_t::painter_t(paint_device_t& device):
        _device(&device) { }

    painter_t::~painter_t() {
        if (_active) {
            (void)end();
        }
    }

    bool painter_t::begin() {
        if (_active) {
            return true;
        }
        // A new pass starts from the default state with every field dirty, because this engine may
        // not have seen a snapshot from this painter yet (begin() resets the backend as well). The
        // translation stack is per pass for the same reason: nothing a previous pass saved can be
        // restored into this one.
        _state = engine_state_t{};
        _saved.clear();
        _translate = point_t{};
        _clip = rect_t{};
        _clip_region = region_t{};
        _clip_enabled = false;
        // The engine is the DEVICE's, and it is created while the device is set up: begin() only asks
        // for it. No engine means "this frame cannot be painted", which is a false return, not an
        // exception.
        paint_engine_t* const engine = _device->paint_engine();
        if (engine == nullptr) {
            // No engine: forget any capability answer a previous pass left behind, so supports() cannot
            // report capabilities this (unpaintable) state does not have.
            _engine_features = engine_feature_t{};
            _engine_translate = false;
            _active = false;
            return false;
        }
        _active = engine->begin(*_device);
        if (_active) {
            // Capability queries once per pass: whether the offset goes to the backend or is added to
            // the coordinates here (SDL does not declare it), and whether a region clip can go to the
            // backend in one piece.
            _engine_features = engine->features();
            _engine_translate = bool(_engine_features & engine_feature_flag::TRANSLATE);
        }
        return _active;
    }

    bool painter_t::end() {
        if (!_active) {
            return true;
        }
        flush_state();
        // A pass can only be active on a device that had an engine when it started, and a device keeps
        // its engine for its lifetime: the query cannot come back empty here.
        paint_engine_t* const engine = _device->paint_engine();
        const bool ok = engine != nullptr ? engine->end() : true;
        _active = false;
        return ok;
    }

    bool painter_t::is_active() const noexcept {
        return _active;
    }

    void painter_t::set_color(color_t color) {
        _state.set_color(color);
    }

    void painter_t::clear(color_t color) {
        if (!_active) {
            return;
        }
        const extent_t extent = _device->size();
        if (extent.isEmpty()) {
            return;
        }
        clear(rect_t{ 0, 0, extent.width(), extent.height() }, color);
    }

    void painter_t::clear(rect_t rect, color_t color) {
        if (!_active || rect.isEmpty()) {
            return;
        }
        // Background fill: the current colour, clip and translation are set aside so the fill covers
        // the requested device rectangle regardless of any clip/translate in effect, then put back.
        // This is the one operation that ignores the W1 state (documented on the class).
        const saved_state_t previous{ _state.color(), _clip, _clip_enabled, _translate, _clip_region };
        _clip = rect_t{};
        _clip_region = region_t{};
        _clip_enabled = false;
        _translate = point_t{};
        _state.set_color(color);
        _state.clear_clip();
        if (_engine_translate) {
            _state.set_translate(point_t{});
        }
        draw_one(rect, true);
        apply_saved_state(previous);
    }

    void painter_t::fill_rect(rect_t rect) {
        if (!_active || rect.isEmpty()) {
            return;
        }
        draw_one(rect, true);
    }

    void painter_t::draw_rect(rect_t rect) {
        if (!_active || rect.isEmpty()) {
            return;
        }
        draw_one(rect, false);
    }

    void painter_t::draw_image(rect_t destination, const image_t& image) {
        if (!_active || destination.isEmpty()) {
            return;
        }
        paint_engine_t* const target = engine();
        if (target == nullptr) {
            return; // no engine: nothing to draw into (the pass would not have started)
        }
        const image_view_t view = image.view();
        if (view.pixels == nullptr || view.width <= 0 || view.height <= 0 || view.pitch < view.width) {
            return; // a moved-from image (or one without pixels) has nothing to blit
        }
        flush_state();
        // The W1 translate fallback, exactly like draw_one(): a backend that cannot translate gets the
        // primitive already moved. The clip is in device coordinates by construction and the engine
        // applies it, so the destination is NOT cropped here — cropping it would stretch the image.
        if (!_engine_translate) {
            destination = destination.translated(_translate);
        }
        target->draw_image(destination, view);
    }
    void painter_t::draw_text(point_t baseline_origin, std::string_view text, const font_t& font) {
        if (!_active || text.empty()) {
            return;
        }
        // Layout only: text_layout_t decodes the code points, maps them to glyphs through the font
        // database, emits one run per engine segment and computes the extent. '\n' is consumed there (it
        // moves the pen to the next baseline) and therefore never reaches the engine.
        text_layout_t layout{ text, font };
        if (layout.runs().empty()) {
            return; // a string of newlines only: nothing to rasterise
        }
        flush_state();
        // The layout is built from the top-left pen origin; the requested baseline origin (plus the W1
        // translate fallback for a backend without engine_feature_flag::TRANSLATE) is applied as one
        // offset. A backend that declares the feature receives the offset in the state snapshot instead
        // and shifts the glyphs itself.
        point_t origin = baseline_origin;
        if (!_engine_translate) {
            origin += _translate;
        }
        if (origin.x() != 0 || origin.y() != 0) {
            layout.translate(origin);
        }
        // The run's colour is the painter's; the layout does not know one. A backend receives one run per
        // engine segment, so a fallback chain paints the same way as a single face.
        for (const glyph_run_t& prototype : layout.runs()) {
            glyph_run_t run = prototype;
            run.color = _state.color();
            engine()->draw_glyphs(run);
        }
    }

    void painter_t::translate(point_t offset) {
        _translate += offset;
        if (_engine_translate) {
            _state.set_translate(_translate);
        }
    }

    void painter_t::set_clip(rect_t rect) {
        // The argument is in the painter's current user coordinates, so the accumulated translation
        // is applied once, here, and the result is what the engine receives as a DEVICE rectangle.
        // Nested calls intersect with the clip already in effect (Qt's clip stack).
        const rect_t device_rect = rect.translated(_translate);
        if (!_clip_enabled) {
            _clip = device_rect;
            _clip_region = region_t{};
        }
        else if (_clip_region.is_empty()) {
            _clip = _clip.intersected(device_rect);
        }
        else {
            // The clip in effect is a region: intersect it, and fall back to the rectangle form when
            // one rectangle is all that is left (the cheap form for every caller after this point).
            region_t narrowed = _clip_region;
            narrowed.intersect(device_rect);
            if (narrowed.rectangle_count() <= 1) {
                _clip_region = region_t{};
                _clip = narrowed.is_empty() ? rect_t{} : narrowed.rects().front();
            }
            else {
                _clip = narrowed.bounds();
                _clip_region = narrowed;
            }
        }
        _clip_enabled = true;
        publish_clip();
    }

    void painter_t::set_clip_region(const region_t& region) {
        // The argument is in the painter's current user coordinates, like set_clip()'s rectangle.
        region_t effective = region;
        effective = effective.translated(_translate.x(), _translate.y());
        if (_clip_enabled) {
            effective = _clip_region.is_empty() ? region_t{ _clip }.intersected(effective)
                                                : _clip_region.intersected(effective);
        }
        if (effective.rectangle_count() <= 1) {
            // One rectangle (or none): the rectangle form is exactly equivalent and cheaper.
            _clip = effective.is_empty() ? rect_t{} : effective.rects().front();
            _clip_region = region_t{};
        }
        else {
            _clip = effective.bounds();
            _clip_region = effective;
        }
        _clip_enabled = true;
        publish_clip();
    }

    void painter_t::clear_clip() {
        _clip = rect_t{};
        _clip_region = region_t{};
        _clip_enabled = false;
        _state.clear_clip();
    }

    rect_t painter_t::clip_rect() const noexcept {
        // The clip is stored in device coordinates; the caller works in the current user coordinates,
        // so the accumulated translation is subtracted once here (the exact inverse of set_clip()).
        if (_clip_enabled) {
            return _clip.translated(point_t{ -_translate.x(), -_translate.y() });
        }
        const extent_t extent = _device->size();
        return rect_t{ 0, 0, extent.width(), extent.height() }
            .translated(point_t{ -_translate.x(), -_translate.y() });
    }

    region_t painter_t::clip_region() const {
        region_t device_clip;
        if (!_clip_enabled) {
            const extent_t extent = _device->size();
            device_clip.set(rect_t{ 0, 0, extent.width(), extent.height() });
        }
        else if (_clip_region.is_empty()) {
            device_clip.set(_clip);
        }
        else {
            device_clip = _clip_region;
        }
        return device_clip.translated(-_translate.x(), -_translate.y());
    }

    bool painter_t::supports(engine_feature_flag feature) const noexcept {
        return bool(_engine_features & feature);
    }

    void painter_t::publish_clip() {
        if (!_clip_enabled) {
            _state.clear_clip();
            return;
        }
        if (!_clip_region.is_empty() && supports(engine_feature_flag::CLIP_REGION)) {
            // The region goes to the backend in one piece: it is the only form that is exact for more
            // than one rectangle.
            _state.set_clip_region(std::span<const rect_t>{ _clip_region.rects() }, _clip);
            return;
        }
        // A rectangle clip, or a backend that cannot clip to a region: it gets the bounding rectangle
        // (a superset, never a subset — see set_clip_region()).
        _state.set_clip(_clip_region.is_empty() ? _clip : _clip_region.bounds());
    }

    void painter_t::save() {
        // The engine snapshot is not copied: only the three fields the widget layer changes are
        // saved, and apply_saved_state() rebuilds the snapshot fields from them.
        _saved.push_back(saved_state_t{ _state.color(), _clip, _clip_enabled, _translate, _clip_region });
    }

    void painter_t::restore() {
        if (_saved.empty()) {
            return; // documented no-op, see the header (unbalanced restore cannot corrupt state)
        }
        const saved_state_t saved = _saved.back();
        _saved.pop_back();
        apply_saved_state(saved);
    }

    void painter_t::apply_saved_state(const saved_state_t& saved) {
        _state.set_color(saved.color);
        _clip = saved.clip;
        _clip_region = saved.clip_region;
        _clip_enabled = saved.clip_enabled;
        publish_clip();
        _translate = saved.translate;
        if (_engine_translate) {
            _state.set_translate(_translate);
        }
    }

    paint_engine_t* painter_t::engine() const noexcept {
        return _device != nullptr ? _device->paint_engine() : nullptr;
    }

    void painter_t::flush_state() {
        if (!_state.is_dirty()) {
            return;
        }
        // Nothing to push into: every entry point guards on _active, and an active pass implies an
        // engine, so this is only reachable for a painter whose pass never started.
        if (paint_engine_t* const target = engine(); target != nullptr) {
            target->update_state(_state);
        }
        _state.clear_dirty();
    }

    void painter_t::draw_one(rect_t rect, bool fill) {
        paint_engine_t* const target = engine();
        if (target == nullptr) {
            return; // no engine: nothing to draw into (the pass would not have started)
        }
        flush_state();
        // The W1 translate fallback: a backend that cannot translate gets the primitive already
        // moved. The clip is not touched here — it is in device coordinates by construction.
        if (!_engine_translate) {
            rect = rect.translated(_translate);
        }
        target->draw_rects(&rect, 1, fill);
    }
}
