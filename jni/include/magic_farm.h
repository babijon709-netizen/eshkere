#pragma once
#include "magic_farm_protocol.h"

namespace magicfarm {
// External controller; the loader runs once per process on a worker thread.
void Update(float dt);
void Stop();
void Shutdown();
State status();
const char* status_text();
}
