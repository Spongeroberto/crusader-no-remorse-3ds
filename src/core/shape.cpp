#include "core/shape.hpp"

#include "core/bytes.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace crusader {

namespace {
constexpr std::size_t kShapeHeader = 6;
constexpr std::size_t kFrameEntry = 8;
constexpr int kMaxDimension = 4096;

// @orig 1078:0116; frame header, the pixels are read by the asm blitters
ShapeFrame parse_frame(std::span<const std::uint8_t> data, std::size_t i, std::size_t prefix) {
    const std::size_t entry = kShapeHeader + kFrameEntry * i;
    const std::uint32_t offset = read_u24(data, entry);
    // The original (HANDLER_1070_026a) only reads the offset. The length is
    // unreliable: from frame 58 on, font 2 of FONTS.FLX stores the end
    // offset there instead. Bound it by the end of the shape.
    std::uint32_t length = read_u32(data, entry + 4);
    check_range(data, offset, 0);
    length = std::min<std::uint32_t>(length, static_cast<std::uint32_t>(data.size() - offset));
    const auto frame_data = data.subspan(offset, length);

    ShapeFrame f;
    f.compressed = read_i32(frame_data, prefix) != 0;
    f.width = read_i32(frame_data, prefix + 4);
    f.height = read_i32(frame_data, prefix + 8);
    f.xoff = read_i32(frame_data, prefix + 12);
    f.yoff = read_i32(frame_data, prefix + 16);
    if (f.width < 0 || f.height < 0 || f.width > kMaxDimension || f.height > kMaxDimension) {
        throw std::runtime_error("shape frame has invalid dimensions");
    }

    const std::size_t table = prefix + 20;
    const std::size_t table_size = std::size_t{4} * f.height;
    check_range(frame_data, table, table_size);
    f.lines.resize(f.height);
    for (int y = 0; y < f.height; ++y) {
        // Stored relative to the entry itself; rebase on the end of the table.
        const std::uint32_t rel = read_u32(frame_data, table + 4 * y);
        f.lines[y] = rel - static_cast<std::uint32_t>(4 * (f.height - y));
    }
    f.rle = frame_data.subspan(table + table_size);
    return f;
}

}  // namespace

// @orig none: the original reads the frames in place
Shape Shape::parse(std::span<const std::uint8_t> data, ShapeLayout layout, bool skip_bad_frames) {
    const std::size_t prefix = layout == ShapeLayout::World ? 8 : 0;
    Shape shape;
    const std::uint16_t count = read_u16(data, 4);
    shape.frames_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!skip_bad_frames) {
            shape.frames_.push_back(parse_frame(data, i, prefix));
            continue;
        }
        try {
            shape.frames_.push_back(parse_frame(data, i, prefix));
        } catch (const std::exception&) {
            shape.frames_.emplace_back();
            ++shape.bad_frames_;
        }
    }
    return shape;
}

// @orig none: layout chosen per archive
ShapeLayout layout_for_archive(const std::string& filename) {
    std::string upper;
    for (char c : filename) {
        upper += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    return upper.find("GUMPS") != std::string::npos || upper.find("MOUSE") != std::string::npos
               ? ShapeLayout::Flat2D
               : ShapeLayout::World;
}

// @orig 1378:0105; pixel test of ShapeButton_contains
int ShapeFrame::pixel(int fx, int fy) const {
    if (fx < 0 || fy < 0 || fx >= width || fy >= height) return -1;
    std::size_t p = lines[fy];
    int lx = 0;
    while (lx < width) {
        lx += read_u8(rle, p++);
        if (lx > fx || lx >= width) return -1;  // inside a transparent run
        int len = read_u8(rle, p++);
        bool fill = false;
        if (compressed) {
            fill = len & 1;
            len >>= 1;
        }
        if (fx < lx + len) return fill ? read_u8(rle, p) : read_u8(rle, p + (fx - lx));
        p += fill ? 1 : len;
        lx += len;
    }
    return -1;
}

// @orig none: consistency check for the tools
bool ShapeFrame::runs_match_width() const {
    for (int ly = 0; ly < height; ++ly) {
        std::size_t p = lines[ly];
        int lx = 0;
        while (true) {
            lx += read_u8(rle, p++);
            if (lx >= width) {
                break;
            }
            int len = read_u8(rle, p++);
            bool fill = false;
            if (compressed) {
                fill = len & 1;
                len >>= 1;
            }
            p += fill ? 1 : len;
            lx += len;
            if (lx >= width) {
                break;
            }
        }
        if (lx != width) {
            return false;
        }
    }
    return true;
}

// @orig ?: asm blitters (1288:00A9 relocatable block), not disassembled
//
// Rendering spends most of its time here, so the clipping is worked out once
// per call instead of per pixel: rows above and below the surface are not
// decoded, a line stops at the last visible column, and each run is cut to
// the visible columns before its pixels are copied or filled in one go. Only
// the bytes a visible run uses are bounds-checked.
// e2e/shape_draw_tests.cpp checks it pixel for pixel against the plain
// per-pixel version on every frame of the game data.
void ShapeFrame::draw(Surface8& dst, int x, int y, bool mirrored, const XformTable* xform) const {
    const int top = y - yoff;
    const int left = mirrored ? x + xoff - (width - 1) : x - xoff;

    // Frame columns [fx0, fx1) and lines [ly0, ly1) that land on the surface.
    // Mirrored, frame column fx goes to surface column left + width - 1 - fx.
    const int fx0 = std::max(0, mirrored ? left + width - dst.width : -left);
    const int fx1 = std::min(width, mirrored ? left + width : dst.width - left);
    const int ly0 = std::max(0, -top);
    const int ly1 = std::min(height, dst.height - top);
    if (fx0 >= fx1 || ly0 >= ly1) return;

    const std::uint8_t* const data = rle.data();
    for (int ly = ly0; ly < ly1; ++ly) {
        std::uint8_t* const row = dst.row(top + ly);
        std::size_t p = lines[ly];
        int lx = 0;
        while (lx < width) {
            lx += read_u8(rle, p++);  // transparent run
            if (lx >= width) {
                break;
            }
            int len = read_u8(rle, p++);
            bool fill = false;
            if (compressed) {
                fill = len & 1;
                len >>= 1;
            }
            // A couple of frames in SHAPES.FLX overshoot the frame width on one
            // line (data quirk); never draw outside the frame.
            const int a = std::max(lx, fx0);
            const int b = std::min({lx + len, width, fx1});
            if (a < b) {
                const int n = b - a;
                const std::size_t src = fill ? p : p + (a - lx);
                check_range(rle, src, fill ? 1 : n);
                const std::uint8_t* in = data + src;
                if (!mirrored) {
                    std::uint8_t* out = row + left + a;
                    if (fill) {
                        const std::uint8_t c = *in;
                        if (!xform || !xform->translucent(c)) {
                            std::memset(out, c, n);
                        } else {
                            for (int k = 0; k < n; ++k) out[k] = xform->blend(c, out[k]);
                        }
                    } else if (!xform) {
                        std::memcpy(out, in, n);
                    } else {
                        for (int k = 0; k < n; ++k) out[k] = xform->blend(in[k], out[k]);
                    }
                } else {
                    // Walks leftwards on the surface as the frame column grows.
                    std::uint8_t* out = row + left + width - 1 - a;
                    if (fill) {
                        const std::uint8_t c = *in;
                        if (!xform || !xform->translucent(c)) {
                            std::memset(out - (n - 1), c, n);
                        } else {
                            for (int k = 0; k < n; ++k) out[-k] = xform->blend(c, out[-k]);
                        }
                    } else if (!xform) {
                        for (int k = 0; k < n; ++k) out[-k] = in[k];
                    } else {
                        for (int k = 0; k < n; ++k) out[-k] = xform->blend(in[k], out[-k]);
                    }
                }
            }
            p += fill ? 1 : len;
            lx += len;
            if (lx >= fx1) {
                break;  // the rest of the line is off the surface
            }
        }
    }
}

}  // namespace crusader
