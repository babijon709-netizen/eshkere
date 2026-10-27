// Minimal executable around the same startup gate used by main.cpp.
#include "bw_obf.h"
#include "startup_key.h"

int main() {
    BW_GUARD();
    return startup_key::Verify() ? 0 : 1;
}
