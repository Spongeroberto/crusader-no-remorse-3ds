// Writes the reach bound of SHAPES.FLX (core/shape_reach.hpp): how far any
// frame reaches from its anchor, for the display list of a game that does not
// hold the whole archive in memory. A console build runs it on the data it
// bundles.
//
// Usage: shape_reach <SHAPES.FLX> <output file>

#include "core/flex.hpp"
#include "core/shape_reach.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: shape_reach <SHAPES.FLX> <output file>\n");
        return 2;
    }
    try {
        const auto archive = crusader::FlexArchive::open(argv[1]);
        const crusader::ShapeReach r = crusader::archive_reach(archive, crusader::ShapeLayout::World);
        crusader::write_reach_file(argv[2], r, std::filesystem::file_size(argv[1]));
        std::printf("shape reach: left %d right %d up %d down %d\n", r.left, r.right, r.up, r.down);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "shape_reach: %s\n", e.what());
        return 1;
    }
}
