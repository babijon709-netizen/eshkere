#include "capture_merge.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace capture_merge {
namespace {
constexpr size_t kDigits = 10;
constexpr size_t kMaxCsd = 1024 * 1024;
constexpr size_t kInitialSample = 256 * 1024;
constexpr size_t kMaxSample = 64 * 1024 * 1024;
constexpr int64_t kDefaultStepUs = 33333;

void message(const char* text) { std::fprintf(stderr, "capture merge: %s\n", text); }

struct PathPlan {
    std::string part;
    std::string output;
    std::string temporary;
    size_t digits = 0;
    uint64_t count = 0;
};

bool make_plan(const char* last, const char* output, PathPlan& plan) {
    if (!last || !output || last[0] != '/' || output[0] != '/') return false;
    plan.part = last;
    plan.output = output;
    const std::string suffix = ".mp4";
    if (plan.part.size() < kDigits + suffix.size() + 5 ||
        plan.part.compare(plan.part.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    plan.digits = plan.part.size() - suffix.size() - kDigits;
    if (plan.part.compare(plan.digits - 5, 5, "-part") != 0) return false;
    for (size_t i = 0; i < kDigits; ++i) {
        const char c = plan.part[plan.digits + i];
        if (c < '0' || c > '9') return false;
        plan.count = plan.count * 10 + static_cast<unsigned>(c - '0');
    }
    if (plan.count == 0 || plan.part.substr(0, plan.digits - 5) + suffix != plan.output)
        return false;
    plan.temporary = plan.output + ".merging";
    return true;
}

void set_part(PathPlan& plan, uint64_t number) {
    for (size_t i = kDigits; i > 0; --i) {
        plan.part[plan.digits + i - 1] = static_cast<char>('0' + number % 10);
        number /= 10;
    }
}

bool same_file(const struct stat& a, const struct stat& b) {
    return S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_dev == b.st_dev &&
           a.st_ino == b.st_ino && a.st_size == b.st_size && a.st_mtime == b.st_mtime;
}

bool placeholder(const std::string& path, struct stat& st) {
    return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0;
}

bool intact_placeholder(const std::string& path, const struct stat& original) {
    struct stat now{};
    return placeholder(path, now) && same_file(now, original);
}

struct Signature {
    std::string mime;
    int32_t width = 0, height = 0, rotation = 0;
    std::vector<uint8_t> csd[3];
};

bool signature(AMediaFormat* format, Signature& out) {
    const char* mime = nullptr;
    if (!AMediaFormat_getString(format, "mime", &mime) || !mime) return false;
    out.mime = mime;
    if (out.mime != "video/avc" && out.mime != "video/hevc") return false;
    if (!AMediaFormat_getInt32(format, "width", &out.width) ||
        !AMediaFormat_getInt32(format, "height", &out.height) ||
        out.width <= 0 || out.height <= 0) return false;
    (void)AMediaFormat_getInt32(format, "rotation-degrees", &out.rotation);
    if (out.rotation != 0 && out.rotation != 90 && out.rotation != 180 && out.rotation != 270)
        return false;
    const char* const keys[] = {"csd-0", "csd-1", "csd-2"};
    for (size_t i = 0; i < 3; ++i) {
        void* data = nullptr;
        size_t size = 0;
        if (!AMediaFormat_getBuffer(format, keys[i], &data, &size)) continue;
        if (!data || size == 0 || size > kMaxCsd) return false;
        const auto* bytes = static_cast<const uint8_t*>(data);
        out.csd[i].assign(bytes, bytes + size);
    }
    return !out.csd[0].empty();
}

bool compatible(const Signature& a, const Signature& b, bool compare_csd = true) {
    if (a.mime != b.mime || a.width != b.width || a.height != b.height || a.rotation != b.rotation)
        return false;
    if (compare_csd) for (size_t i = 0; i < 3; ++i)
        if (a.csd[i] != b.csd[i]) return false;
    return true;
}

struct Source {
    int fd = -1;
    AMediaExtractor* extractor = nullptr;
    AMediaFormat* format = nullptr;
    int track = -1;
    struct stat stat_at_open{};
    Signature sig;

    ~Source() {
        if (format) AMediaFormat_delete(format);
        if (extractor) AMediaExtractor_delete(extractor);
        if (fd >= 0) close(fd);
    }
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    bool open_file(const char* path) {
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0 || fstat(fd, &stat_at_open) != 0 ||
            !S_ISREG(stat_at_open.st_mode) || stat_at_open.st_size <= 0) return false;
        extractor = AMediaExtractor_new();
        if (!extractor || AMediaExtractor_setDataSourceFd(extractor, fd, 0, stat_at_open.st_size) != AMEDIA_OK)
            return false;
        const size_t count = AMediaExtractor_getTrackCount(extractor);
        if (count == 0 || count > 256) return false;
        for (size_t i = 0; i < count; ++i) {
            AMediaFormat* candidate = AMediaExtractor_getTrackFormat(extractor, i);
            if (!candidate) return false;
            const char* mime = nullptr;
            const bool is_video = AMediaFormat_getString(candidate, "mime", &mime) &&
                                  mime && std::strncmp(mime, "video/", 6) == 0;
            if (!is_video) { AMediaFormat_delete(candidate); continue; }
            format = candidate;
            track = static_cast<int>(i);
            break;
        }
        return format && signature(format, sig) &&
               AMediaExtractor_selectTrack(extractor, static_cast<size_t>(track)) == AMEDIA_OK;
    }

    bool unchanged() const {
        struct stat now{};
        return fd >= 0 && fstat(fd, &now) == 0 && same_file(now, stat_at_open);
    }
};

struct TemporaryOutput {
    std::string path;
    int fd = -1;
    bool owned = false;
    explicit TemporaryOutput(const std::string& name) : path(name) {}
    ~TemporaryOutput() {
        if (fd >= 0) close(fd);
        if (owned) (void)unlink(path.c_str());
    }
    bool create() {
        fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0644);
        owned = fd >= 0;
        return owned;
    }
    void close_fd() { if (fd >= 0) { close(fd); fd = -1; } }
};

struct MuxerOwner {
    AMediaMuxer* muxer = nullptr;
    bool started = false;
    ~MuxerOwner() {
        if (muxer) {
            if (started) (void)AMediaMuxer_stop(muxer);
            AMediaMuxer_delete(muxer);
        }
    }
    void close_muxer() {
        if (muxer) { AMediaMuxer_delete(muxer); muxer = nullptr; }
    }
};

struct Digest {
    uint64_t a = 1469598103934665603ULL;
    uint64_t b = 0x9e3779b97f4a7c15ULL;
    uint64_t samples = 0;
    int64_t last_pts = -1;
    void add(const uint8_t* data, size_t size, bool sync) {
        a = (a ^ size) * 1099511628211ULL;
        b = (b ^ static_cast<uint64_t>(sync)) * 0x100000001b3ULL;
        for (size_t i = 0; i < size; ++i) {
            a = (a ^ data[i]) * 1099511628211ULL;
            b = (b ^ (static_cast<uint64_t>(data[i]) + 1)) * 0x100000001b3ULL;
        }
        ++samples;
    }
};

bool read_sample(AMediaExtractor* extractor, std::vector<uint8_t>& buffer, ssize_t& size) {
    if (buffer.empty()) buffer.resize(kInitialSample);
    for (;;) {
        size = AMediaExtractor_readSampleData(extractor, buffer.data(), buffer.size());
        if (size >= 0) return size > 0 && static_cast<size_t>(size) <= buffer.size();
        if (buffer.size() >= kMaxSample) return false;
        buffer.resize(std::min(kMaxSample, buffer.size() * 2));
    }
}

// Input samples are copied without decoding. Reject reordered/unsupported
// streams instead of producing a subtly damaged output, retaining all parts.
bool copy_source(Source& source, AMediaMuxer* muxer, size_t output_track,
                 int64_t& next_pts, Digest& digest, std::vector<uint8_t>& buffer) {
    int64_t base = -1, previous = -1, last_output = -1, step = kDefaultStepUs;
    bool first = true;
    for (;;) {
        const int track = AMediaExtractor_getSampleTrackIndex(source.extractor);
        if (track < 0) break;
        if (track != source.track) return false;
        const int64_t pts = AMediaExtractor_getSampleTime(source.extractor);
        const uint32_t flags = AMediaExtractor_getSampleFlags(source.extractor);
        if (pts < 0 || flags == std::numeric_limits<uint32_t>::max() ||
            (flags & ~static_cast<uint32_t>(AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC)) != 0) return false;
        const bool sync = (flags & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC) != 0;
        if (first && !sync) return false;
        if (base < 0) base = pts;
        if (previous >= 0) {
            if (pts <= previous) return false;
            const int64_t delta = pts - previous;
            if (delta >= 1000 && delta <= 250000) step = delta;
        }
        const int64_t relative = pts - base;
        if (relative < 0 || next_pts > std::numeric_limits<int64_t>::max() - relative) return false;
        const int64_t out_pts = next_pts + relative;
        ssize_t size = 0;
        if (!read_sample(source.extractor, buffer, size) ||
            size > std::numeric_limits<int32_t>::max()) return false;
        AMediaCodecBufferInfo info{};
        info.offset = 0;
        info.size = static_cast<int32_t>(size);
        info.presentationTimeUs = out_pts;
        info.flags = sync ? AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC : 0;
        if (AMediaMuxer_writeSampleData(muxer, output_track, buffer.data(), &info) != AMEDIA_OK)
            return false;
        digest.add(buffer.data(), static_cast<size_t>(size), sync);
        digest.last_pts = out_pts;
        last_output = out_pts;
        previous = pts;
        first = false;
        if (!AMediaExtractor_advance(source.extractor)) break;
    }
    if (first || last_output > std::numeric_limits<int64_t>::max() - step) return false;
    next_pts = last_output + step;
    return source.unchanged();
}

bool verify_output(const char* path, const Signature& expected, const Digest& original,
                   std::vector<uint8_t>& buffer) {
    Source result;
    if (!result.open_file(path) || !compatible(expected, result.sig, false)) return false;
    Digest checked;
    int64_t previous = -1;
    for (;;) {
        const int track = AMediaExtractor_getSampleTrackIndex(result.extractor);
        if (track < 0) break;
        if (track != result.track) return false;
        const int64_t pts = AMediaExtractor_getSampleTime(result.extractor);
        const uint32_t flags = AMediaExtractor_getSampleFlags(result.extractor);
        if (pts < 0 || (previous >= 0 && pts <= previous) ||
            flags == std::numeric_limits<uint32_t>::max() ||
            (flags & ~static_cast<uint32_t>(AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC)) != 0) return false;
        const bool sync = (flags & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC) != 0;
        if (checked.samples == 0 && !sync) return false;
        ssize_t size = 0;
        if (!read_sample(result.extractor, buffer, size)) return false;
        checked.add(buffer.data(), static_cast<size_t>(size), sync);
        checked.last_pts = pts;
        previous = pts;
        if (!AMediaExtractor_advance(result.extractor)) break;
    }
    const int64_t difference = checked.last_pts > original.last_pts ? checked.last_pts - original.last_pts
                                                                   : original.last_pts - checked.last_pts;
    return checked.samples == original.samples && checked.samples > 0 &&
           checked.a == original.a && checked.b == original.b && difference <= 100000 && result.unchanged();
}

// Best effort only: a failure to notify the media scanner does not invalidate
// a committed file. The helper is a new process and prepares arguments before fork.
void notify_scanner(const std::string& path) {
    constexpr char command[] = "/system/bin/am";
    if (access(command, X_OK) != 0) return;
    const std::string uri = "file://" + path;
    const pid_t child = fork();
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        char* const args[] = {const_cast<char*>(command), const_cast<char*>("broadcast"),
                              const_cast<char*>("-a"), const_cast<char*>("android.intent.action.MEDIA_SCANNER_SCAN_FILE"),
                              const_cast<char*>("-d"), const_cast<char*>(uri.c_str()), nullptr};
        execv(command, args);
        _exit(127);
    }
    if (child <= 0) return;
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child || (waited < 0 && errno != EINTR)) return;
        usleep(50000);
    }
    (void)kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

int run(const char* last, const char* output) {
    PathPlan plan;
    struct stat reserved{};
    if (!make_plan(last, output, plan) || !placeholder(plan.output, reserved)) {
        message("invalid paths or missing reserved output");
        return 1;
    }

    // A short recording needs no remux or extra free space.
    if (plan.count == 1) {
        struct stat input{};
        if (lstat(plan.part.c_str(), &input) != 0 || !S_ISREG(input.st_mode) || input.st_size <= 0 ||
            !intact_placeholder(plan.output, reserved) || rename(plan.part.c_str(), plan.output.c_str()) != 0) {
            message("could not finalize the single segment");
            return 1;
        }
        notify_scanner(plan.output);
        return 0;
    }

    TemporaryOutput temp(plan.temporary);
    if (!temp.create()) { message("cannot reserve merge output"); return 1; }
    MuxerOwner owner;
    owner.muxer = AMediaMuxer_new(temp.fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    AMediaMuxer* const muxer = owner.muxer;
    bool good = muxer != nullptr;
    if (!good) message("cannot create MP4 muxer");
    ssize_t track = -1;
    int64_t next_pts = 0;
    Signature first_signature;
    Digest digest;
    std::vector<uint8_t> buffer;
    std::vector<struct stat> part_stats;

    for (uint64_t i = 1; good && i <= plan.count; ++i) {
        set_part(plan, i);
        Source source;
        if (!source.open_file(plan.part.c_str())) { message("cannot read a video segment"); good = false; break; }
        if (i == 1) {
            first_signature = source.sig;
            if (AMediaMuxer_setOrientationHint(muxer, source.sig.rotation) != AMEDIA_OK ||
                (track = AMediaMuxer_addTrack(muxer, source.format)) < 0 ||
                AMediaMuxer_start(muxer) != AMEDIA_OK) {
                message("cannot start MP4 muxer"); good = false; break;
            }
            owner.started = true;
        } else if (!compatible(first_signature, source.sig)) {
            message("segments have different video formats or orientation"); good = false; break;
        }
        if (!copy_source(source, muxer, static_cast<size_t>(track), next_pts, digest, buffer)) {
            message("failed to copy a video segment"); good = false; break;
        }
        part_stats.push_back(source.stat_at_open);
    }
    if (owner.started) {
        if (AMediaMuxer_stop(muxer) != AMEDIA_OK) { message("cannot finish MP4"); good = false; }
        owner.started = false;
    }
    owner.close_muxer();
    if (good && fsync(temp.fd) != 0) { message("cannot flush merged MP4"); good = false; }
    temp.close_fd();
    if (good && !verify_output(plan.temporary.c_str(), first_signature, digest, buffer)) {
        message("merged MP4 failed verification"); good = false;
    }
    for (uint64_t i = 1; good && i <= plan.count; ++i) {
        set_part(plan, i);
        struct stat current{};
        if (lstat(plan.part.c_str(), &current) != 0 || !same_file(current, part_stats[static_cast<size_t>(i - 1)])) {
            message("a segment changed during merge"); good = false;
        }
    }
    if (good && (!intact_placeholder(plan.output, reserved) || rename(plan.temporary.c_str(), plan.output.c_str()) != 0)) {
        message("cannot commit merged MP4"); good = false;
    }
    if (!good) return 1; // Never delete source segments on failure.
    temp.owned = false; // rename committed it as the reserved final file.
    for (uint64_t i = 1; i <= plan.count; ++i) {
        set_part(plan, i);
        struct stat current{};
        if (lstat(plan.part.c_str(), &current) == 0 && same_file(current, part_stats[static_cast<size_t>(i - 1)]))
            if (unlink(plan.part.c_str()) != 0) message("merged MP4 is ready, but a source segment could not be removed");
    }
    notify_scanner(plan.output);
    return 0;
}
} // namespace

int Run(const char* last_part, const char* output) {
    try {
        return run(last_part, output);
    } catch (...) {
        // The caller retains the numbered files if memory or media allocation fails.
        message("unexpected merge failure");
        return 1;
    }
}
} // namespace capture_merge
