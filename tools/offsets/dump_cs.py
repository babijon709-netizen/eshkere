#!/usr/bin/env python3
"""Parse an Il2CppDumper `dump.cs` into a light class index.

dump.cs is ~90 MB; this keeps only class headers and fields, so the whole
index fits comfortably in memory and can be queried by name, namespace,
TypeDefIndex or field type.

Usage as a library:
    from dump_cs import Dump
    d = Dump("/path/dump.cs")
    d.klass("PlayerManager")            # first class with that short name
    d.fields("PlayerManager")           # [(offset, type, name), ...]
    d.by_tdi(706)                       # class with TypeDefIndex 706
"""
import re

CLASS_RE = re.compile(r"^\s*(?:\[[^\]]*\]\s*)*(?:public|internal|private|protected|sealed|abstract|static|partial|\s)*"
                      r"(class|struct|interface|enum)\s+([A-Za-z0-9_`<>.,]+)\s*(?::\s*([^{]*))?//\s*TypeDefIndex:\s*(\d+)")
FIELD_RE = re.compile(r"^\s*(?:public|private|protected|internal|static|readonly|const|\s)+"
                      r"([A-Za-z0-9_`<>.,\[\] *]+?)\s+([A-Za-z0-9_`<>*]+)\s*(?:=\s*[^;]+)?;\s*//\s*0x([0-9A-Fa-f]+)")
NS_RE = re.compile(r"^//\s*Namespace:\s*(\S*)")


class Klass:
    __slots__ = ("kind", "name", "namespace", "tdi", "bases", "fields", "line")

    def __init__(self, kind, name, tdi, bases, namespace, line):
        self.kind = kind
        self.name = name
        self.tdi = tdi
        self.bases = bases
        self.namespace = namespace
        self.fields = []
        self.line = line

    def field(self, name):
        for off, typ, fname in self.fields:
            if fname == name:
                return off
        return None

    def by_type(self, typ):
        return [(off, t, n) for off, t, n in self.fields if t == typ]

    def __repr__(self):
        return f"<{self.kind} {self.namespace}.{self.name} TDI {self.tdi} fields {len(self.fields)}>"


class Dump:
    def __init__(self, path):
        self.classes = []
        self._by_name = {}
        self._by_tdi = {}
        self._parse(path)

    def _parse(self, path):
        cur = None
        ns = ""
        with open(path, encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                if line.startswith("// Namespace:"):
                    m = NS_RE.match(line)
                    ns = m.group(1) if m else ""
                    continue
                m = CLASS_RE.match(line)
                if m:
                    kind, name, bases, tdi = m.group(1), m.group(2), (m.group(3) or ""), int(m.group(4))
                    cur = Klass(kind, name, tdi, bases.strip(), ns, lineno)
                    self.classes.append(cur)
                    self._by_name.setdefault(name, []).append(cur)
                    self._by_tdi.setdefault(tdi, []).append(cur)
                    continue
                if cur is not None:
                    fm = FIELD_RE.match(line)
                    if fm:
                        cur.fields.append((int(fm.group(3), 16), fm.group(1).strip(), fm.group(2)))

    # ---- queries ----
    def all(self, name):
        return self._by_name.get(name, [])

    def klass(self, name, namespace=None):
        cands = self._by_name.get(name, [])
        if namespace is not None:
            cands = [c for c in cands if c.namespace == namespace]
        return cands[0] if cands else None

    def fields(self, name, namespace=None):
        k = self.klass(name, namespace)
        return k.fields if k else None

    def by_tdi(self, tdi):
        c = self._by_tdi.get(tdi)
        return c[0] if c else None

    def find_field_type(self, typ):
        out = []
        for c in self.classes:
            for off, t, n in c.fields:
                if t == typ:
                    out.append((c, off, n))
        return out


if __name__ == "__main__":
    import sys
    d = Dump(sys.argv[1])
    print(len(d.classes), "classes,", sum(len(c.fields) for c in d.classes), "fields")
