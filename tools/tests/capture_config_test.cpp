// Exercise the real config trailer codec without touching /data/local.
#include "../../jni/src/config.cpp"
#include <cassert>
#include <sstream>

AppState g_state;

int main() {
    static_assert(sizeof(CfgBlob) == 260, "legacy config layout changed");
    static_assert(sizeof(CfgExt2Trailer) == 252, "extended trailer layout changed");
    for (bool visible : {false, true}) {
        g_state.streamer_mode = visible;
        g_state.esp_pumpkins = true;
        g_state.esp_armor = true;
        std::ostringstream output(std::ios::binary);
        WriteExt2Trailer(output);
        const std::string bytes = output.str();
        assert(bytes.size() == sizeof(CfgExt2Trailer));
        std::istringstream input(bytes, std::ios::binary);
        CfgExt2Trailer decoded{};
        assert(ReadExt2Trailer(input, decoded));
        assert(overlay_capture::visible_from_config(decoded.flags) == visible);
        assert((decoded.flags & (1u << 15)) && (decoded.flags & (1u << 16)));

        // Older trailer without the final pumpkin color still decodes and
        // keeps unrelated flags, including the newly allocated capture bit.
        std::istringstream legacy(bytes.substr(0, bytes.size() - sizeof(ImVec4)), std::ios::binary);
        CfgExt2Trailer decoded_legacy{};
        assert(ReadExt2Trailer(legacy, decoded_legacy));
        assert(overlay_capture::visible_from_config(decoded_legacy.flags) == visible);
        assert((decoded_legacy.flags & (1u << 16)) != 0);
        std::istringstream truncated(bytes.substr(0, 12), std::ios::binary);
        CfgExt2Trailer ignored{};
        assert(!ReadExt2Trailer(truncated, ignored));
    }
    // Existing files have no capture bit: capture visibility stays enabled.
    assert(overlay_capture::visible_from_config(0));
    std::puts("PASS: real config trailer round-trip, old/truncated layouts, unchanged flag bits");
}
