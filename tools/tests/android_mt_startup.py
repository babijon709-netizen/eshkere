#!/usr/bin/env python3
"""Exercise the real startup gate inside MT Manager's script-executor window.

The emulator runs an x86_64 build of the same gate, not the production arm64
binary. SELinux is relaxed only in this disposable emulator so an unprivileged
MT Manager can execute the test binary from /data/local/tmp without Magisk.
"""
import io
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

PKG = os.environ["MT_PACKAGE"]
REMOTE = "/data/local/tmp/benzware-mt-test.sh"
LAST = {}
TRACE = []


def run(*args, check=True, timeout=40, input=None):
    return subprocess.run(args, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=check, timeout=timeout)


def adb(*args, **kwargs):
    return run("adb", *args, **kwargs)


def text(data):
    return data.decode("utf-8", "replace")


def notice(title, message):
    message = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print(f"::notice title={title}::{message}", flush=True)


def snapshot(label):
    adb("shell", "uiautomator", "dump", "/sdcard/mt-ui.xml", check=False, timeout=45)
    result = adb("exec-out", "cat", "/sdcard/mt-ui.xml", check=False)
    xml = text(result.stdout)
    nodes = []
    try:
        for node in ET.fromstring(xml).iter("node"):
            attrs = node.attrib
            value = attrs.get("text", "") or attrs.get("content-desc", "")
            if value:
                # Keep dialog buttons in diagnostic output even when an
                # agreement contains thousands of characters.
                nodes.append((value[:160], attrs.get("bounds", ""), attrs.get("clickable", "")))
    except ET.ParseError:
        pass
    screenshot = adb("exec-out", "screencap", "-p", timeout=30).stdout
    path = Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "mt-startup-last.png"
    path.write_bytes(screenshot)
    ocr = ""
    try:
        image = Image.open(io.BytesIO(screenshot)).convert("RGB")
        image = image.resize((image.width * 2, image.height * 2))
        buf = io.BytesIO()
        image.save(buf, format="PNG")
        ocr = text(run("tesseract", "stdin", "stdout", "--psm", "6", input=buf.getvalue(), timeout=25).stdout)
    except Exception as exc:
        ocr = f"OCR error: {exc}"
    LAST.update(label=label, nodes=nodes, ocr=ocr)
    line = f"SCREEN {label}: nodes={nodes!r}; OCR={ocr!r}"
    TRACE.append(line)
    print(line, flush=True)
    return nodes, ocr


def tap_node(nodes, terms, exact=False):
    for term in terms:
        for value, bounds, _ in nodes:
            label = value.lower().strip()
            matched = label == term.lower() if exact else term.lower() in label
            if not matched:
                continue
            match = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", bounds)
            if match:
                x1, y1, x2, y2 = map(int, match.groups())
                adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
                print(f"TAP {value!r}", flush=True)
                return True
    return False


def prepare():
    adb("root", check=False)
    adb("wait-for-device")
    adb("shell", "setenforce", "0", check=False)
    adb("shell", "chmod", "0755", "/data/local/tmp")
    adb("install", "-r", "-g", os.environ["MT_APK"], timeout=120)
    for perm in ("android.permission.READ_EXTERNAL_STORAGE", "android.permission.WRITE_EXTERNAL_STORAGE"):
        adb("shell", "pm", "grant", PKG, perm, check=False)
    adb("shell", "appops", "set", PKG, "MANAGE_EXTERNAL_STORAGE", "allow", check=False)
    adb("shell", "settings", "put", "system", "accelerometer_rotation", "0", check=False)
    launch = adb("shell", "monkey", "-p", PKG, "1", check=False)
    TRACE.append(f"MT launch: {text(launch.stdout)} {text(launch.stderr)}")
    print(TRACE[-1], flush=True)
    time.sleep(4)
    for i in range(4):
        nodes, _ = snapshot(f"first-start-{i}")
        if not tap_node(nodes, ("allow", "agree", "i agree", "read and agree", "accept", "continue", "ok",
                                "while using the app", "allow access"), exact=True):
            break
        time.sleep(2)


def start_file(binary):
    adb("shell", "am", "force-stop", PKG, check=False)
    adb("push", binary, REMOTE)
    adb("shell", "chmod", "0755", REMOTE)
    result = adb("shell", "am", "start", "-W", "-a", "android.intent.action.VIEW",
                 "-d", "file://" + REMOTE, "-t", "application/x-sh",
                 PKG, check=False)
    TRACE.append(f"OpenFileActivity: {text(result.stdout)} {text(result.stderr)}")
    print(TRACE[-1], flush=True)
    time.sleep(3)
    for i in range(6):
        nodes, ocr = snapshot(f"open-{i}")
        combined = "\n".join(x[0] for x in nodes) + "\n" + ocr
        if re.search(r"Enter\s+key\s*:", combined, re.I):
            return
        if tap_node(nodes, ("linux script", "execute script", "execute", "run")):
            time.sleep(2)
            continue
        time.sleep(2)
    raise AssertionError("MT did not display the key prompt")


def submit_key():
    adb("shell", "input", "text", "t.me/benzware")
    adb("shell", "input", "keyevent", "66")
    time.sleep(3)
    nodes, ocr = snapshot("after-input")
    combined = "\n".join(x[0] for x in nodes) + "\n" + ocr
    if "KEY_ACCEPTED" not in combined and "KEY ACCEPTED" not in combined:
        raise AssertionError("MT did not display the success marker after input")


def main():
    prepare()
    for env in ("ANDROID_STARTUP_PLAIN", "ANDROID_STARTUP_ENCRYPTED"):
        start_file(os.environ[env])
        submit_key()
        print(f"MT Manager script execution: PASS ({env})", flush=True)
    notice("MT Manager startup", "Prompt visible before input and key accepted in MT's script executor (plain + encrypted).")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        detail = f"{exc}; last screen={LAST}"
        debug = "\n".join(TRACE[:6])
        notice("MT diagnostic", debug[:10000])
        activity = adb("shell", "dumpsys", "activity", "activities", check=False)
        lines = [line for line in text(activity.stdout).splitlines()
                 if any(token in line for token in ("mResumed", "mFocused", "topResumed", "bin.mt.plus"))]
        notice("MT activity state", "\n".join(lines[-12:]))
        print("::error title=MT Manager startup::" + detail.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")[-3500:], flush=True)
        raise
    finally:
        adb("shell", "am", "force-stop", PKG, check=False)
        adb("shell", "rm", "-f", REMOTE, check=False)
