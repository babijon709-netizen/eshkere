#include "capture_merge.h"
#include <cstdio>

// Standalone API-level smoke test: the app's supervisor uses the same merger
// via a fresh exec before the startup gate, exercised separately by host tests.
int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "expected last-part and output paths\n"); return 2; }
    return capture_merge::Run(argv[1], argv[2]);
}
