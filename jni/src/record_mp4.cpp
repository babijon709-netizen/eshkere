#include "record_mp4.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace record_mp4 {
namespace {
constexpr uint32_t kTimescale = 1000; // 1 ms resolution, 64-bit movie/fragment times.
constexpr size_t kIndexReserve = 1024 * 1024;
constexpr size_t kMaxFrame = 64 * 1024 * 1024;
constexpr size_t kMaxFragmentBytes = 16 * 1024 * 1024;
constexpr uint64_t kFragmentTargetMs = 2000;
constexpr uint64_t kFragmentMaxMs = 10000;
constexpr uint64_t kIndexTargetMs = 10000;
#ifdef RECORD_MP4_TEST_INDEX_ENTRIES
constexpr size_t kIndexEntryLimit = RECORD_MP4_TEST_INDEX_ENTRIES;
static_assert(kIndexEntryLimit >= 2 && kIndexEntryLimit <= 65535, "invalid test index limit");
#else
constexpr size_t kIndexEntryLimit = 65535;
#endif

struct Bytes {
    std::vector<uint8_t> data;
    size_t begin(const char* type) { const size_t at = data.size(); u32(0); raw(type, 4); return at; }
    void end(size_t at) { patch32(at, static_cast<uint32_t>(data.size() - at)); }
    void raw(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        if (n) data.insert(data.end(), b, b + n);
    }
    void zeros(size_t n) { data.insert(data.end(), n, 0); }
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u8(v >> 24); u8(v >> 16); u8(v >> 8); u8(v); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v >> 32)); u32(static_cast<uint32_t>(v)); }
    void full(uint8_t version, uint32_t flags) { u8(version); u8(flags >> 16); u8(flags >> 8); u8(flags); }
    void patch32(size_t at, uint32_t v) {
        data[at] = v >> 24; data[at + 1] = v >> 16; data[at + 2] = v >> 8; data[at + 3] = v;
    }
};

bool write_all(int fd, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (size) {
        const ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; size -= static_cast<size_t>(n);
    }
    return true;
}
bool pwrite_all(int fd, const void* data, size_t size, off_t offset) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (size) {
        const ssize_t n = pwrite(fd, p, size, offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; size -= static_cast<size_t>(n); offset += n;
    }
    return true;
}
bool write_u64_at(int fd, off_t offset, uint64_t value) {
    uint8_t bytes[8];
    for (int i = 7; i >= 0; --i) { bytes[i] = static_cast<uint8_t>(value); value >>= 8; }
    return pwrite_all(fd, bytes, sizeof(bytes), offset);
}
void matrix(Bytes& b) {
    b.u32(0x00010000); b.u32(0); b.u32(0);
    b.u32(0); b.u32(0x00010000); b.u32(0);
    b.u32(0); b.u32(0); b.u32(0x40000000);
}

// Annex B units start with 00 00 01 or 00 00 00 01. Encoders normally use
// four-byte start codes, but the three-byte form is also legal.
size_t start_code(const uint8_t* p, size_t n, size_t from, size_t& prefix) {
    if (!p || n < 3) return n;
    for (size_t i = from; i + 3 <= n; ++i) {
        if (p[i] != 0 || p[i + 1] != 0) continue;
        if (p[i + 2] == 1) { prefix = 3; return i; }
        if (i + 4 <= n && p[i + 2] == 0 && p[i + 3] == 1) { prefix = 4; return i; }
    }
    return n;
}

struct Unit { const uint8_t* data; size_t size; };
bool annex_units(const uint8_t* p, size_t n, std::vector<Unit>& units) {
    size_t prefix = 0;
    size_t pos = start_code(p, n, 0, prefix);
    if (pos == n) return false;
    for (size_t i = 0; i < pos; ++i) if (p[i] != 0) return false;
    while (pos < n) {
        const size_t start = pos + prefix;
        size_t next_prefix = 0;
        const size_t next = start_code(p, n, start, next_prefix);
        if (next <= start) return false;
        units.push_back({p + start, next - start});
        pos = next; prefix = next_prefix;
    }
    return !units.empty();
}

bool sample_to_avcc(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    if (!data || size == 0 || size > kMaxFrame) return false;
    // Validate the complete AVCC framing first: a length such as 0x00000104
    // has the same prefix as a three-byte Annex B start code.
    size_t pos = 0;
    bool length_prefixed = true;
    while (pos < size) {
        if (size - pos < 4) { length_prefixed = false; break; }
        const uint32_t len = (uint32_t(data[pos]) << 24) | (uint32_t(data[pos + 1]) << 16) |
                             (uint32_t(data[pos + 2]) << 8) | data[pos + 3];
        pos += 4;
        if (!len || len > size - pos) { length_prefixed = false; break; }
        pos += len;
    }
    if (length_prefixed) { out.assign(data, data + size); return true; }
    std::vector<Unit> units;
    if (!annex_units(data, size, units)) return false;
    Bytes b;
    for (const Unit& unit : units) {
        if (!unit.size || unit.size > UINT32_MAX) return false;
        b.u32(static_cast<uint32_t>(unit.size)); b.raw(unit.data, unit.size);
    }
    if (b.data.size() > kMaxFrame) return false;
    out = std::move(b.data);
    return true;
}

bool make_avcc(const uint8_t* csd0, size_t size0, const uint8_t* csd1, size_t size1,
               std::vector<uint8_t>& out) {
    if (!csd0 || !size0 || size0 > 1024 * 1024 || size1 > 1024 * 1024 || (size1 && !csd1)) return false;
    // A codec may supply an AVCDecoderConfigurationRecord instead of Annex B.
    if (csd0[0] == 1 && size0 >= 7) {
        size_t pos = 6; const unsigned sps_count = csd0[5] & 31;
        if (!sps_count) return false;
        for (unsigned i = 0; i < sps_count; ++i) {
            if (pos + 2 > size0) return false;
            size_t len = (size_t(csd0[pos]) << 8) | csd0[pos + 1]; pos += 2;
            if (!len || len > size0 - pos) return false;
            pos += len;
        }
        if (pos >= size0 || !csd0[pos]) return false;
        const unsigned pps_count = csd0[pos++];
        for (unsigned i = 0; i < pps_count; ++i) {
            if (pos + 2 > size0) return false;
            size_t len = (size_t(csd0[pos]) << 8) | csd0[pos + 1]; pos += 2;
            if (!len || len > size0 - pos) return false;
            pos += len;
        }
        if ((csd0[4] & 3) != 3) return false; // output uses four-byte lengths.
        out.assign(csd0, csd0 + size0);
        return true;
    }
    std::vector<Unit> units;
    if (!annex_units(csd0, size0, units)) return false;
    if (size1 && !annex_units(csd1, size1, units)) return false;
    std::vector<Unit> sps, pps;
    for (const Unit& unit : units) {
        if (!unit.size || unit.size > UINT16_MAX) return false;
        if ((unit.data[0] & 31) == 7) sps.push_back(unit);
        if ((unit.data[0] & 31) == 8) pps.push_back(unit);
    }
    if (sps.empty() || sps.size() > 31 || pps.empty() || pps.size() > 255 || sps[0].size < 4) return false;
    Bytes b;
    b.u8(1); b.u8(sps[0].data[1]); b.u8(sps[0].data[2]); b.u8(sps[0].data[3]);
    b.u8(0xff); b.u8(static_cast<uint8_t>(0xe0 | sps.size()));
    for (const Unit& unit : sps) { b.u16(static_cast<uint16_t>(unit.size)); b.raw(unit.data, unit.size); }
    b.u8(static_cast<uint8_t>(pps.size()));
    for (const Unit& unit : pps) { b.u16(static_cast<uint16_t>(unit.size)); b.raw(unit.data, unit.size); }
    out = std::move(b.data);
    return true;
}
} // namespace

struct Writer::Impl {
    struct Sample { uint32_t size, duration; bool key; };
    struct IndexEntry { uint32_t size, duration; };
    int fd;
    bool started = false, finished = false, index_valid = true;
    std::string why;
    off_t movie_duration = 0, track_duration = 0, media_duration = 0, fragment_duration = 0, index_offset = 0;
    int64_t base_us = -1, last_us = -1;
    uint64_t pending_tick = 0, fragment_start_tick = 0, committed_end_tick = 0, committed = 0;
    uint32_t last_step = 33, sequence = 1;
    uint64_t index_target_ms = kIndexTargetMs;
    bool pending_key = false;
    std::vector<uint8_t> pending, payload;
    std::vector<Sample> samples;
    std::vector<IndexEntry> index;

    explicit Impl(int file) : fd(file) {}
    bool fail(const char* message) { if (why.empty()) why = message; return false; }

    bool init(int width, int height, const uint8_t* csd0, size_t size0, const uint8_t* csd1, size_t size1) {
        if (started || fd < 0 || width <= 0 || height <= 0 || width > 65535 || height > 65535)
            return fail("invalid MP4 dimensions or state");
        struct stat st{};
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 0 || lseek(fd, 0, SEEK_SET) < 0)
            return fail("output is not the reserved empty file");
        std::vector<uint8_t> avcc;
        if (!make_avcc(csd0, size0, csd1, size1, avcc)) return fail("invalid H.264 codec configuration");
        Bytes b;
        auto ftyp = b.begin("ftyp"); b.raw("isom", 4); b.u32(0x200); b.raw("isomiso6mp41avc1", 16); b.end(ftyp);
        auto moov = b.begin("moov");
        auto mvhd = b.begin("mvhd"); b.full(1, 0); b.u64(0); b.u64(0); b.u32(kTimescale);
        movie_duration = static_cast<off_t>(b.data.size()); b.u64(0); b.u32(0x10000); b.u16(0x100); b.zeros(10);
        matrix(b); b.zeros(24); b.u32(2); b.end(mvhd);
        auto trak = b.begin("trak");
        auto tkhd = b.begin("tkhd"); b.full(1, 7); b.u64(0); b.u64(0); b.u32(1); b.u32(0);
        track_duration = static_cast<off_t>(b.data.size()); b.u64(0); b.zeros(8); b.u16(0); b.u16(0); b.u16(0); b.u16(0);
        matrix(b); b.u32(static_cast<uint32_t>(width) << 16); b.u32(static_cast<uint32_t>(height) << 16); b.end(tkhd);
        auto mdia = b.begin("mdia");
        auto mdhd = b.begin("mdhd"); b.full(1, 0); b.u64(0); b.u64(0); b.u32(kTimescale);
        media_duration = static_cast<off_t>(b.data.size()); b.u64(0); b.u16(0x55c4); b.u16(0); b.end(mdhd);
        auto hdlr = b.begin("hdlr"); b.full(0, 0); b.u32(0); b.raw("vide", 4); b.zeros(12); b.raw("VideoHandler", 13); b.end(hdlr);
        auto minf = b.begin("minf");
        auto vmhd = b.begin("vmhd"); b.full(0, 1); b.u16(0); b.zeros(6); b.end(vmhd);
        auto dinf = b.begin("dinf"); auto dref = b.begin("dref"); b.full(0, 0); b.u32(1);
        auto url = b.begin("url "); b.full(0, 1); b.end(url); b.end(dref); b.end(dinf);
        auto stbl = b.begin("stbl"); auto stsd = b.begin("stsd"); b.full(0, 0); b.u32(1);
        auto avc1 = b.begin("avc1"); b.zeros(6); b.u16(1); b.u16(0); b.u16(0); b.zeros(12);
        b.u16(static_cast<uint16_t>(width)); b.u16(static_cast<uint16_t>(height));
        b.u32(0x480000); b.u32(0x480000); b.u32(0); b.u16(1); b.zeros(32); b.u16(24); b.u16(0xffff);
        auto config = b.begin("avcC"); b.raw(avcc.data(), avcc.size()); b.end(config); b.end(avc1); b.end(stsd);
        auto stts = b.begin("stts"); b.full(0, 0); b.u32(0); b.end(stts);
        auto stsc = b.begin("stsc"); b.full(0, 0); b.u32(0); b.end(stsc);
        auto stsz = b.begin("stsz"); b.full(0, 0); b.u32(0); b.u32(0); b.end(stsz);
        auto stco = b.begin("stco"); b.full(0, 0); b.u32(0); b.end(stco);
        b.end(stbl); b.end(minf); b.end(mdia); b.end(trak);
        auto mvex = b.begin("mvex");
        auto mehd = b.begin("mehd"); b.full(1, 0); fragment_duration = static_cast<off_t>(b.data.size()); b.u64(0); b.end(mehd);
        auto trex = b.begin("trex"); b.full(0, 0); b.u32(1); b.u32(1); b.u32(0); b.u32(0); b.u32(0); b.end(trex);
        b.end(mvex); b.end(moov);
        index_offset = static_cast<off_t>(b.data.size());
        auto free = b.begin("free"); b.zeros(kIndexReserve - 8); b.end(free);
        if (!write_all(fd, b.data.data(), b.data.size())) return fail("cannot write MP4 header");
        started = true;
        return true;
    }

    bool duration_header() {
        return write_u64_at(fd, movie_duration, committed_end_tick) &&
               write_u64_at(fd, track_duration, committed_end_tick) &&
               write_u64_at(fd, media_duration, committed_end_tick) &&
               write_u64_at(fd, fragment_duration, committed_end_tick);
    }

    bool compact_index() {
        // sidx has a 16-bit entry count. Grow seek intervals instead of
        // stopping a long recording when the initial intervals fill it.
        std::vector<IndexEntry> compact;
        compact.reserve(index.size());
        for (size_t i = 0; i < index.size();) {
            IndexEntry entry = index[i++];
            if (i < index.size() && index[i].size <= 0x7fffffffU - entry.size &&
                index[i].duration <= UINT32_MAX - entry.duration) {
                entry.size += index[i].size;
                entry.duration += index[i].duration;
                ++i;
            }
            compact.push_back(entry);
        }
        if (compact.size() >= index.size()) return false;
        index.swap(compact);
        index_target_ms = std::min<uint64_t>(index_target_ms * 2, UINT32_MAX);
        return true;
    }

    void add_index(uint32_t bytes, uint32_t duration, bool key) {
        if (!index_valid) return;
        if (index.empty()) {
            if (!key) { index_valid = false; return; }
            index.push_back({bytes, duration});
            return;
        }
        const IndexEntry& last = index.back();
        const bool overflow = bytes > 0x7fffffffU - last.size || duration > UINT32_MAX - last.duration;
        if ((key && last.duration >= index_target_ms) || overflow) {
            if (!key || (index.size() >= kIndexEntryLimit && !compact_index())) {
                index.clear(); index_valid = false; return; // Sequential playback remains possible.
            }
            index.push_back({bytes, duration});
        } else {
            index.back().size += bytes; index.back().duration += duration;
        }
    }

    bool flush_fragment() {
        if (samples.empty()) return true;
        const off_t offset = lseek(fd, 0, SEEK_CUR);
        if (offset < 0) return fail("cannot seek MP4 output");
        Bytes b;
        auto moof = b.begin("moof");
        auto mfhd = b.begin("mfhd"); b.full(0, 0); b.u32(sequence); b.end(mfhd);
        auto traf = b.begin("traf");
        auto tfhd = b.begin("tfhd"); b.full(0, 0x020000); b.u32(1); b.end(tfhd);
        auto tfdt = b.begin("tfdt"); b.full(1, 0); b.u64(fragment_start_tick); b.end(tfdt);
        auto trun = b.begin("trun"); b.full(0, 0x000701); b.u32(static_cast<uint32_t>(samples.size()));
        const size_t data_offset = b.data.size(); b.u32(0);
        uint64_t duration = 0;
        for (const Sample& sample : samples) {
            b.u32(sample.duration); b.u32(sample.size);
            b.u32(sample.key ? 0x02000000 : 0x01010000);
            duration += sample.duration;
        }
        b.end(trun); b.end(traf); b.end(moof);
        if (b.data.size() > INT32_MAX - 8 || payload.size() > UINT32_MAX - 8 || duration > UINT32_MAX)
            return fail("MP4 fragment is too large");
        b.patch32(data_offset, static_cast<uint32_t>(b.data.size() + 8));
        const uint32_t mdat_size = static_cast<uint32_t>(payload.size() + 8);
        uint8_t mdat[] = {uint8_t(mdat_size >> 24), uint8_t(mdat_size >> 16), uint8_t(mdat_size >> 8),
                          uint8_t(mdat_size), 'm', 'd', 'a', 't'};
        const uint64_t total = b.data.size() + sizeof(mdat) + payload.size();
        if (total > 0x7fffffffU) return fail("MP4 fragment exceeds index size");
        if (!write_all(fd, b.data.data(), b.data.size()) || !write_all(fd, mdat, sizeof(mdat)) ||
            !write_all(fd, payload.data(), payload.size())) {
            // An incomplete trailing fragment must not make prior fragments unusable.
            if (ftruncate(fd, offset) != 0) return fail("cannot truncate incomplete MP4 fragment");
            (void)lseek(fd, offset, SEEK_SET);
            return fail("cannot write MP4 fragment");
        }
        add_index(static_cast<uint32_t>(total), static_cast<uint32_t>(duration), samples.front().key);
        committed += samples.size(); committed_end_tick = fragment_start_tick + duration;
        ++sequence; // Wrap is permitted by ISO BMFF; no time limit is imposed here.
        samples.clear(); payload.clear(); fragment_start_tick = committed_end_tick;
        if (!duration_header()) return fail("cannot update MP4 duration");
        return true;
    }

    bool append_pending(uint32_t duration) {
        if (pending.empty() || !duration) return fail("invalid frame duration");
        if (samples.empty()) fragment_start_tick = pending_tick;
        if (pending.size() > UINT32_MAX || payload.size() > SIZE_MAX - pending.size()) return fail("frame too large");
        samples.push_back({static_cast<uint32_t>(pending.size()), duration, pending_key});
        payload.insert(payload.end(), pending.begin(), pending.end());
        pending.clear();
        return true;
    }

    bool push(const uint8_t* data, size_t size, int64_t pts, bool key) {
        if (!started || finished || !why.empty() || pts < 0) return fail("invalid recording state or timestamp");
        if (last_us >= 0 && pts <= last_us) return fail("encoder reordered or repeated timestamps");
        std::vector<uint8_t> converted;
        if (!sample_to_avcc(data, size, converted)) return fail("invalid H.264 frame");
        if (base_us < 0) { if (!key) return fail("recording did not start with a key frame"); base_us = pts; }
        const uint64_t relative = static_cast<uint64_t>(pts - base_us);
        const uint64_t tick = relative / 1000 + ((relative % 1000) >= 500 ? 1 : 0);
        if (!pending.empty()) {
            if (tick <= pending_tick || tick - pending_tick > UINT32_MAX) return fail("unsupported timestamp gap");
            last_step = static_cast<uint32_t>(tick - pending_tick);
            if (!append_pending(last_step)) return false;
            const uint64_t fragment_ms = tick - fragment_start_tick;
            if ((key && fragment_ms >= kFragmentTargetMs) || fragment_ms >= kFragmentMaxMs ||
                payload.size() >= kMaxFragmentBytes) {
                if (!flush_fragment()) return false;
            }
        }
        pending_tick = tick; pending_key = key; pending = std::move(converted); last_us = pts;
        return true;
    }

    bool finalize_index() {
        if (!index_valid || index.empty()) return true; // Valid sequential MP4 without a seek index.
        Bytes b;
        auto sidx = b.begin("sidx"); b.full(1, 0); b.u32(1); b.u32(kTimescale); b.u64(0);
        const size_t first_offset = b.data.size(); b.u64(0); b.u16(0); b.u16(static_cast<uint16_t>(index.size()));
        for (const IndexEntry& entry : index) { b.u32(entry.size); b.u32(entry.duration); b.u32(0x90000000); }
        b.end(sidx);
        if (b.data.size() + 8 > kIndexReserve) return fail("MP4 seek index exceeds reserved area");
        const uint64_t gap = kIndexReserve - b.data.size();
        const size_t high = first_offset;
        b.patch32(high, static_cast<uint32_t>(gap >> 32)); b.patch32(high + 4, static_cast<uint32_t>(gap));
        uint8_t free_header[] = {uint8_t(gap >> 24), uint8_t(gap >> 16), uint8_t(gap >> 8), uint8_t(gap), 'f', 'r', 'e', 'e'};
        // Keep the original valid free box until the complete index and the
        // trailing free header have been written; publish its 8-byte header last.
        if (!pwrite_all(fd, b.data.data() + 8, b.data.size() - 8, index_offset + 8) ||
            !pwrite_all(fd, free_header, sizeof(free_header), index_offset + b.data.size()) ||
            !pwrite_all(fd, b.data.data(), 8, index_offset)) return fail("cannot write MP4 seek index");
        return true;
    }

    bool finish() {
        if (finished) return why.empty() && committed > 0;
        finished = true;
        if (!started) return fail("MP4 output was not started");
        bool okay = why.empty();
        if (okay && !pending.empty()) {
            // Do not duplicate a long idle gap as the final frame duration.
            if (!append_pending(std::min(last_step, 1000U))) okay = false;
        }
        if (!samples.empty() && !flush_fragment()) okay = false;
        if (!duration_header()) { fail("cannot update MP4 duration"); okay = false; }
        if (!finalize_index()) okay = false;
        if (fsync(fd) != 0) { fail("cannot flush MP4 output"); okay = false; }
        if (!committed) { fail("no complete video frames"); okay = false; }
        return okay && why.empty();
    }
};

Writer::Writer(int fd) : impl_(new Impl(fd)) {}
Writer::~Writer() = default;
bool Writer::start(int w, int h, const uint8_t* a, size_t an, const uint8_t* b, size_t bn) {
    return impl_->init(w, h, a, an, b, bn);
}
bool Writer::sample(const uint8_t* data, size_t size, int64_t pts, bool key) {
    return impl_->push(data, size, pts, key);
}
bool Writer::finish() { return impl_->finish(); }
uint64_t Writer::committed_frames() const { return impl_->committed; }
const char* Writer::error() const { return impl_->why.c_str(); }
} // namespace record_mp4
