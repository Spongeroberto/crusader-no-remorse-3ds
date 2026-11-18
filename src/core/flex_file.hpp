#pragma once
// A Flex archive read on request: only the index is read when it is opened,
// and entries are read from the file when asked for. For archives too large
// to hold whole (SHAPES.FLX on a console). FlexArchive reads the whole file.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <vector>

namespace crusader {

class FlexFile {
public:
    static FlexFile open(const std::filesystem::path& path);

    std::size_t count() const { return index_.size(); }
    std::uint32_t size(std::size_t i) const { return i < index_.size() ? index_[i].size : 0; }

    // One entry, empty when it is empty or out of range.
    std::vector<std::uint8_t> read(std::size_t i);

    // Several entries, handed to `out` one by one. Reading a file costs a
    // fixed delay per request on slow storage (about 8 ms on a 3DS), so the
    // entries are read in file order and neighbours less than `max_gap` bytes
    // apart share one request, up to `max_request` bytes. Empty and
    // out-of-range entries are skipped.
    void read_many(std::span<const std::size_t> entries,
                   const std::function<void(std::size_t entry, std::vector<std::uint8_t> bytes)>& out,
                   std::size_t max_gap = 64 * 1024, std::size_t max_request = 1024 * 1024);

    // File reads issued so far.
    std::size_t requests() const { return requests_; }

private:
    struct Slot {
        std::uint32_t offset;
        std::uint32_t size;
    };
    void read_at(std::uint32_t offset, std::uint8_t* out, std::size_t size);

    std::filesystem::path path_;
    std::ifstream in_;
    std::vector<Slot> index_;
    std::size_t requests_ = 0;
};

}  // namespace crusader
