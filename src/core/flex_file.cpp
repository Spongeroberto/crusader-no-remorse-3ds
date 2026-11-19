#include "core/flex_file.hpp"

#include "core/bytes.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace crusader {

// @orig none: the original reads the archives whole (see flex.cpp)
FlexFile FlexFile::open(const std::filesystem::path& path) {
    constexpr std::size_t kCountOffset = 0x54;
    constexpr std::size_t kIndexOffset = 0x80;

    FlexFile f;
    f.path_ = path;
    f.in_.open(path, std::ios::binary);
    if (!f.in_) throw std::runtime_error("cannot open " + path.string());
    f.in_.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::uint64_t>(f.in_.tellg());
    f.file_size_ = file_size;
    if (file_size < kIndexOffset) throw std::runtime_error(path.string() + ": too small for a Flex archive");

    std::vector<std::uint8_t> header(kIndexOffset);
    f.read_at(0, header.data(), header.size());
    const std::uint32_t count = read_u32(header, kCountOffset);
    if (kIndexOffset + std::uint64_t{count} * 8 > file_size) {
        throw std::runtime_error(path.string() + ": index exceeds file size");
    }
    std::vector<std::uint8_t> index(std::size_t{count} * 8);
    f.read_at(kIndexOffset, index.data(), index.size());
    f.index_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Slot s{read_u32(index, 8 * i), read_u32(index, 8 * i + 4)};
        if (s.offset != 0 && std::uint64_t{s.offset} + s.size > file_size) {
            throw std::runtime_error(path.string() + ": entry " + std::to_string(i) + " out of bounds");
        }
        // Offset 0 marks an empty entry, whatever its size says.
        if (s.offset == 0) s.size = 0;
        f.index_.push_back(s);
    }
    return f;
}

void FlexFile::read_at(std::uint32_t offset, std::uint8_t* out, std::size_t size) {
    in_.clear();
    in_.seekg(offset);
    in_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    if (static_cast<std::size_t>(in_.gcount()) != size) {
        throw std::runtime_error(path_.string() + ": short read at " + std::to_string(offset));
    }
    ++requests_;
}

std::vector<std::uint8_t> FlexFile::read(std::size_t i) {
    std::vector<std::uint8_t> bytes(size(i));
    if (!bytes.empty()) read_at(index_[i].offset, bytes.data(), bytes.size());
    return bytes;
}

void FlexFile::read_many(std::span<const std::size_t> entries,
                         const std::function<void(std::size_t, std::vector<std::uint8_t>)>& out,
                         std::size_t max_gap, std::size_t max_request) {
    std::vector<std::size_t> order;
    for (std::size_t i : entries) {
        if (size(i) != 0) order.push_back(i);
    }
    std::sort(order.begin(), order.end(),
              [this](std::size_t a, std::size_t b) { return index_[a].offset < index_[b].offset; });
    order.erase(std::unique(order.begin(), order.end()), order.end());

    std::vector<std::uint8_t> buffer;
    for (std::size_t first = 0; first < order.size();) {
        // Grow the request while the next entry is close and it stays small
        // enough; a single entry larger than max_request is read alone.
        const std::uint32_t start = index_[order[first]].offset;
        std::uint64_t end = std::uint64_t{start} + index_[order[first]].size;
        std::size_t last = first + 1;
        for (; last < order.size(); ++last) {
            const Slot& s = index_[order[last]];
            const std::uint64_t next_end = std::max<std::uint64_t>(end, std::uint64_t{s.offset} + s.size);
            if (s.offset > end + max_gap || next_end - start > max_request) break;
            end = next_end;
        }
        buffer.resize(static_cast<std::size_t>(end - start));
        read_at(start, buffer.data(), buffer.size());
        for (std::size_t k = first; k < last; ++k) {
            const Slot& s = index_[order[k]];
            const auto* at = buffer.data() + (s.offset - start);
            out(order[k], std::vector<std::uint8_t>(at, at + s.size));
        }
        first = last;
    }
}

}  // namespace crusader
