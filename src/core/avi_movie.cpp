#include "core/avi_movie.hpp"

#include "core/bytes.hpp"

#include <algorithm>
#include <stdexcept>

namespace crusader {

namespace {

// DS:69C2 and DS:69B2: length of run code c, and the extra bits after it.
// Codes 3, 7, 11 and 15 do not occur in the movies.
constexpr std::array<std::uint32_t, 16> kRunBase = {0, 128, 8, 0, 2, 0, 32, 0, 1, 256, 16, 0, 4, 0, 64, 0};
constexpr std::array<int, 16> kRunExtraBits = {0, 7, 3, 0, 1, 16, 5, 0, 1, 8, 4, 0, 2, 24, 6, 0};

// FUN_1460_0000: the next `n` bits, most significant first.
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> d) : d_(d) {}

    // @orig 1460:0000
    std::uint32_t get(int n) {
        std::uint32_t window = 0;
        const std::size_t byte = pos_ >> 3;
        for (std::size_t i = 0; i < 4; ++i) {
            window = window << 8 | (byte + i < d_.size() ? d_[byte + i] : 0);
        }
        window <<= pos_ & 7;
        pos_ += static_cast<std::size_t>(n);
        return n == 0 ? 0 : window >> (32 - n);
    }

private:
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
};

struct RiffChunk {
    std::uint32_t id;
    std::size_t data;
    std::uint32_t size;
};

// @orig 1468:2188
std::vector<RiffChunk> riff_chunks(std::span<const std::uint8_t> d, std::size_t from, std::size_t to) {
    std::vector<RiffChunk> out;
    std::size_t p = from;
    while (p + 8 <= to) {
        const std::uint32_t id = read_u32(d, p);
        const std::uint32_t size = read_u32(d, p + 4);
        if (size > to - p - 8) break;
        out.push_back({id, p + 8, size});
        p += 8 + size + (size & 1);
    }
    return out;
}

bool is_list(std::span<const std::uint8_t> d, const RiffChunk& c, std::uint32_t type) {
    return c.id == fourcc("LIST") && c.size >= 4 && read_u32(d, c.data) == type;
}

}  // namespace

// @orig none: the player reads the file as it plays (1468:17B0); the port
// keeps the file open and reads each chunk when it is needed
AviMovie AviMovie::load(const std::filesystem::path& path, bool use_index) {
    AviMovie m;
    m.file_.reset(std::fopen(path.string().c_str(), "rb"));
    if (!m.file_ || std::fseek(m.file_.get(), 0, SEEK_END) != 0) throw std::runtime_error("cannot open " + path.string());
    const long size = std::ftell(m.file_.get());
    if (size < 12) throw std::runtime_error("not an AVI file");
    m.file_size_ = static_cast<std::size_t>(size);
    std::array<std::uint8_t, 12> riff{};
    m.read_at(0, riff);
    if (read_u32(riff, 0) != fourcc("RIFF") || read_u32(riff, 8) != fourcc("AVI ")) {
        throw std::runtime_error("not an AVI file");
    }

    // 1468:2188: the top-level chunks, as riff_chunks walks them.
    bool video = false;
    std::size_t movi = 0, movi_end = 0, idx1 = 0;
    std::uint32_t idx1_size = 0;
    std::vector<std::uint8_t> buffer;
    for (std::size_t p = 12; p + 8 <= m.file_size_;) {
        std::array<std::uint8_t, 12> head{};
        const std::size_t head_size = std::min<std::size_t>(12, m.file_size_ - p);
        m.read_at(p, std::span(head).first(head_size));
        const std::uint32_t id = read_u32(head, 0);
        const std::uint32_t chunk_size = read_u32(head, 4);
        if (chunk_size > m.file_size_ - p - 8) break;
        const std::size_t data = p + 8;
        if (id == fourcc("LIST") && chunk_size >= 4 && head_size == 12) {
            const std::uint32_t type = read_u32(head, 8);
            if (type == fourcc("hdrl")) {
                buffer.resize(chunk_size);
                m.read_at(data, buffer);
                video = m.parse_header(buffer) || video;
            } else if (type == fourcc("movi")) {
                movi = data;
                movi_end = data + chunk_size;
            }
        } else if (id == fourcc("idx1")) {
            idx1 = data;
            idx1_size = chunk_size;
        }
        p = data + chunk_size + (chunk_size & 1);
    }
    if (!video || m.width_ <= 0 || m.height_ <= 0) throw std::runtime_error("AVI file without a video stream");
    if (movi != 0 && !(use_index && idx1 != 0 && m.chunks_from_index(movi, movi_end, idx1, idx1_size))) {
        m.chunks_from_walk(movi + 4, movi_end);
    }
    return m;
}

// @orig 1468:2188: the "hdrl" list (its data, from the "hdrl" type on).
// True when it has a video format.
bool AviMovie::parse_header(std::span<const std::uint8_t> d) {
    bool video = false;
    for (const RiffChunk& c : riff_chunks(d, 4, d.size())) {
        if (c.id == fourcc("avih")) {
            width_ = static_cast<int>(read_u32(d, c.data + 32));
            height_ = static_cast<int>(read_u32(d, c.data + 36));
        }
        if (!is_list(d, c, fourcc("strl"))) continue;
        std::uint32_t type = 0;
        for (const RiffChunk& s : riff_chunks(d, c.data + 4, c.data + c.size)) {
            if (s.id == fourcc("strh")) {
                type = read_u32(d, s.data);
                if (type == fourcc("vids")) {
                    codec_ = read_u32(d, s.data + 4);
                    frame_count_ = read_u32(d, s.data + 32);
                } else if (type == fourcc("auds")) {
                    has_audio_ = true;
                    audio_initial_frames_ = read_u32(d, s.data + 16);
                }
            } else if (s.id == fourcc("strf") && type == fourcc("vids")) {
                video = true;
                colours_ = static_cast<int>(read_u32(d, s.data + 36));
                for (int i = 0; i < 256 && 40u + 4u * i + 4u <= s.size; ++i) {
                    const std::size_t q = s.data + 40 + 4 * static_cast<std::size_t>(i);
                    colour_table_[i] = {d[q + 2], d[q + 1], d[q]};
                }
            }
        }
    }
    return video;
}

// @orig none: the "idx1" index, taken only when its entries follow one
// another from the start of the "movi" list to its end, i.e. when they are
// exactly the chunks a walk finds. Offsets are from the "movi" type, or from
// the file start in some writers.
bool AviMovie::chunks_from_index(std::size_t movi, std::size_t movi_end, std::size_t idx1, std::uint32_t size) {
    std::vector<std::uint8_t> index(size);
    read_at(idx1, index);
    std::vector<Chunk> chunks;
    chunks.reserve(size / 16);
    std::size_t base = 0;
    std::size_t next = movi + 4;  // where the next chunk's header must be
    for (std::size_t e = 0; e + 16 <= index.size(); e += 16) {
        const std::uint32_t id = read_u32(index, e);
        const std::size_t offset = read_u32(index, e + 8);
        const std::uint32_t chunk_size = read_u32(index, e + 12);
        if (e == 0) {
            if (offset == 4) base = movi;
            else if (offset != next) return false;
        }
        if (base + offset != next || next + 8 > movi_end || chunk_size > movi_end - next - 8) return false;
        chunks.push_back({id, next + 8, chunk_size});
        next += 8 + chunk_size + (chunk_size & 1);
    }
    if (next != movi_end && next != movi_end + 1) return false;
    chunks_ = std::move(chunks);
    return true;
}

// @orig 1468:2188: the chunk headers of the "movi" list, one after another.
void AviMovie::chunks_from_walk(std::size_t from, std::size_t to) {
    chunks_.clear();
    for (std::size_t p = from; p + 8 <= to;) {
        std::array<std::uint8_t, 8> head{};
        read_at(p, head);
        const std::uint32_t chunk_size = read_u32(head, 4);
        if (chunk_size > to - p - 8) break;
        chunks_.push_back({read_u32(head, 0), p + 8, chunk_size});
        p += 8 + chunk_size + (chunk_size & 1);
    }
}

void AviMovie::read_at(std::size_t offset, std::span<std::uint8_t> out) const {
    if (out.empty()) return;
    if (offset > file_size_ || out.size() > file_size_ - offset ||
        std::fseek(file_.get(), static_cast<long>(offset), SEEK_SET) != 0 ||
        std::fread(out.data(), 1, out.size(), file_.get()) != out.size()) {
        throw std::runtime_error("AVI file cut short");
    }
}

std::span<const std::uint8_t> AviMovie::read(const Chunk& c, std::vector<std::uint8_t>& out, std::uint32_t* id) const {
    if (id) {
        std::array<std::uint8_t, 8> head{};
        if (c.offset < 8) throw std::runtime_error("AVI chunk without a header");
        read_at(c.offset - 8, head);
        if (read_u32(head, 4) != c.size) throw std::runtime_error("AVI chunk header does not match the index");
        *id = read_u32(head, 0);
    }
    out.resize(c.size);
    read_at(c.offset, out);
    return out;
}

// @orig ?: strip decoder of VideoPlayer (segment 1468), not split yet
void decode_movie_strip(std::span<const std::uint8_t> chunk, int strip, int strips, bool half_width,
                        Surface8& frame) {
    const std::size_t start = read_u32(chunk, 4 * static_cast<std::size_t>(strip));
    const std::uint32_t codes_size = read_u32(chunk, start);
    if (codes_size == 0) return;
    BitReader codes(chunk.subspan(start + 4));
    std::size_t src = start + 4 + codes_size;

    const int rows = frame.height / strips;
    std::size_t out = static_cast<std::size_t>(strip) * rows * frame.width;
    const std::size_t end = out + static_cast<std::size_t>(rows) * frame.width;
    bool copy = true;  // switched before the first run, which skips
    std::uint32_t run = 0;
    while (out < end) {
        if (run == 0) {
            const std::uint32_t code = codes.get(4);
            run = kRunBase[code];
            if (code != 0 && code != 8) run += codes.get(kRunExtraBits[code]);
            if (half_width) run <<= 1;
            copy = !copy;
            continue;
        }
        const std::size_t n = std::min<std::size_t>(run, end - out);
        if (copy) {
            if (half_width) {
                check_range(chunk, src, (n + 1) >> 1);
                for (std::size_t k = 0; k < n; ++k) frame.pixels[out + k] = chunk[src + (k >> 1)];
                src += n >> 1;
            } else {
                check_range(chunk, src, n);
                std::copy_n(chunk.begin() + static_cast<std::ptrdiff_t>(src), n, frame.pixels.begin() + static_cast<std::ptrdiff_t>(out));
                src += n;
            }
        }
        out += n;
        run -= static_cast<std::uint32_t>(n);
    }
}

std::vector<std::int8_t> movie_audio_samples(std::span<const std::uint8_t> chunk) {
    std::vector<std::int8_t> out(chunk.size());
    for (std::size_t i = 0; i < chunk.size(); ++i) out[i] = static_cast<std::int8_t>(chunk[i] - 0x80);
    return out;
}

}  // namespace crusader
