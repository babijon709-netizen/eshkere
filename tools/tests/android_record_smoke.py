#!/usr/bin/env python3
"""Record the emulator's actual display using the production continuous backend."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import traceback

import av


def run(*args, check=True, timeout=30):
    return subprocess.run(args, check=check, text=True, capture_output=True, timeout=timeout)


def main():
    helper = Path(os.environ["ANDROID_RECORD_HELPER"]).resolve()
    remote = "/data/local/tmp/benzware-record-smoke"
    run("adb", "root", check=False)
    run("adb", "wait-for-device")
    run("adb", "shell", "rm", "-rf", remote)
    run("adb", "shell", "mkdir", "-p", remote)
    run("adb", "push", str(helper), f"{remote}/record-helper")
    run("adb", "shell", "chmod", "0755", f"{remote}/record-helper")
    run("adb", "shell", "input", "keyevent", "KEYCODE_WAKEUP", check=False)
    run("adb", "shell", "wm", "dismiss-keyguard", check=False)
    command = ["adb", "shell", f"{remote}/record-helper", f"{remote}/capture.mp4"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        # Change the display while recording, rather than merely validating an
        # encoder-generated black frame. No game or production overlay is run.
        for _ in range(6):
            time.sleep(0.8)
            if process.poll() is not None:
                break
            run("adb", "shell", "input", "swipe", "300", "700", "300", "250", "300", check=False)
        if process.poll() is not None:
            output = process.communicate(timeout=5)[0]
            raise RuntimeError(f"recorder exited early ({process.returncode}): {output}")
        pids = run("adb", "shell", "pidof", "record-helper").stdout.strip().split()
        if len(pids) != 1 or not pids[0].isdigit():
            raise RuntimeError(f"unexpected recorder pids: {pids}")
        run("adb", "shell", "kill", "-INT", pids[0])
        output = process.communicate(timeout=15)[0]
        if process.returncode:
            raise RuntimeError(f"recorder failed ({process.returncode}): {output}")
        with tempfile.TemporaryDirectory() as tmp:
            clip = Path(tmp) / "capture.mp4"
            run("adb", "pull", f"{remote}/capture.mp4", str(clip))
            with av.open(str(clip)) as movie:
                stream = movie.streams.video[0]
                frames = list(movie.decode(stream))
            if len(frames) < 2:
                raise RuntimeError(f"expected several captured frames, got {len(frames)}: {output}")
            if max(bytes(frames[-1].planes[0])) < 24:
                raise RuntimeError(f"captured display appears black: {output}")
            timestamps = [float(frame.pts * frame.time_base) for frame in frames]
            if not all(a < b for a, b in zip(timestamps, timestamps[1:])):
                raise RuntimeError(f"nonmonotonic frame timestamps: {timestamps[:30]}")
            print(f"::notice title=Android continuous capture::PASS: {len(frames)} decoded frames, "
                  f"{stream.width}x{stream.height}, {timestamps[-1]:.2f}s, {clip.stat().st_size} bytes")
    finally:
        if process.poll() is None:
            run("adb", "shell", "pkill", "-INT", "record-helper", check=False)
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
        run("adb", "shell", "rm", "-rf", remote, check=False)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        message = str(exc).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print(f"::error title=Android continuous capture failed::{message}", flush=True)
        traceback.print_exc()
        raise
