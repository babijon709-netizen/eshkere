#include "mock_media.h"
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaMuxer.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace {
constexpr char kMagic[] = "BWMOCK01";
bool fail(const char* name) {
    const char* value = std::getenv("MOCK_MEDIA_FAILURE");
    return value && std::strcmp(value, name) == 0;
}
bool write_all(int fd, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    while (size) {
        const ssize_t n = write(fd, bytes, size);
        if (n <= 0) return false;
        bytes += n; size -= static_cast<size_t>(n);
    }
    return true;
}
bool read_all(int fd, void* data, size_t size) {
    auto* bytes = static_cast<uint8_t*>(data);
    while (size) {
        const ssize_t n = read(fd, bytes, size);
        if (n <= 0) return false;
        bytes += n; size -= static_cast<size_t>(n);
    }
    return true;
}
template <typename T> bool put(int fd, const T& value) { return write_all(fd, &value, sizeof(value)); }
template <typename T> bool get(int fd, T& value) { return read_all(fd, &value, sizeof(value)); }
bool write_video_fd(int fd, const MockVideo& video) {
    if (lseek(fd, 0, SEEK_SET) < 0 || ftruncate(fd, 0) != 0) return false;
    const uint32_t mime = video.mime == "video/avc" ? 1 : video.mime == "video/hevc" ? 2 : 3;
    const uint32_t count = static_cast<uint32_t>(video.frames.size());
    if (!write_all(fd, kMagic, 8) || !put(fd, mime) || !put(fd, video.width) ||
        !put(fd, video.height) || !put(fd, video.rotation) || !put(fd, count)) return false;
    for (const auto& csd : video.csd) {
        const uint32_t length = static_cast<uint32_t>(csd.size());
        if (!put(fd, length) || !write_all(fd, csd.data(), csd.size())) return false;
    }
    for (const auto& frame : video.frames) {
        const uint32_t length = static_cast<uint32_t>(frame.data.size());
        if (!put(fd, frame.pts) || !put(fd, frame.flags) || !put(fd, length) ||
            !write_all(fd, frame.data.data(), frame.data.size())) return false;
    }
    return true;
}
bool read_video_fd(int fd, MockVideo& video) {
    if (lseek(fd, 0, SEEK_SET) < 0) return false;
    char magic[8]; uint32_t mime = 0, count = 0;
    if (!read_all(fd, magic, sizeof(magic)) || std::memcmp(magic, kMagic, 8) != 0 ||
        !get(fd, mime) || !get(fd, video.width) || !get(fd, video.height) ||
        !get(fd, video.rotation) || !get(fd, count) || count > 10000) return false;
    video.mime = mime == 1 ? "video/avc" : mime == 2 ? "video/hevc" : "video/other";
    for (auto& csd : video.csd) {
        uint32_t length = 0;
        if (!get(fd, length) || length > 2 * 1024 * 1024) return false;
        csd.resize(length);
        if (!read_all(fd, csd.data(), csd.size())) return false;
    }
    video.frames.clear();
    for (uint32_t i = 0; i < count; ++i) {
        MockFrame frame; uint32_t length = 0;
        if (!get(fd, frame.pts) || !get(fd, frame.flags) || !get(fd, length) || length > 64 * 1024 * 1024)
            return false;
        frame.data.resize(length);
        if (!read_all(fd, frame.data.data(), frame.data.size())) return false;
        video.frames.push_back(std::move(frame));
    }
    return true;
}
} // namespace

bool write_mock_video(const std::string& path, const MockVideo& video) {
    const int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) return false;
    const bool ok = write_video_fd(fd, video);
    close(fd); return ok;
}
bool read_mock_video(const std::string& path, MockVideo& video) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const bool ok = read_video_fd(fd, video);
    close(fd); return ok;
}

struct AMediaFormat { MockVideo video; };
struct AMediaExtractor { MockVideo video; size_t position = 0; bool selected = false; bool valid = false; };
struct AMediaMuxer { int fd = -1; MockVideo video; bool started = false; };

extern "C" {
media_status_t AMediaFormat_delete(AMediaFormat* f) { delete f; return AMEDIA_OK; }
bool AMediaFormat_getInt32(AMediaFormat* f, const char* key, int32_t* value) {
    if (!std::strcmp(key, "width")) *value = f->video.width;
    else if (!std::strcmp(key, "height")) *value = f->video.height;
    else if (!std::strcmp(key, "rotation-degrees")) *value = f->video.rotation;
    else return false;
    return true;
}
bool AMediaFormat_getString(AMediaFormat* f, const char* key, const char** value) {
    if (std::strcmp(key, "mime")) return false;
    *value = f->video.mime.c_str(); return true;
}
bool AMediaFormat_getBuffer(AMediaFormat* f, const char* key, void** data, size_t* size) {
    if (std::strlen(key) != 5 || std::strncmp(key, "csd-", 4) || key[4] < '0' || key[4] > '2') return false;
    auto& csd = f->video.csd[key[4] - '0'];
    if (csd.empty()) return false;
    *data = csd.data(); *size = csd.size(); return true;
}
AMediaExtractor* AMediaExtractor_new() { return fail("extractor") ? nullptr : new AMediaExtractor; }
media_status_t AMediaExtractor_delete(AMediaExtractor* e) { delete e; return AMEDIA_OK; }
media_status_t AMediaExtractor_setDataSourceFd(AMediaExtractor* e, int fd, off64_t offset, off64_t) {
    if (offset != 0 || !read_video_fd(fd, e->video)) return AMEDIA_ERROR_UNKNOWN;
    e->valid = true; return AMEDIA_OK;
}
size_t AMediaExtractor_getTrackCount(AMediaExtractor* e) { return e->valid ? 1 : 0; }
AMediaFormat* AMediaExtractor_getTrackFormat(AMediaExtractor* e, size_t track) {
    if (!e->valid || track != 0 || fail("format")) return nullptr;
    auto* f = new AMediaFormat; f->video = e->video; return f;
}
media_status_t AMediaExtractor_selectTrack(AMediaExtractor* e, size_t track) {
    if (track != 0 || fail("select")) return AMEDIA_ERROR_UNKNOWN;
    e->selected = true; e->position = 0; return AMEDIA_OK;
}
ssize_t AMediaExtractor_readSampleData(AMediaExtractor* e, uint8_t* data, size_t capacity) {
    if (fail("read") || !e->selected || e->position >= e->video.frames.size()) return -1;
    const auto& frame = e->video.frames[e->position];
    if (capacity < frame.data.size()) return -1;
    std::memcpy(data, frame.data.data(), frame.data.size());
    return static_cast<ssize_t>(frame.data.size());
}
uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor* e) {
    return e->position < e->video.frames.size() ? e->video.frames[e->position].flags : std::numeric_limits<uint32_t>::max();
}
int AMediaExtractor_getSampleTrackIndex(AMediaExtractor* e) {
    return e->selected && e->position < e->video.frames.size() ? 0 : -1;
}
int64_t AMediaExtractor_getSampleTime(AMediaExtractor* e) {
    return e->position < e->video.frames.size() ? e->video.frames[e->position].pts : -1;
}
bool AMediaExtractor_advance(AMediaExtractor* e) { return ++e->position < e->video.frames.size(); }
AMediaMuxer* AMediaMuxer_new(int fd, OutputFormat) {
    if (fail("muxer")) return nullptr;
    auto* muxer = new AMediaMuxer; muxer->fd = fd; return muxer;
}
media_status_t AMediaMuxer_delete(AMediaMuxer* m) { delete m; return AMEDIA_OK; }
media_status_t AMediaMuxer_setOrientationHint(AMediaMuxer* m, int rotation) {
    if (fail("orientation")) return AMEDIA_ERROR_UNKNOWN;
    m->video.rotation = rotation; return AMEDIA_OK;
}
ssize_t AMediaMuxer_addTrack(AMediaMuxer* m, const AMediaFormat* f) {
    if (fail("add")) return -1;
    m->video = f->video; m->video.frames.clear(); return 0;
}
media_status_t AMediaMuxer_start(AMediaMuxer* m) {
    if (fail("start")) return AMEDIA_ERROR_UNKNOWN;
    m->started = true; return AMEDIA_OK;
}
media_status_t AMediaMuxer_stop(AMediaMuxer* m) {
    if (!m->started || fail("stop")) return AMEDIA_ERROR_UNKNOWN;
    m->started = false;
    if (fail("corrupt") && !m->video.frames.empty() && !m->video.frames.back().data.empty())
        m->video.frames.back().data.back() ^= 0xff;
    if (fail("drop") && !m->video.frames.empty()) m->video.frames.pop_back();
    return write_video_fd(m->fd, m->video) ? AMEDIA_OK : AMEDIA_ERROR_UNKNOWN;
}
media_status_t AMediaMuxer_writeSampleData(AMediaMuxer* m, size_t track, const uint8_t* data,
                                         const AMediaCodecBufferInfo* info) {
    if (!m->started || track != 0 || info->offset < 0 || info->size <= 0 || fail("write"))
        return AMEDIA_ERROR_UNKNOWN;
    MockFrame frame;
    frame.pts = info->presentationTimeUs; frame.flags = info->flags;
    frame.data.assign(data + info->offset, data + info->offset + info->size);
    m->video.frames.push_back(std::move(frame));
    return AMEDIA_OK;
}
} // extern "C"
