#!/usr/bin/env python3
"""Check the production MP4 writer with real H.264 packets (requires PyAV)."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import av


def encode(path):
    with av.open(str(path), "w", format="mp4") as movie:
        stream = movie.add_stream(
            "libx264", rate=30,
            options={"preset": "ultrafast", "tune": "zerolatency", "profile": "baseline",
                     "x264-params": "keyint=60:min-keyint=60:scenecut=0:bframes=0"},
        )
        stream.width, stream.height, stream.pix_fmt = 320, 240, "yuv420p"
        for index in range(180):
            frame = av.VideoFrame(320, 240, "yuv420p")
            for plane_index, plane in enumerate(frame.planes):
                value = (40 + index // 30 * 20) if plane_index == 0 else 128
                plane.update(bytes([value]) * plane.buffer_size)
            for packet in stream.encode(frame):
                movie.mux(packet)
        for packet in stream.encode():
            movie.mux(packet)


def config_as_annexb(config):
    pos = 6
    outputs = []
    for count in (config[5] & 31, None):
        if count is None:
            count = config[pos]
            pos += 1
        buffer = bytearray()
        for _ in range(count):
            length = int.from_bytes(config[pos:pos + 2], "big")
            pos += 2
            buffer += b"\x00\x00\x00\x01" + config[pos:pos + length]
            pos += length
        outputs.append(bytes(buffer))
    return outputs


def avcc_as_annexb(packet):
    data = bytearray()
    pos = 0
    while pos < len(packet):
        length = int.from_bytes(packet[pos:pos + 4], "big")
        pos += 4
        assert length and pos + length <= len(packet)
        data += b"\x00\x00\x00\x01" + packet[pos:pos + length]
        pos += length
    return bytes(data)


def packet_stream(path, annexb=False):
    data = bytearray()
    with av.open(str(path)) as movie:
        stream = movie.streams.video[0]
        config = stream.codec_context.extradata
        assert config and config[0] == 1
        csd0, csd1 = config_as_annexb(config) if annexb else (config, b"")
        data += struct.pack("<IIII", 320, 240, len(csd0), len(csd1)) + csd0 + csd1
        count = 0
        for packet in movie.demux(stream):
            if not packet.size:
                continue
            assert packet.pts is not None and packet.dts == packet.pts
            pts = int(packet.pts * packet.time_base * 1_000_000)
            payload = avcc_as_annexb(bytes(packet)) if annexb else bytes(packet)
            data += struct.pack("<qII", pts, int(packet.is_keyframe), len(payload)) + payload
            count += 1
    assert count == 180
    return bytes(data)


def frames(path):
    levels, pts = [], []
    with av.open(str(path)) as movie:
        for frame in movie.decode(video=0):
            levels.append(bytes(frame.planes[0])[0])
            pts.append(float(frame.pts * frame.time_base))
    return levels, pts


def top_boxes(data):
    position = 0
    while position + 8 <= len(data):
        length = int.from_bytes(data[position:position + 4], "big")
        kind = data[position + 4:position + 8]
        if length == 1:
            length = int.from_bytes(data[position + 8:position + 16], "big")
        assert length >= 8 and position + length <= len(data), (kind, position, length)
        yield position, length, kind
        position += length
    assert position == len(data)


def sparse_over_4gb(original, target):
    # Insert a sparse extended-size free atom before the media fragments and
    # adjust sidx.first_offset. The samples are unchanged but physically live
    # beyond 4 GiB; this verifies 64-bit offsets without allocating 4 GiB.
    data = bytearray(original.read_bytes())
    boxes = list(top_boxes(data))
    sidx = next(pos for pos, _, kind in boxes if kind == b"sidx")
    first_moof = next(pos for pos, _, kind in boxes if kind == b"moof")
    length = (1 << 32) + 4096
    first_offset = sidx + 28
    old = int.from_bytes(data[first_offset:first_offset + 8], "big")
    data[first_offset:first_offset + 8] = (old + length).to_bytes(8, "big")
    with open(target, "wb") as output:
        output.write(data[:first_moof])
        output.write(struct.pack(">I4sQ", 1, b"free", length))
        output.seek(first_moof + length)
        output.write(data[first_moof:])
    assert target.stat().st_size > 1 << 32
    assert target.stat().st_blocks * 512 < 32 * 1024 * 1024, "filesystem did not create a sparse file"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: record_mp4_real_test.py /path/to/record_mp4_driver")
    driver = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        source, result = root / "source.mp4", root / "result.mp4"
        encode(source)
        packets = packet_stream(source)
        process = subprocess.run([driver, str(result)], input=packets, capture_output=True)
        assert process.returncode == 0, process.stderr.decode()
        assert b"frames=180" in process.stdout
        kinds = [kind for _, _, kind in top_boxes(result.read_bytes())]
        assert kinds.count(b"moof") >= 3 and b"sidx" in kinds, kinds
        levels, pts = frames(result)
        assert len(levels) == 180, len(levels)
        for index in (0, 30, 60, 120, 179):
            assert abs(levels[index] - (40 + index // 30 * 20)) <= 8, (index, levels[index])
        assert all(a < b for a, b in zip(pts, pts[1:]))
        assert 5.9 < pts[-1] < 6.1, pts[-1]
        with av.open(str(result)) as movie:
            movie.seek(4_000_000, backward=True)
            seek_frame = next(movie.decode(video=0))
            assert seek_frame.pts * seek_frame.time_base <= 4.1
        annex = root / "annexb.mp4"
        annex_run = subprocess.run([driver, str(annex)], input=packet_stream(source, annexb=True), capture_output=True)
        assert annex_run.returncode == 0, annex_run.stderr.decode()
        assert frames(annex)[0] == levels

        # Sudden death after the first complete fragment must leave playable
        # already-written frames, even without the final seek index.
        interrupted = root / "interrupted.mp4"
        crash = subprocess.run([driver, str(interrupted), "100"], input=packets, capture_output=True)
        assert crash.returncode == 42
        partial, _ = frames(interrupted)
        assert len(partial) >= 50, len(partial)

        huge = root / "over-4gb-sparse.mp4"
        sparse_over_4gb(result, huge)
        huge_levels, _ = frames(huge)
        assert huge_levels == levels
        print("PASS: real H.264 MP4, seek, interrupted recovery and >4 GiB sparse offsets")


if __name__ == "__main__":
    main()
