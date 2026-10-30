#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace record_mp4 {
// One seekable fragmented MP4, written incrementally to an already-reserved
// empty regular file. Does not own the fd. A complete fragment is preserved
// if a later write or the recorder fails. H.264 input is Annex B or AVCC.
class Writer {
public:
    explicit Writer(int fd);
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool start(int width, int height, const uint8_t* csd0, size_t csd0_size,
               const uint8_t* csd1, size_t csd1_size);
    bool sample(const uint8_t* data, size_t size, int64_t presentation_us, bool keyframe);
    bool finish();
    uint64_t committed_frames() const;
    const char* error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace record_mp4
