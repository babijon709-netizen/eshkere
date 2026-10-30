#pragma once

namespace capture_merge {
// Run in a freshly exec'd helper process, not in the multithreaded overlay or
// its fork child. last_part is the final numbered screenrecord segment; output
// is the already-reserved session MP4. Returns 0 only after a complete output
// was committed. Original segments are retained on any failure.
int Run(const char* last_part, const char* output);
}
