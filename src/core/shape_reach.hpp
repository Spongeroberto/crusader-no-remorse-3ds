#pragma once
// How far the frames of a shape reach from their anchor on screen (port
// addition). The display list uses a bound over all of SHAPES.FLX to visit
// only the map cells whose items can reach the view, instead of every item.
//
// The bound is computed from the whole archive: at start-up when it is in
// memory, else when a console's CIA is built (shape_reach tool), shipped as
// a small text file beside SHAPES.FLX:
//   crusader shape reach 1
//   <SHAPES.FLX size in bytes>
//   <left> <right> <up> <down>

#include "core/flex.hpp"
#include "core/shape.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace crusader {

// For an anchor at (x, y), a frame's pixels lie in [x - left, x + right) x
// [y - up, y + down), whether drawn mirrored or not.
struct ShapeReach {
    int left = 0, right = 0, up = 0, down = 0;

    void include(const ShapeFrame& f);
    void include(const Shape& s);
    bool covers(const ShapeReach& r) const {
        return r.left <= left && r.right <= right && r.up <= up && r.down <= down;
    }
    bool operator==(const ShapeReach&) const = default;
};

// Over every frame of every shape of the archive (malformed frames skipped).
ShapeReach archive_reach(const FlexArchive& archive, ShapeLayout layout);

void write_reach_file(const std::filesystem::path& path, const ShapeReach& reach, std::uint64_t archive_size);
// The bound, if the file exists, is well formed and was made for an archive
// of `archive_size` bytes.
std::optional<ShapeReach> read_reach_file(const std::filesystem::path& path, std::uint64_t archive_size);

}  // namespace crusader
