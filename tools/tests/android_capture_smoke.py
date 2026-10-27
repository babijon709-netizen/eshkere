#!/usr/bin/env python3
"""Exercise the real MediaNDK merger in an Android emulator with real H.264 MP4s.

Requires adb, an Android emulator/device, a compiled Android test helper at
ANDROID_CAPTURE_MERGE_HELPER, and PyAV with the libx264 encoder on the host.
The production overlay and any game are not started by this test.
"""
import os
from pathlib import Path
import subprocess
import tempfile

import av


def run(*args, check=True):
    return subprocess.run(args, check=check, text=True, capture_output=True)


def make_clip(path, luminance, width=320):
    with av.open(str(path), "w", format="mp4") as output:
        stream = output.add_stream(
            "libx264", rate=30,
            options={"preset": "ultrafast", "tune": "zerolatency", "profile": "baseline",
                     "x264-params": "keyint=30:min-keyint=30:scenecut=0:bframes=0"},
        )
        stream.width, stream.height, stream.pix_fmt = width, 240, "yuv420p"
        for index in range(30):
            frame = av.VideoFrame(width, 240, "yuv420p")
            for plane_index, plane in enumerate(frame.planes):
                value = luminance + index % 3 if plane_index == 0 else 128
                plane.update(bytes([value]) * plane.buffer_size)
            for packet in stream.encode(frame):
                output.mux(packet)
        for packet in stream.encode():
            output.mux(packet)


def frame_levels(path):
    levels, pts = [], []
    with av.open(str(path)) as movie:
        for frame in movie.decode(video=0):
            levels.append(bytes(frame.planes[0])[0])
            pts.append(float(frame.pts * frame.time_base))
    return levels, pts


def main():
    helper = Path(os.environ["ANDROID_CAPTURE_MERGE_HELPER"]).resolve()
    remote = "/data/local/tmp/benzware-merge-smoke"
    run("adb", "shell", "rm", "-rf", remote)
    run("adb", "shell", "mkdir", "-p", remote)
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        clips = []
        for index, luminance in enumerate((40, 100, 170), 1):
            path = root / f"clip-part{index:010d}.mp4"
            make_clip(path, luminance)
            clips.append(path)
        mismatch = root / "bad-part0000000002.mp4"
        make_clip(mismatch, 100, width=352)
        run("adb", "push", str(helper), f"{remote}/merge")
        run("adb", "shell", "chmod", "0755", f"{remote}/merge")
        for clip in clips:
            run("adb", "push", str(clip), f"{remote}/{clip.name}")
        run("adb", "shell", "touch", f"{remote}/clip.mp4")
        result = run("adb", "shell", f"{remote}/merge", f"{remote}/{clips[-1].name}", f"{remote}/clip.mp4", check=False)
        if result.returncode:
            raise RuntimeError(f"Android merge failed ({result.returncode}): {result.stdout} {result.stderr}")
        run("adb", "pull", f"{remote}/clip.mp4", str(root / "result.mp4"))
        levels, pts = frame_levels(root / "result.mp4")
        assert len(levels) == 90, f"expected 90 decoded frames, got {len(levels)}"
        for index, expected in ((0, 40), (30, 100), (60, 170)):
            assert abs(levels[index] - expected) <= 8, (index, levels[index], expected)
        assert all(a < b for a, b in zip(pts, pts[1:])), "non-increasing timestamps"
        assert 2.8 < pts[-1] < 3.2, f"unexpected last timestamp: {pts[-1]}"
        for clip in clips:
            assert run("adb", "shell", "test", "!", "-e", f"{remote}/{clip.name}", check=False).returncode == 0
        print(f"PASS: Android MediaNDK remuxed and decoded {len(levels)} real H.264 frames")

        # An incompatible second segment must survive unchanged on the device.
        run("adb", "push", str(clips[0]), f"{remote}/bad-part0000000001.mp4")
        run("adb", "push", str(mismatch), f"{remote}/{mismatch.name}")
        run("adb", "shell", "touch", f"{remote}/bad.mp4")
        result = run("adb", "shell", f"{remote}/merge", f"{remote}/{mismatch.name}", f"{remote}/bad.mp4", check=False)
        assert result.returncode != 0, "incompatible tracks unexpectedly merged"
        for name in ("bad-part0000000001.mp4", mismatch.name):
            run("adb", "shell", "test", "-s", f"{remote}/{name}")
        assert run("adb", "shell", "test", "!", "-e", f"{remote}/bad.mp4.merging", check=False).returncode == 0
        print("PASS: incompatible Android segments retained")
    run("adb", "shell", "rm", "-rf", remote)


if __name__ == "__main__":
    main()
