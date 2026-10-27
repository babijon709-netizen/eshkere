#include "record_capture.h"
#include "record_display.h"
#include "record_mp4.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace record_capture {
namespace {
constexpr int kDefaultBitrate = 20000000;
constexpr int kColorFormatSurface = 0x7f000789;
constexpr uint32_t kKeyFrame = 1;
constexpr uint32_t kCodecConfig = 2;
constexpr uint32_t kEndOfStream = 4;
volatile sig_atomic_t g_stop = 0;
void on_stop(int) { g_stop = 1; }

int64_t monotonic_ms() {
    timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return 0;
    return int64_t(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}
void report(const char* message) { std::fprintf(stderr, "capture: %s\n", message); }
void set_stop_handler() {
    struct sigaction sa{}; sa.sa_handler = on_stop; sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGINT, &sa, nullptr); (void)sigaction(SIGTERM, &sa, nullptr);
}
void extract_csd(AMediaFormat* format, const char* key, std::vector<uint8_t>& output) {
    void* buffer = nullptr; size_t size = 0;
    if (AMediaFormat_getBuffer(format, key, &buffer, &size) && buffer && size && size <= 1024 * 1024)
        output.assign(static_cast<uint8_t*>(buffer), static_cast<uint8_t*>(buffer) + size);
}

struct Codec {
    AMediaCodec* codec = nullptr;
    ANativeWindow* window = nullptr;
    bool started = false;
    ~Codec() {
        if (codec && started) (void)AMediaCodec_stop(codec);
        if (window) ANativeWindow_release(window);
        if (codec) (void)AMediaCodec_delete(codec);
    }
    bool configure(int width, int height, int bitrate,
                   media_status_t (*create_surface)(AMediaCodec*, ANativeWindow**)) {
        codec = AMediaCodec_createEncoderByType("video/avc");
        AMediaFormat* format = AMediaFormat_new();
        if (!codec || !format) { if (format) AMediaFormat_delete(format); return false; }
        AMediaFormat_setString(format, "mime", "video/avc");
        AMediaFormat_setInt32(format, "width", width);
        AMediaFormat_setInt32(format, "height", height);
        AMediaFormat_setInt32(format, "color-format", kColorFormatSurface);
        AMediaFormat_setInt32(format, "bitrate", bitrate);
        AMediaFormat_setInt32(format, "frame-rate", 30);
        AMediaFormat_setInt32(format, "i-frame-interval", 2);
        AMediaFormat_setInt32(format, "max-bframes", 0);
        const media_status_t configured = AMediaCodec_configure(codec, format, nullptr, nullptr,
                                                                 AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaFormat_delete(format);
        if (configured != AMEDIA_OK) return false;
        if (create_surface(codec, &window) != AMEDIA_OK || !window) return false;
        if (AMediaCodec_start(codec) != AMEDIA_OK) return false;
        started = true;
        return true;
    }
};

bool try_start_writer(std::unique_ptr<record_mp4::Writer>& writer, int fd, int width, int height,
                      const std::vector<uint8_t>& csd0, const std::vector<uint8_t>& csd1,
                      std::string& error) {
    if (writer || csd0.empty()) return true;
    std::unique_ptr<record_mp4::Writer> candidate(new record_mp4::Writer(fd));
    if (candidate->start(width, height, csd0.data(), csd0.size(), csd1.data(), csd1.size())) {
        writer = std::move(candidate);
        return true;
    }
    error = candidate->error();
    // A few encoders publish SPS and PPS later in a codec-config buffer.
    // Invalid CSD is rejected before writing the header, so that case can be
    // retried safely. All I/O failures are terminal and remain visible.
    return error == "invalid H.264 codec configuration";
}
} // namespace

int Run(int fd, int bitrate) {
    g_stop = 0;
    set_stop_handler();
    struct stat file{};
    if (fd < 0 || fstat(fd, &file) != 0 || !S_ISREG(file.st_mode) || file.st_size != 0) {
        report("invalid reserved output file"); return 2;
    }
    if (bitrate != 0 && bitrate != 4000000 && bitrate != 8000000 && bitrate != 16000000) {
        report("invalid bitrate"); return 2;
    }
    void* media = dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
    auto create_surface = reinterpret_cast<media_status_t (*)(AMediaCodec*, ANativeWindow**)>(
        media ? dlsym(media, "AMediaCodec_createInputSurface") : nullptr);
    auto signal_eos = reinterpret_cast<media_status_t (*)(AMediaCodec*)>(
        media ? dlsym(media, "AMediaCodec_signalEndOfInputStream") : nullptr);
    if (!create_surface || !signal_eos) { report("surface encoder requires Android 8 or newer"); return 3; }

    record_display::Display display;
    record_display::State state{};
    if (!display.initialize() || !display.state(state)) { report(display.error()); return 3; }
    int width = state.width & ~1, height = state.height & ~1;
    if (width <= 0 || height <= 0) { report("invalid display dimensions"); return 3; }
    std::unique_ptr<Codec> codec(new Codec);
    if (!codec->configure(width, height, bitrate ? bitrate : kDefaultBitrate, create_surface)) {
        // Match screenrecord's practical fallback when the default encoder
        // cannot handle the native display size.
        codec.reset();
        const double scale = std::min(1280.0 / std::max(width, height), 720.0 / std::min(width, height));
        if (scale >= 1.0) { report("cannot configure H.264 surface encoder"); return 3; }
        width = std::max(2, int(width * scale) & ~1);
        height = std::max(2, int(height * scale) & ~1);
        codec.reset(new Codec);
        if (!codec->configure(width, height, bitrate ? bitrate : kDefaultBitrate, create_surface)) {
            report("cannot configure H.264 surface encoder"); return 3;
        }
        std::fprintf(stderr, "capture: encoder fallback %dx%d\n", width, height);
    }
    if (!display.attach(codec->window, state, width, height)) { report(display.error()); return 3; }
    std::fprintf(stderr, "capture: recording %dx%d bitrate=%d\n", width, height, bitrate ? bitrate : kDefaultBitrate);

    std::unique_ptr<record_mp4::Writer> writer;
    std::vector<uint8_t> csd0, csd1;
    std::string error;
    bool failed = false, eos_requested = false, saw_eos = false;
    int64_t eos_deadline = 0, next_display_check = monotonic_ms() + 1000;
    for (;;) {
        const int64_t now = monotonic_ms();
        if (g_stop && !eos_requested) {
            display.detach();
            eos_requested = true;
            eos_deadline = now + 3500;
            if (signal_eos(codec->codec) != AMEDIA_OK) { error = "encoder did not accept stop"; failed = true; break; }
        }
        if (eos_requested && now >= eos_deadline) { error = "encoder did not finish in time"; failed = true; break; }
        if (!eos_requested && now >= next_display_check) {
            record_display::State current{};
            if (!display.state(current)) { error = display.error(); failed = true; break; }
            if (current.width != state.width || current.height != state.height ||
                current.orientation != state.orientation || current.layer_stack != state.layer_stack) {
                if (!display.update(current, width, height)) { error = display.error(); failed = true; break; }
                state = current;
            }
            next_display_check = now + 1000;
        }
        AMediaCodecBufferInfo info{};
        const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec->codec, &info, 100000);
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER || index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* format = AMediaCodec_getOutputFormat(codec->codec);
            if (!format) { error = "cannot obtain encoder output format"; failed = true; break; }
            extract_csd(format, "csd-0", csd0); extract_csd(format, "csd-1", csd1);
            AMediaFormat_delete(format);
            if (!try_start_writer(writer, fd, width, height, csd0, csd1, error)) { failed = true; break; }
            continue;
        }
        if (index < 0) { error = "encoder output failed"; failed = true; break; }
        size_t available = 0;
        uint8_t* data = AMediaCodec_getOutputBuffer(codec->codec, static_cast<size_t>(index), &available);
        if (info.size < 0 || (info.size && (!data || static_cast<size_t>(info.size) > available))) {
            error = "invalid encoder output buffer"; failed = true;
        } else if (info.size > 0 && (info.flags & kCodecConfig)) {
            if (!writer) {
                csd0.assign(data, data + info.size); csd1.clear();
                if (!try_start_writer(writer, fd, width, height, csd0, csd1, error)) failed = true;
            }
        } else if (info.size > 0) {
            if (!writer) { if (error.empty()) error = "encoder did not provide H.264 configuration"; failed = true; }
            else if (!writer->sample(data, static_cast<size_t>(info.size), info.presentationTimeUs,
                                     (info.flags & kKeyFrame) != 0)) {
                error = writer->error(); failed = true;
            }
        }
        if (AMediaCodec_releaseOutputBuffer(codec->codec, static_cast<size_t>(index), false) != AMEDIA_OK) {
            if (error.empty()) error = "cannot release encoder output buffer";
            failed = true;
        }
        if (info.flags & kEndOfStream) { saw_eos = true; break; }
        if (failed) break;
    }
    display.detach();
    if (saw_eos && !eos_requested) { error = "encoder stopped unexpectedly"; failed = true; }
    if (writer) {
        if (!writer->finish()) { if (error.empty()) error = writer->error(); failed = true; }
        std::fprintf(stderr, "capture: %llu frames committed\n", static_cast<unsigned long long>(writer->committed_frames()));
    } else {
        if (error.empty()) error = "no encoded frames";
        failed = true;
    }
    if (failed) { report(error.c_str()); return 4; }
    return 0;
}
} // namespace record_capture
