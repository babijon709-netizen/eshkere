#!/usr/bin/env python3
"""Report emulator libgui ABI symbols as CI annotations for the recording smoke test."""
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    ndk = Path(os.environ["ANDROID_SDK_ROOT"]) / "ndk/26.3.11579264/toolchains/llvm/prebuilt/linux-x86_64/bin"
    with tempfile.TemporaryDirectory() as tmp:
        library = Path(tmp) / "libgui.so"
        subprocess.run(["adb", "pull", "/system/lib64/libgui.so", str(library)], check=True, capture_output=True)
        result = subprocess.run([str(ndk / "llvm-nm"), "-D", "--defined-only", str(library)],
                                check=True, capture_output=True, text=True)
        keys = ("createDisplayE", "createVirtualDisplayE", "destroyDisplayE", "destroyVirtualDisplayE",
                "mirrorDisplayE", "getDefaultE", "getIGraphicBufferProducerE", "getBuiltInDisplayE",
                "getInternalDisplayTokenE", "getPhysicalDisplayIdsE", "getPhysicalDisplayTokenE",
                "getDisplayStateE", "getDisplayInfoE", "setDisplaySurfaceE", "setDisplayProjectionE",
                "setDisplayLayerStackE", "setLayerStackE", "setDisplaySizeE",
                "openGlobalTransactionE", "closeGlobalTransactionE",
                "TransactionC1E", "TransactionC2E", "TransactionD1E", "TransactionD2E", "Transaction5applyE",
                "SortedVectorINS_12DisplayStateEEC")
        symbols = [line.split()[-1] for line in result.stdout.splitlines()
                   if len(line.split()[-1]) < 220 and any(key in line for key in keys)]
        api = subprocess.run(["adb", "shell", "getprop", "ro.build.version.sdk"],
                             check=True, capture_output=True, text=True).stdout.strip()
        if not symbols:
            raise RuntimeError("no relevant libgui symbols found")
        escaped = "%0A".join(symbols)
        print(f"::notice title=Android {api} libgui recording ABI::{escaped}")


if __name__ == "__main__":
    main()
