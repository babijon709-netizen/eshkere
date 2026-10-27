#!/usr/bin/env python3
"""Test the actual system IME and production bridge on an Android emulator."""
import os
import re
import subprocess
import sys
import time
import traceback

REMOTE = "/data/local/tmp/benzware-ime-test"
HEADER = open(os.environ["ANDROID_IME_HEADER"], encoding="ascii").read(300)
PACKAGE = re.search(r'kPackage\[\] = "([^"]+)"', HEADER).group(1)


def run(*args, timeout=40, check=True, input=None):
    return subprocess.run(args, capture_output=True, timeout=timeout, check=check, input=input)


def shell(*args, check=True, timeout=30):
    return run("adb", "shell", *args, check=check, timeout=timeout).stdout.decode(errors="replace")


def ime_visible(proc):
    end = time.monotonic() + 45
    dump = ""
    while time.monotonic() < end:
        if proc.poll() is not None:
            out, err = proc.communicate()
            raise AssertionError(f"bridge exited before IME: {out!r} {err!r}")
        dump = shell("dumpsys", "input_method", check=False)
        window = shell("dumpsys", "window", "windows", check=False)
        if "mInputShown=true" in dump and PACKAGE in window:
            print(f"system IME visible in real focused Activity: {PACKAGE}", flush=True)
            return
        time.sleep(0.5)
    relevant = "\n".join(line for line in dump.splitlines()
                         if any(k in line for k in ("mInputShown", "mShowRequested", "mCurId", "mCurMethodId")))
    raise AssertionError(f"system IME did not become visible: {relevant}")


def launch(binary, closed=True):
    command = f"{REMOTE}/{binary} </dev/null" if closed else f"{REMOTE}/{binary}"
    return subprocess.Popen(["adb", "shell", "-T", command],
                            stdin=subprocess.DEVNULL if closed else subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def finish(proc, expected):
    try:
        out, err = proc.communicate(timeout=45)
    except subprocess.TimeoutExpired as exc:
        raise AssertionError(f"bridge did not finish: {exc.output!r} {exc.stderr!r}") from exc
    assert b"Enter key:" in out and f"IME_RESULT={expected}".encode() in out, (proc.returncode, out, err)
    listing = shell("pm", "list", "packages", check=False)
    assert PACKAGE not in listing, f"temporary helper still installed: {listing}"
    leftovers = shell("ls", "/data/local/tmp", check=False)
    assert PACKAGE not in leftovers, f"temporary helper files left: {leftovers}"
    print(f"bridge result={expected}; temporary app and files removed", flush=True)


def close(proc):
    if proc.poll() is None:
        proc.kill()
        proc.communicate()


def good_after_wrong(binary):
    proc = launch(binary)
    try:
        ime_visible(proc)
        shell("input", "text", "wrong")
        shell("input", "keyevent", "KEYCODE_ENTER")
        time.sleep(1)
        assert proc.poll() is None, "wrong key should keep the editor open"
        assert PACKAGE in shell("dumpsys", "window", "windows", check=False)
        # The Activity selects the wrong entry, so replacement tests correction too.
        shell("input", "text", "t.me/benzware")
        shell("input", "keyevent", "KEYCODE_ENTER")
        finish(proc, "accepted")
    finally:
        close(proc)


def cancellation(binary):
    proc = launch(binary)
    try:
        ime_visible(proc)
        shell("input", "keyevent", "KEYCODE_BACK") # dismiss IME first
        time.sleep(0.5)
        shell("input", "keyevent", "KEYCODE_BACK") # cancel Activity
        finish(proc, "cancelled")
    finally:
        close(proc)


def late_pipe(binary):
    proc = launch(binary, closed=False)
    try:
        ime_visible(proc)
        proc.stdin.write(b"t.me/benzware\n")
        proc.stdin.flush()
        # communicate() flushes a closed stdin unless the handle is cleared.
        proc.stdin.close()
        proc.stdin = None
        finish(proc, "accepted")
    finally:
        close(proc)


def main():
    run("adb", "root", check=False)
    run("adb", "wait-for-device")
    shell("mkdir", "-p", REMOTE)
    shell("input", "keyevent", "KEYCODE_WAKEUP", check=False)
    shell("wm", "dismiss-keyguard", check=False)
    # A hardware keyboard on an emulator can otherwise suppress the software IME.
    shell("settings", "put", "secure", "show_ime_with_hard_keyboard", "1", check=False)
    try:
        for env, name in (("ANDROID_IME_PLAIN", "ime-plain"), ("ANDROID_IME_ENCRYPTED", "ime-encrypted")):
            run("adb", "push", os.environ[env], f"{REMOTE}/{name}")
            shell("chmod", "0755", f"{REMOTE}/{name}")
        good_after_wrong("ime-plain")
        cancellation("ime-encrypted")
        late_pipe("ime-encrypted")
        print("::notice title=Android system keyboard::real IME visible; wrong/correct, cancel, late pipe and encrypted bridge passed")
    finally:
        shell("am", "force-stop", PACKAGE, check=False)
        shell("pm", "uninstall", PACKAGE, check=False)
        shell("rm", "-rf", REMOTE, check=False)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        details = traceback.format_exc()
        print(details, file=sys.stderr)
        print("::error title=Android system keyboard::" +
              details.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A"), flush=True)
        sys.exit(1)
