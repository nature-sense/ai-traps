/*
 * Copyright 2026 Nature Sense
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "overlay_actor.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>

namespace ct {

// ─── Colour palette (BGR for NV12 UV plane) ──────────────────────────────────
// Each entry: {Y, U, V} — bright, saturated colours that pop on camera feeds.
struct RgbTriplet { uint8_t r, g, b; };

static constexpr RgbTriplet kPalette[] = {
    {255,   0,   0},   // red
    {  0, 255,   0},   // green
    {  0,   0, 255},   // blue
    {255, 255,   0},   // yellow
    {255,   0, 255},   // magenta
    {  0, 255, 255},   // cyan
    {255, 128,   0},   // orange
    {128,   0, 255},   // purple
    {255, 255, 255},   // white
    {  0, 255, 128},   // spring green
};

static constexpr int kNumColors = sizeof(kPalette) / sizeof(kPalette[0]);

// Pick a colour based on track_id (stable per-track colour)
static const RgbTriplet& color_for_id(int track_id) {
    return kPalette[static_cast<unsigned>(track_id) % kNumColors];
}

// ─── RGB → YUV conversion (ITU-R BT.601, full range) ─────────────────────────
// NV12: Y = 0..255, U/V = 128-centred (0..255)
static void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b,
                       uint8_t& y, uint8_t& u, uint8_t& v) {
    y = static_cast<uint8_t>(( 66 * r + 129 * g +  25 * b + 128) / 256 +  16);
    u = static_cast<uint8_t>((-38 * r -  74 * g + 112 * b + 128) / 256 + 128);
    v = static_cast<uint8_t>((112 * r -  94 * g -  18 * b + 128) / 256 + 128);
}

// ─── draw_overlays ────────────────────────────────────────────────────────────
// Scales bbox coords from full-res → medium-res, then draws rect + label.
void OverlayActor::draw_overlays(FrameBuffer& frame,
                                const std::vector<TrackedObject>& tracked) {

    if (!frame.data || frame.width == 0 || frame.height == 0) return;

    const int fw = static_cast<int>(frame.width);
    const int fh = static_cast<int>(frame.height);
    const int stride = static_cast<int>(frame.stride);

    // NV12 layout: Y plane = stride × height bytes, UV plane = stride × height/2
    uint8_t* y_plane  = static_cast<uint8_t*>(frame.data);
    uint8_t* uv_plane = y_plane + stride * fh;

    // Scale factors: full-res → medium-res
    const float sx = static_cast<float>(fw) / static_cast<float>(full_w);
    const float sy = static_cast<float>(fh) / static_cast<float>(full_h);

    for (const auto& obj : tracked) {
        // Scale bbox to medium-res
        int rx = static_cast<int>(obj.full_x * sx);
        int ry = static_cast<int>(obj.full_y * sy);
        int rw = static_cast<int>(obj.full_w * sx);
        int rh = static_cast<int>(obj.full_h * sy);

        // Clamp to frame bounds
        rx = std::max(0, std::min(rx, fw - 1));
        ry = std::max(0, std::min(ry, fh - 1));
        rw = std::max(1, std::min(rw, fw - rx));
        rh = std::max(1, std::min(rh, fh - ry));

        const auto& col = color_for_id(obj.track_id);

        // Draw filled rectangle (semi-transparent effect via thin rect)
        draw_rect(y_plane, uv_plane, stride, fw, fh,
                  rx, ry, rw, rh, col.r, col.g, col.b);

        // Draw label: "ID:{track_id} {conf:.2f}"
        char label[64];
        std::snprintf(label, sizeof(label), "ID:%d %.2f",
                      obj.track_id, obj.detection.confidence);

        // Position label above the bbox (or inside if near top edge)
        int label_y = ry - 10;
        if (label_y < 0) label_y = ry + 2;
        draw_text(y_plane, stride, fw, fh, rx + 2, label_y, label, 220);
    }
}

// ─── draw_rect ────────────────────────────────────────────────────────────────
// Draws a 2-pixel-wide coloured rectangle on NV12 data.
void OverlayActor::draw_rect(uint8_t* y_plane, uint8_t* uv_plane,
                            int stride, int w, int h,
                            int rx, int ry, int rw, int rh,
                            uint8_t r, uint8_t g, uint8_t b) {
    uint8_t y_col, u_col, v_col;
    rgb_to_yuv(r, g, b, y_col, u_col, v_col);

    // Clamp rect to frame
    const int x1 = std::max(0, rx);
    const int y1 = std::max(0, ry);
    const int x2 = std::min(w - 1, rx + rw - 1);
    const int y2 = std::min(h - 1, ry + rh - 1);
    if (x2 <= x1 || y2 <= y1) return;

    const int thickness = 2;

    // ── Draw horizontal lines (top + bottom edges) ────────────────────────────
    for (int t = 0; t < thickness; ++t) {
        // Top edge
        int yy = y1 + t;
        if (yy < h) {
            uint8_t* y_row = y_plane + yy * stride;
            for (int xx = x1; xx <= x2; ++xx) y_row[xx] = y_col;
            // UV (subsampled 2x)
            if ((yy & 1) == 0) {
                uint8_t* uv_row = uv_plane + (yy / 2) * stride;
                for (int xx = x1; xx <= x2; xx += 2) {
                    uv_row[xx]     = v_col;  // V
                    uv_row[xx + 1] = u_col;  // U
                }
            }
        }
        // Bottom edge
        yy = y2 - t;
        if (yy >= 0) {
            uint8_t* y_row = y_plane + yy * stride;
            for (int xx = x1; xx <= x2; ++xx) y_row[xx] = y_col;
            if ((yy & 1) == 0) {
                uint8_t* uv_row = uv_plane + (yy / 2) * stride;
                for (int xx = x1; xx <= x2; xx += 2) {
                    uv_row[xx]     = v_col;
                    uv_row[xx + 1] = u_col;
                }
            }
        }
    }

    // ── Draw vertical lines (left + right edges) ──────────────────────────────
    for (int t = 0; t < thickness; ++t) {
        // Left edge
        int xx = x1 + t;
        if (xx < w) {
            for (int yy = y1 + thickness; yy <= y2 - thickness; ++yy) {
                uint8_t* y_row = y_plane + yy * stride;
                y_row[xx] = y_col;
                if ((yy & 1) == 0) {
                    uint8_t* uv_row = uv_plane + (yy / 2) * stride;
                    uv_row[xx & ~1]     = v_col;
                    uv_row[xx & ~1 | 1] = u_col;
                }
            }
        }
        // Right edge
        xx = x2 - t;
        if (xx >= 0) {
            for (int yy = y1 + thickness; yy <= y2 - thickness; ++yy) {
                uint8_t* y_row = y_plane + yy * stride;
                y_row[xx] = y_col;
                if ((yy & 1) == 0) {
                    uint8_t* uv_row = uv_plane + (yy / 2) * stride;
                    uv_row[xx & ~1]     = v_col;
                    uv_row[xx & ~1 | 1] = u_col;
                }
            }
        }
    }
}

// ─── draw_text ────────────────────────────────────────────────────────────────
// Minimal 5×7 bitmap font renderer.  Draws directly on the Y plane only
// (UV left at original values — text will be monochrome but readable).
void OverlayActor::draw_text(uint8_t* y_plane, int stride, int w, int h,
                            int x, int y, const char* text, uint8_t brightness) {
    if (!text || *text == '\0') return;

    // 5×7 bitmap font (5 columns × 7 rows per character)
    // Each character is defined by 7 bytes (rows), bits 0-4 = columns.
    static const uint8_t kFont[95][7] = {
        // Space (index 0)
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        // '!' (index 1)
        {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
        // '"' (index 2)
        {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00},
        // '#' (index 3)
        {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},
        // '$' (index 4)
        {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
        // '%' (index 5)
        {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
        // '&' (index 6)
        {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},
        // ''' (index 7)
        {0x04,0x04,0x04,0x00,0x00,0x00,0x00},
        // '(' (index 8)
        {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
        // ')' (index 9)
        {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
        // '*' (index 10)
        {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
        // '+' (index 11)
        {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
        // ',' (index 12)
        {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
        // '-' (index 13)
        {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
        // '.' (index 14)
        {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
        // '/' (index 15)
        {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
        // '0' (index 16)
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        // '1' (index 17)
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        // '2' (index 18)
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        // '3' (index 19)
        {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
        // '4' (index 20)
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        // '5' (index 21)
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        // '6' (index 22)
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        // '7' (index 23)
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        // '8' (index 24)
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        // '9' (index 25)
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
        // ':' (index 26)
        {0x00,0x00,0x04,0x00,0x00,0x04,0x00},
        // ';' (index 27)
        {0x00,0x00,0x04,0x00,0x00,0x04,0x08},
        // '<' (index 28)
        {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
        // '=' (index 29)
        {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
        // '>' (index 30)
        {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
        // '?' (index 31)
        {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
        // '@' (index 32)
        {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},
        // 'A' (index 33)
        {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11},
        // 'B' (index 34)
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        // 'C' (index 35)
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        // 'D' (index 36)
        {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
        // 'E' (index 37)
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        // 'F' (index 38)
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        // 'G' (index 39)
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        // 'H' (index 40)
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        // 'I' (index 41)
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        // 'J' (index 42)
        {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        // 'K' (index 43)
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        // 'L' (index 44)
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        // 'M' (index 45)
        {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},
        // 'N' (index 46)
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        // 'O' (index 47)
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        // 'P' (index 48)
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        // 'Q' (index 49)
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        // 'R' (index 50)
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        // 'S' (index 51)
        {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
        // 'T' (index 52)
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        // 'U' (index 53)
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        // 'V' (index 54)
        {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
        // 'W' (index 55)
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        // 'X' (index 56)
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        // 'Y' (index 57)
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        // 'Z' (index 58)
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
        // '[' (index 59)
        {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
        // '\' (index 60)
        {0x10,0x08,0x08,0x04,0x02,0x02,0x01},
        // ']' (index 61)
        {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
        // '^' (index 62)
        {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
        // '_' (index 63)
        {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
        // '`' (index 64)
        {0x08,0x04,0x02,0x00,0x00,0x00,0x00},
        // 'a' (index 65)
        {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
        // 'b' (index 66)
        {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},
        // 'c' (index 67)
        {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
        // 'd' (index 68)
        {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
        // 'e' (index 69)
        {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
        // 'f' (index 70)
        {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
        // 'g' (index 71)
        {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
        // 'h' (index 72)
        {0x10,0x10,0x16,0x19,0x11,0x11,0x11},
        // 'i' (index 73)
        {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
        // 'j' (index 74)
        {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
        // 'k' (index 75)
        {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
        // 'l' (index 76)
        {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
        // 'm' (index 77)
        {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
        // 'n' (index 78)
        {0x00,0x00,0x16,0x19,0x11,0x11,0x11},
        // 'o' (index 79)
        {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
        // 'p' (index 80)
        {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
        // 'q' (index 81)
        {0x00,0x00,0x0D,0x13,0x0F,0x01,0x01},
        // 'r' (index 82)
        {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
        // 's' (index 83)
        {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
        // 't' (index 84)
        {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
        // 'u' (index 85)
        {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
        // 'v' (index 86)
        {0x00,0x00,0x11,0x11,0x0A,0x0A,0x04},
        // 'w' (index 87)
        {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
        // 'x' (index 88)
        {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
        // 'y' (index 89)
        {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
        // 'z' (index 90)
        {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
        // '{' (index 91)
        {0x02,0x04,0x04,0x08,0x04,0x04,0x02},
        // '|' (index 92)
        {0x04,0x04,0x04,0x00,0x04,0x04,0x04},
        // '}' (index 93)
        {0x08,0x04,0x04,0x02,0x04,0x04,0x08},
        // '~' (index 94)
        {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
    };

    const int char_w = 6;   // 5 pixels + 1 space
    const int char_h = 8;   // 7 rows + 1 blank row

    for (const char* p = text; *p; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch < 32 || ch > 126) { ch = ' '; }
        const uint8_t* glyph = kFont[ch - 32];

        int cx = x + static_cast<int>(p - text) * char_w;
        if (cx + 5 >= w || cx < 0) continue;

        for (int row = 0; row < 7; ++row) {
            int yy = y + row;
            if (yy < 0 || yy >= h) continue;
            uint8_t bits = glyph[row];
            uint8_t* y_row = y_plane + yy * stride;
            for (int col = 0; col < 5; ++col) {
                int xx = cx + col;
                if (xx >= 0 && xx < w && (bits & (0x10 >> col))) {
                    y_row[xx] = brightness;
                }
            }
        }
    }
}

} // namespace ct
