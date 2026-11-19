#include "core/shape_reach.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

namespace crusader {

namespace {
constexpr const char* kHeader = "crusader shape reach 1";
}

// @orig none: port-only bound for the display list
void ShapeReach::include(const ShapeFrame& f) {
    if (f.width == 0 || f.height == 0) return;
    // Unmirrored the frame starts xoff left of the anchor; mirrored it ends
    // xoff right of it (ShapeFrame::draw).
    left = std::max({left, f.xoff, f.width - 1 - f.xoff});
    right = std::max({right, f.width - f.xoff, f.xoff + 1});
    up = std::max(up, f.yoff);
    down = std::max(down, f.height - f.yoff);
}

void ShapeReach::include(const Shape& s) {
    for (const ShapeFrame& f : s.frames()) include(f);
}

ShapeReach archive_reach(const FlexArchive& archive, ShapeLayout layout) {
    ShapeReach r;
    for (std::size_t i = 0; i < archive.count(); ++i) {
        const auto entry = archive.entry(i);
        if (entry.empty()) continue;
        try {
            r.include(Shape::parse(entry, layout, true));
        } catch (const std::exception&) {
            // A shape the game cannot parse is never drawn either.
        }
    }
    return r;
}

void write_reach_file(const std::filesystem::path& path, const ShapeReach& reach, std::uint64_t archive_size) {
    std::ofstream out(path, std::ios::trunc);
    out << kHeader << '\n' << archive_size << '\n'
        << reach.left << ' ' << reach.right << ' ' << reach.up << ' ' << reach.down << '\n';
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::optional<ShapeReach> read_reach_file(const std::filesystem::path& path, std::uint64_t archive_size) {
    std::ifstream in(path);
    std::string header;
    if (!in || !std::getline(in, header) || header != kHeader) return std::nullopt;
    std::uint64_t size = 0;
    ShapeReach r;
    if (!(in >> size >> r.left >> r.right >> r.up >> r.down) || size != archive_size) return std::nullopt;
    return r;
}

}  // namespace crusader
