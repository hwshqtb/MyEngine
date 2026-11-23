// The GDI font engine, constructed DIRECTLY (not through the font database).
//
// Semantic assertions only: the system face is machine dependent, so nothing here asserts a pixel
// position or an exact advance. What it pins is that GDI really answers for a real face:
//  - 'A' has a positive advance, and the engine reports sane vertical metrics;
//  - CJK is mappable and its mask has ink (the whole point of the platform backend);
//  - the advance of a CJK character is about twice an ASCII one (a wide character is wide);
//  - a private-use code point has NO glyph, so the layout draws the missing-glyph box;
//  - masks are 8-bit coverage with a four-byte-aligned pitch (the format the interface promises).
//
// The engine is Windows-only by construction: on another platform its constructor throws, and this test
// then reports that it could not run instead of pretending to pass.

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

#include "hwshqtb/gui/font_engine_t.h"
#include "win32_font_engine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// The engine keeps Win32 out of its header; this test asks GDI directly, so it includes the header last
// (and undoes the CONST macro, exactly like the engine's own translation unit does).
#include <windows.h>
#ifdef CONST
#undef CONST
#endif

using hwshqtb::gui::glyph_bitmap_t;

namespace win32_font_engine_test {

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

}  // namespace win32_font_engine_test

using namespace win32_font_engine_test;

int main() {
    std::printf("== win32 font engine: GDI behind the font_engine_t seam ==\n");

    std::unique_ptr<hwshqtb::gui::detail::win32_font_engine> engine;
    try {
        // An empty family means "the system UI face"; 0 means "the size the system asks for".
        engine = std::make_unique<hwshqtb::gui::detail::win32_font_engine>("", 0);
    }
    catch (const std::exception& error) {
        std::printf("win32_font_engine: cannot run here (%s)\n", error.what());
        return 1;
    }

    std::printf("[win32font] face='%s' pixel_size=%d ascent=%d descent=%d leading=%d\n",
                std::string(engine->face_name()).c_str(), engine->pixel_size(), engine->ascent(),
                engine->descent(), engine->leading());

    // ---- (1) the vertical metrics are a real face's ------------------------------------------------
    CHECK(engine->pixel_size() > 0);
    CHECK(engine->ascent() > 0);
    CHECK(engine->descent() >= 0);
    CHECK(engine->leading() >= 0);
    // A face at N pixels has a line box of roughly N plus the internal leading; a wide sanity bound
    // catches "GDI handed us something else entirely" without pinning a machine-specific number.
    CHECK(engine->ascent() + engine->descent() >= engine->pixel_size() / 2);
    CHECK(engine->ascent() + engine->descent() <= engine->pixel_size() * 3);

    // ---- (2) ASCII: mappable, positive advance, ink ------------------------------------------------
    std::uint32_t ascii_glyph = 0;
    CHECK(engine->glyph_index(U'A', ascii_glyph));
    // The glyph identity IS the code point (see the engine's header): GDI's outline API takes the
    // character, so a separate index space would rasterise the wrong character.
    CHECK(ascii_glyph == static_cast<std::uint32_t>(U'A'));
    const int ascii_advance = engine->advance(ascii_glyph);
    CHECK(ascii_advance > 0);

    glyph_bitmap_t ascii_bitmap{};
    CHECK(engine->glyph_bitmap(ascii_glyph, ascii_bitmap));
    CHECK(ascii_bitmap.alpha != nullptr);
    CHECK(ascii_bitmap.width > 0);
    CHECK(ascii_bitmap.height > 0);
    CHECK(ascii_bitmap.pitch % 4 == 0); // the interface's row alignment
    CHECK(ascii_bitmap.pitch >= ascii_bitmap.width);
    CHECK(ascii_bitmap.antialiased);
    {
        int ink = 0;
        for (int row = 0; row < ascii_bitmap.height; ++row) {
            for (int column = 0; column < ascii_bitmap.width; ++column) {
                if (ascii_bitmap.alpha[row * ascii_bitmap.pitch + column] != 0) {
                    ++ink;
                }
            }
        }
        CHECK(ink > 0);
        std::printf("[win32font] 'A': advance %d, mask %dx%d pitch %d, %d ink px\n", ascii_advance,
                    ascii_bitmap.width, ascii_bitmap.height, ascii_bitmap.pitch, ink);
    }

    // ---- (3) CJK: mappable, ink, and roughly twice as wide as ASCII --------------------------------
    std::uint32_t cjk_glyph = 0;
    CHECK(engine->glyph_index(static_cast<char32_t>(0x4E2D), cjk_glyph)); // 中
    CHECK(cjk_glyph == 0x4E2Du);
    const int cjk_advance = engine->advance(cjk_glyph);
    CHECK(cjk_advance > 0);
    // A CJK character is about two ASCII cells wide, never one and never five.
    CHECK(cjk_advance >= ascii_advance * 3 / 2);
    CHECK(cjk_advance <= ascii_advance * 5 / 2);
    glyph_bitmap_t cjk_bitmap{};
    CHECK(engine->glyph_bitmap(cjk_glyph, cjk_bitmap));
    CHECK(cjk_bitmap.alpha != nullptr);
    CHECK(cjk_bitmap.pitch % 4 == 0);
    {
        int ink = 0;
        for (int row = 0; row < cjk_bitmap.height; ++row) {
            for (int column = 0; column < cjk_bitmap.width; ++column) {
                if (cjk_bitmap.alpha[row * cjk_bitmap.pitch + column] != 0) {
                    ++ink;
                }
            }
        }
        // A CJK glyph has a lot more ink than a Latin letter; the assertion is "not empty", not a count.
        CHECK(ink > 0);
        std::printf("[win32font] '中': advance %d (ASCII %d), mask %dx%d, %d ink px\n", cjk_advance,
                    ascii_advance, cjk_bitmap.width, cjk_bitmap.height, ink);
    }

    // ---- (3b) the left side bearing IS GDI's gmptGlyphOrigin.x -------------------------------------
    // The mask's bearing is the one value a renderer needs horizontally, and it is not a guess: this
    // compares it with the GDI call it came from, for the same face at the same pixel height.
    {
        HDC const dc = CreateCompatibleDC(nullptr);
        CHECK(dc != nullptr);
        HFONT const font = CreateFontW(-engine->pixel_size(), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                      std::wstring(engine->face_name().begin(), engine->face_name().end()).c_str());
        CHECK(font != nullptr);
        if (dc != nullptr && font != nullptr) {
            HGDIOBJ const previous = SelectObject(dc, font);
            // The identity MAT2 is mandatory: a null one answers GDI_ERROR for every format.
            const MAT2 identity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
            for (const char32_t codepoint : { U'A', U'i', U'j', U'W' }) {
                GLYPHMETRICS metrics{};
                if (GetGlyphOutlineW(dc, static_cast<UINT>(codepoint), GGO_METRICS, &metrics, 0, nullptr,
                                     &identity) == GDI_ERROR) {
                    continue;
                }
                std::uint32_t glyph = 0;
                if (!engine->glyph_index(codepoint, glyph)) {
                    continue;
                }
                glyph_bitmap_t mask{};
                if (!engine->glyph_bitmap(glyph, mask)) {
                    continue; // no pixels (a space)
                }
                CHECK(mask.bearing_x == static_cast<int>(metrics.gmptGlyphOrigin.x));
            }
            (void)SelectObject(dc, previous);
            DeleteObject(font);
            DeleteDC(dc);
        }
    }

    // ---- (4) uncovered code points have no glyph ---------------------------------------------------
    {
        std::uint32_t unused = 0;
        CHECK(!engine->glyph_index(static_cast<char32_t>(0xE000), unused)); // private use area
        CHECK(!engine->glyph_index(static_cast<char32_t>(0x10FFFF), unused)); // outside the BMP
        // A code point without a glyph still answers a mask query safely (no crash, no exception).
        glyph_bitmap_t none{};
        (void)engine->glyph_bitmap(unused, none);
    }

    // ---- (5) a space has an advance but no ink -----------------------------------------------------
    {
        std::uint32_t space = 0;
        CHECK(engine->glyph_index(U' ', space));
        CHECK(engine->advance(space) > 0);
        glyph_bitmap_t blank{};
        // GDI reports an empty box for a space; the engine answers "no pixels", which the paint engine
        // then skips instead of drawing an empty mask.
        CHECK(!engine->glyph_bitmap(space, blank));
    }

    if (g_failures == 0) {
        std::printf("win32 font engine: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("win32 font engine: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
