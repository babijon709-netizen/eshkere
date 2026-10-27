#pragma once
#include "startup_key.h"

namespace startup_ime {
// The native SurfaceFlinger overlay cannot itself own an Android IME window.
// Ask a temporary, permissionless Activity before creating the overlay/touch.
enum class Result { Accepted, Cancelled, Invalid, Unavailable };
Result Request(startup_key::FdReader& input);
}
