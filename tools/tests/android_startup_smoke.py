#!/usr/bin/env python3
"""Android/Bionic startup checks. MT Manager and physical touch are not tested."""
import os
import select
import subprocess
import time

remote = "/data/local/tmp/benzware-startup-test"


def run(*args, check=True, input=None, timeout=30):
    return subprocess.run(args, input=input, capture_output=True, check=check, timeout=timeout)


def wait_text(stream, wanted, timeout=10):
    data = b""
    deadline = time.monotonic() + timeout
    while wanted not in data:
        ready, _, _ = select.select([stream], [], [], max(0, deadline - time.monotonic()))
        if not ready:
            raise AssertionError(f"timed out waiting for {wanted!r}; got {data!r}")
        part = os.read(stream.fileno(), 1)
        if not part:
            raise AssertionError(f"EOF waiting for {wanted!r}; got {data!r}")
        data += part
    return data


def exercise(name):
    binary = f"{remote}/{name}"
    good = run("adb", "shell", "-T", binary, input=b"t.me/benzware\r\n", check=False)
    assert good.returncode == 0 and b"Enter key:" in good.stdout, (good.returncode, good.stdout, good.stderr)
    wrong = run("adb", "shell", "-T", binary, input=b"wrong\n", check=False)
    assert wrong.returncode == 1 and b"Invalid key" in wrong.stderr, (wrong.returncode, wrong.stdout, wrong.stderr)
    closed = run("adb", "shell", "-T", f"{binary} </dev/null", check=False)
    assert closed.returncode == 2 and b"GUI fallback required" in closed.stderr, (closed.returncode, closed.stdout, closed.stderr)

    # Simulate a launcher which retains stdout until termination. Its stdin is
    # open but empty at first; fallback must start and late piped input works.
    p = subprocess.Popen(["adb", "shell", "-T", binary], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        wait_text(p.stderr, b"GUI fallback ready\n")
        assert p.poll() is None
        p.stdin.write(b"t.me/benzware\n"); p.stdin.flush()
        assert p.wait(timeout=15) == 0
        assert b"Enter key:" in p.stdout.read()
    finally:
        if p.poll() is None:
            p.kill(); p.wait()
        for stream in (p.stdin, p.stdout, p.stderr):
            stream.close()

    # Force a real Android shell PTY and send input after the helper's
    # fork+setsid, not only during the initial 100 ms probe.
    p = subprocess.Popen(["adb", "shell", "-tt", binary], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        prompt = wait_text(p.stdout, b"Enter key:")
        assert b"Enter key:" in prompt
        wait_text(p.stdout, b"GUI fallback ready")
        time.sleep(0.15)
        p.stdin.write(b"t.me/benzware\n"); p.stdin.flush()
        assert p.wait(timeout=15) == 0
    finally:
        if p.poll() is None:
            p.kill(); p.wait()
        p.stdin.close(); p.stdout.close()
    print(f"android startup pipes / buffered launcher / detached PTY: PASS ({name})")


def main():
    run("adb", "root", check=False)
    run("adb", "wait-for-device")
    run("adb", "shell", "mkdir", "-p", remote)
    try:
        for env, name in (("ANDROID_STARTUP_PLAIN", "startup-plain"),
                          ("ANDROID_STARTUP_ENCRYPTED", "startup-encrypted"),
                          ("ANDROID_STARTUP_UI", "startup-ui")):
            run("adb", "push", os.environ[env], f"{remote}/{name}")
            run("adb", "shell", "chmod", "0755", f"{remote}/{name}")
        for name in ("startup-plain", "startup-encrypted"):
            exercise(name)
        ui = run("adb", "shell", "-T", f"{remote}/startup-ui", timeout=45, check=False)
        assert ui.returncode == 0 and b"android startup UI: PASS" in ui.stdout, (ui.returncode, ui.stdout, ui.stderr)
        print(ui.stdout.decode(errors="replace").strip())
    finally:
        run("adb", "shell", "rm", "-rf", remote, check=False)


if __name__ == "__main__":
    main()
