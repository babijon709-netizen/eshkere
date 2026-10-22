#!/usr/bin/env python3
"""Сверка двух Il2CppDumper-дампов (dump.cs) по классам и полям.

Использование:
    python3 tools/offsets/offsets_diff.py release/dump.cs beta/dump.cs \
        PlayerManager FPMotion KCC ...

Зачем. Обфускатор переименовывает классы и часть полей в каждом билде, но
читаемые имена (m_MaxReach, hitRadius, respawning, userID, clanId,
m_IdleOffset, m_AimOffset, ...) остаются. По ним смещения сопоставляются
точно; всё, что не совпало по именам, остаётся на ручную проверку — скрипт
честно пишет, что именно не удалось сопоставить.

Каждый класс печатается в виде «ПЕРЕЕХАЛО / ОК» со списком полей, чьё
смещение изменилось, плюс поля, существующие только в одном из дампов.
"""
import pickle
import re
import sys

CLASS_RE = re.compile(
    r"^\s*(?:\[[^\]]*\]\s*)?"
    r"(?:public|internal|private|protected)?\s*"
    r"(?:sealed\s+|abstract\s+|static\s+|unsafe\s+|readonly\s+|partial\s+)*"
    r"(class|struct|interface|enum)\s+([A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s*:\s*([^{]*))?"
)
FIELD_RE = re.compile(
    r"^\s*(?:\[[^\]]*\]\s*)*"
    r"(public|private|protected|internal|static|const|readonly|volatile|new|unsafe|fixed|\s)*"
    r"([A-Za-z_][A-Za-z0-9_<>,\.\[\]\? ]*?)\s+"
    r"([<A-Za-z_][A-Za-z0-9_>]*(?:k__BackingField)?)\s*"
    r"(?:=\s*[^;]*)?;\s*"
    r"//\s*(0x[0-9A-Fa-f]+)\s*$"
)
STATIC_RE = re.compile(r"//\s*Static\s+(0x[0-9A-Fa-f]+)\s*$")
# Пустой namespace в dump.cs пишется как "// Namespace: " — после strip()
# содержимого нет, поэтому группа обязана быть жадной, но необязательной.
NS_RE = re.compile(r"^//\s*Namespace:\s*(.*)$")


def parse(path):
    """-> {имя_класса: {'full','ns','bases','fields','statics'}}"""
    out = {}
    ns = ""
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            stripped = line.strip()
            if stripped.startswith("// Namespace:"):
                ns = NS_RE.match(stripped).group(1).strip()
                continue
            m = CLASS_RE.match(line)
            if m and not stripped.startswith("//"):
                kind, name, bases = m.group(1), m.group(2), m.group(3)
                cur = {
                    "full": (ns + "." + name) if ns else name,
                    "ns": ns,
                    "bases": (bases or "").strip(),
                    "fields": [],
                    "statics": [],
                }
                out.setdefault(name, cur)
                out[cur["full"]] = cur
                continue
            if cur is None:
                continue
            if stripped == "}":
                cur = None
                continue
            mf = FIELD_RE.match(line)
            if mf:
                typ, fname, off = mf.group(2).strip(), mf.group(3), int(mf.group(4), 16)
                ms = STATIC_RE.search(line)
                (cur["statics"] if ms else cur["fields"]).append(
                    (int(ms.group(1), 16) if ms else off, typ, fname)
                )
    return out


def load(path, cache_dir="/tmp"):
    import os
    import hashlib
    key = hashlib.sha1(os.path.abspath(path).encode()).hexdigest()[:16]
    cache = os.path.join(cache_dir, "offsets_diff_%s.pkl" % key)
    if os.path.exists(cache) and os.path.getmtime(cache) >= os.path.getmtime(path):
        with open(cache, "rb") as fh:
            return pickle.load(fh)
    data = parse(path)
    os.makedirs(cache_dir, exist_ok=True)
    with open(cache, "wb") as fh:
        pickle.dump(data, fh)
    return data


def find(dump, name):
    if name in dump:
        return dump[name]
    hits = [v for k, v in dump.items() if k.split(".")[-1] == name]
    return hits[0] if len(hits) == 1 else None


def diff(a, b, name, label_a="A", label_b="B"):
    ca, cb = find(a, name), find(b, name)
    if ca is None or cb is None:
        print("%-28s ОТСУТСТВУЕТ (%s=%s %s=%s)"
              % (name, label_a, ca is not None, label_b, cb is not None))
        return
    fa = {n: (o, t) for o, t, n in ca["fields"]}
    fb = {n: (o, t) for o, t, n in cb["fields"]}
    sa = {n: (o, t) for o, t, n in ca["statics"]}
    sb = {n: (o, t) for o, t, n in cb["statics"]}
    moved = [(n, fa[n], fb[n]) for n in sorted(set(fa) & set(fb)) if fa[n][0] != fb[n][0]]
    moved_s = [(n, sa[n], sb[n]) for n in sorted(set(sa) & set(sb)) if sa[n][0] != sb[n][0]]
    tag = "ПЕРЕЕХАЛО" if (moved or moved_s) else "ОК"
    print("%-28s %-10s полей общих: %d"
          % (name, tag, len(set(fa) & set(fb))))
    for n, x, y in moved:
        print("     %-26s %s +0x%03X (%s) -> %s +0x%03X (%s)"
              % (n, label_a, x[0], x[1], label_b, y[0], y[1]))
    for n, x, y in moved_s:
        print("     static %-19s %s +0x%03X -> %s +0x%03X" % (n, label_a, x[0], label_b, y[0]))
    only_a = sorted(set(fa) - set(fb))
    only_b = sorted(set(fb) - set(fa))
    if only_a:
        print("     только в %s: %s" % (label_a, ", ".join(only_a)))
    if only_b:
        print("     только в %s: %s" % (label_b, ", ".join(only_b)))


def main(argv):
    if len(argv) < 4:
        print(__doc__.strip())
        return 2
    path_a, path_b, names = argv[1], argv[2], argv[3:]
    a, b = load(path_a), load(path_b)
    print("=== %s vs %s" % (path_a, path_b))
    for n in names:
        diff(a, b, n, "релиз", "бета")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
