#!/usr/bin/env python3
"""elf_crypt.py — пост-линковочная обфускация benzware.sh.

Шифрует все read-only .rodata* секции ELF64 LE (arm64 / x86_64) потоковым
splitmix64-шифром, ключованным на ВИРТУАЛЬНЫЙ АДРЕС каждого байта и случайный
ключ сборки. Диапазоны записывает в .bwmeta; ранний init_array-конструктор
bw_obf.cpp снимает шифровку до любого C++ static initializer.

Что делает ещё:
  * исключает из шифрования 64K-блоки, где лежит хотя бы байт .text (иначе
    mprotect на RW-окне снял бы X с исполняемой страницы);
  * исключает 8-байтные слоты, в которые лоадер пишет relocations;
  * проверяет, что контрольные строки исчезли из файла;
  * обнуляет таблицу заголовков секций и обрезает хвост файла — section
    names / .comment / .shstrtab не достаются дизассемблеру.

Никаких внешних зависимостей. Коды выхода: 0 ок, 1 ошибка/проверка провалена.
"""

import argparse
import os
import struct
import sys

MASK = (1 << 64) - 1

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_NOBITS = 8
PT_LOAD = 1
PT_DYNAMIC = 2
DT_NULL = 0
DT_STRTAB = 5
DT_JMPREL = 23
DT_RELA = 7
DT_RELASZ = 8
DT_RELAENT = 9
DT_REL = 17
DT_RELSZ = 18
DT_RELENT = 19
DT_ANDROID_RELA = 0x60000011
DT_ANDROID_RELASZ = 0x60000012

# 64 KiB — консервативная гранулярность: покрывает устройства с 4K/16K/64K
# страницами. Реальный mprotect в бинаре берёт AT_PAGESZ.
GUARD_GRAN = 0x10000

MAGIC = bytes([
    0xB7, 0x53, 0x1E, 0x9A, 0x44, 0xC0, 0xF1, 0x6D,
    0x2A, 0x88, 0x37, 0xDE, 0x90, 0x0B, 0x65, 0xEC,
])
MAX_RANGES = 1024
META_HDR = 24
META_SIZE = META_HDR + MAX_RANGES * 16

# Длинные уникальные строки, которые в НЕзашифрованном бинаре лежат именно
# в строковых .rodata (проверено на arm64-артефакте). Если после патча хоть
# одна осталась в неисполняемой секции — сборка обязана упасть.
CANARIES = [
    b"com.catsbit.oxidesurvivalisland",
    b"C61StIeKasRIz89g3c6xLzIc9L0LaRbI",
    b"iptables -I INPUT ! -i lo -p udp",
    b"/proc/%d/maps",
    b"PlayerLoopHelper",
    b"Oxide.PlayerManager",
    b"PlayerEventHandler",
    b"--check-driver",
    b"t.me/benzware",
    "Версия игры".encode(),
    "Способ чтения".encode(),
    "Прямое чтение".encode(),
]


def mix64(x):
    z = (x + 0x9E3779B97F4A7C15) & MASK
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    return z ^ (z >> 31)


def stream_byte(vaddr, key):
    seed = vaddr ^ key ^ (((key << 17) | (key >> 47)) & MASK)
    return mix64(seed) & 0xFF


class Elf:
    def __init__(self, path, data):
        self.path = path
        self.d = bytearray(data)
        d = self.d
        if len(d) < 64 or d[:4] != b"\x7fELF" or d[4] != 2 or d[5] != 1:
            raise RuntimeError("не ELF64 LE")
        self.machine = struct.unpack_from("<H", d, 0x12)[0]
        self.phoff = struct.unpack_from("<Q", d, 0x20)[0]
        self.shoff = struct.unpack_from("<Q", d, 0x28)[0]
        self.phentsize, self.phnum = struct.unpack_from("<HH", d, 0x36)
        self.shentsize, self.shnum, self.shstrndx = struct.unpack_from("<HHH", d, 0x3A)
        self.phdrs = []
        for i in range(self.phnum):
            o = self.phoff + i * self.phentsize
            t, fl, off, va, pa, fsz, msz, al = struct.unpack_from("<IIQQQQQQ", d, o)
            self.phdrs.append(dict(t=t, fl=fl, off=off, va=va, fsz=fsz, msz=msz))
        self.secs = []
        if self.shoff and self.shnum:
            strtab = self._raw_section(self.shstrndx)
            for i in range(self.shnum):
                o = self.shoff + i * self.shentsize
                (name, typ, flags, addr, off, size, link, info, align, entsize) = \
                    struct.unpack_from("<IIQQQQIIQQ", d, o)
                end = strtab.find(b"\0", name) if strtab else -1
                nm = strtab[name:end].decode("utf-8", "replace") if end >= 0 else ""
                self.secs.append(dict(i=i, name=nm, typ=typ, flags=flags,
                                      addr=addr, off=off, size=size))

    def _raw_section(self, idx):
        o = self.shoff + idx * self.shentsize
        off, size = struct.unpack_from("<QQ", self.d, o + 0x18)
        return bytes(self.d[off:off + size])

    def va_to_off(self, va):
        for p in self.phdrs:
            if p["t"] == PT_LOAD and p["va"] <= va < p["va"] + p["fsz"]:
                return p["off"] + (va - p["va"])
        return None

    def dynamic_reloc_targets(self):
        """Слоты (vaddr), куда лоадер пишет R_*_RELATIVE/IRELATIVE/JUMP_SLOT.

        Берём их прямо из SHT_RELA/SHT_REL секций — это точнее, чем разбор
        DT_* тегов, и не зависит от Android packed relocations (APS2/RELR).
        """
        out = set()
        for s in self.secs:
            if s["typ"] not in (4, 9):  # SHT_RELA, SHT_REL
                continue
            step = 24 if s["typ"] == 4 else 16
            o = s["off"]
            end = min(o + s["size"], len(self.d))
            while o + step <= end:
                r_off = struct.unpack_from("<Q", self.d, o)[0]
                if r_off and r_off < (1 << 48):
                    out.add(r_off)
                o += step
        return out


def subtract(intervals, holes):
    """Вычесть из [(start,end)] список [(start,end)]. Интервалы не пересекаются."""
    holes = sorted(holes)
    result = []
    for a, b in intervals:
        pieces = [(a, b)]
        for ha, hb in holes:
            if hb <= a or ha >= b:
                continue
            nxt = []
            for pa, pb in pieces:
                if hb <= pa or ha >= pb:
                    nxt.append((pa, pb))
                    continue
                if pa < ha:
                    nxt.append((pa, ha))
                if hb < pb:
                    nxt.append((hb, pb))
            pieces = nxt
        result.extend(pieces)
    return sorted(x for x in result if x[1] > x[0])


def find_all(data, needle):
    out = []
    off = 0
    while True:
        i = data.find(needle, off)
        if i < 0:
            return out
        out.append(i)
        off = i + 1


def section_at(elf, file_off):
    for s in elf.secs:
        if s["typ"] != SHT_NOBITS and s["off"] <= file_off < s["off"] + s["size"]:
            return s
    return None


def canary_report(elf, strict=True):
    problems = []
    for can in CANARIES:
        hits = find_all(bytes(elf.d), can)
        for h in hits:
            s = section_at(elf, h)
            name = s["name"] if s else "?"
            exec_sec = bool(s and (s["flags"] & SHF_EXECINSTR))
            if exec_sec:
                print(f"  warn  {can[:28]!r:32} {h:#x} в исполняемой {name} (immediates)")
                continue
            problems.append((can, h, name))
    if problems:
        for can, h, name in problems[:10]:
            print(f"  FAIL  {can[:28]!r:32} {h:#x} @{name}", file=sys.stderr)
        if strict:
            raise RuntimeError(f"в бинаре остались открытые строки: {len(problems)}")
    return len(problems)


def strip_section_headers(elf):
    d = elf.d
    keep = 0
    for p in elf.phdrs:
        if p["t"] in (PT_LOAD, PT_DYNAMIC) or p["fsz"]:
            keep = max(keep, p["off"] + p["fsz"])
    # .bwmeta может лежать в последнем LOAD — он уже учтён. Обрезаем хвост.
    if elf.shoff:
        struct.pack_into("<Q", d, 0x28, 0)          # e_shoff
        struct.pack_into("<H", d, 0x3C, 0)          # e_shnum
        struct.pack_into("<H", d, 0x3E, 0)          # e_shstrndx
        if keep < len(d):
            del d[keep:]
    return min(keep, len(d))


def patch(path, key, keep_sections=False):
    with open(path, "rb") as f:
        elf = Elf(path, f.read())
    d = elf.d
    if not elf.secs:
        raise RuntimeError("таблица секций уже вырезана — патчить нечего")

    magic_hits = find_all(bytes(d), MAGIC)
    if len(magic_hits) != 1:
        raise RuntimeError(f".bwmeta: ожидался ровно один маркер, найдено {len(magic_hits)}. "
                           "bw_obf.cpp не попал в сборку или LTO выкинул g_bw_meta")
    meta = magic_hits[0]
    if meta + META_HDR + 16 > len(d):
        raise RuntimeError(".bwmeta обрезана")

    # Страницы с кодом не трогаем ТОЛЬКО в том сегменте, где они правда
    # исполняемые: если .rodata лежит в R-сегменте ( -z separate-code ),
    # mprotect(R->RW) вообще не снимает X ни с одной страницы с кодом.
    # Для R|E-сегмента (старый lld без separate-code) режем 64K-блоками —
    # это покрывает устройства с 4K/16K/64K страницами.
    exec_holes = []
    for s in elf.secs:
        if s["flags"] & SHF_EXECINSTR and s["typ"] != SHT_NOBITS and s["size"]:
            lo = (s["addr"] // GUARD_GRAN) * GUARD_GRAN
            hi = ((s["addr"] + s["size"] + GUARD_GRAN - 1) // GUARD_GRAN) * GUARD_GRAN
            exec_holes.append((lo, hi))
    # Слоты, куда лоадер пишет релокейшены — исключаем всегда.
    reloc_holes = [(v, v + 8) for v in elf.dynamic_reloc_targets()]

    candidates = [s for s in elf.secs
                  if s["name"].startswith(".rodata")
                  and (s["flags"] & SHF_ALLOC)
                  and not (s["flags"] & SHF_WRITE)
                  and not (s["flags"] & SHF_EXECINSTR)
                  and s["typ"] != SHT_NOBITS and s["size"] > 0]
    if not candidates:
        raise RuntimeError("не найдено ни одной строковой .rodata* секции — "
                           "изменился toolchain/линкер, проверь readelf -S")

    def segment_of(va):
        for p in elf.phdrs:
            if p["t"] == PT_LOAD and p["va"] <= va < p["va"] + p["msz"]:
                return p
        return None

    ranges = []
    for s in candidates:
        base = s["addr"]
        seg = segment_of(base)
        holes = list(reloc_holes)
        if seg and (seg["fl"] & 0x1):  # PF_X
            holes += exec_holes
        pieces = subtract([(base, base + s["size"])], holes)
        for va, ve in pieces:
            fo = s["off"] + (va - base)
            ranges.append((va, fo, ve - va))
    ranges.sort()
    if len(ranges) > MAX_RANGES:
        # Сливаем соседние: между ними нет ни кода, ни релокейшенов.
        merged = []
        for va, fo, sz in ranges:
            if merged and merged[-1][0] + merged[-1][2] == va and \
               merged[-1][1] + merged[-1][2] == fo:
                pva, pfo, psz = merged[-1]
                merged[-1] = (pva, pfo, psz + sz)
            else:
                merged.append((va, fo, sz))
        ranges = merged
    if len(ranges) > MAX_RANGES:
        raise RuntimeError(f"слишком много диапазонов ({len(ranges)} > {MAX_RANGES})")
    if meta + META_HDR + len(ranges) * 16 > len(d):
        raise RuntimeError(f".bwmeta не вмещает {len(ranges)} диапазонов")

    candidate_total = sum(s["size"] for s in candidates)
    mask = bytearray(len(d))
    total = 0
    for va, fo, sz in ranges:
        for i in range(sz):
            d[fo + i] ^= stream_byte(va + i, key)
        mask[fo:fo + sz] = b"\x01" * sz
        total += sz

    # Если lld положил .rodata в один R|E-сегмент вместе с .text, 64K-блоки
    # вокруг кода исключены — это законная дыра, но в ней не должно остаться
    # ни одной читаемой строки. Проверяем ВСЕ незашифрованные байты
    # кандидатов, а не только список контрольных строк.
    leaks = []
    for s in candidates:
        base, n = s["off"], s["size"]
        i = 0
        while i < n:
            if mask[base + i]:
                i += 1
                continue
            j = i
            while j < n and not mask[base + j]:
                c = d[base + j]
                if not (0x20 <= c < 0x7F or c >= 0xC0):
                    break
                j += 1
            if j - i >= 8 and j < n and d[base + j] == 0:
                leaks.append((base + i, bytes(d[base + i:base + j])))
            i = max(j, i + 1)
    if leaks:
        for off, text in leaks[:8]:
            print(f"  FAIL  незашифрованная строка {off:#x}: {text[:48]!r}",
                  file=sys.stderr)
        raise RuntimeError(f"в .rodata остались открытые строки: {len(leaks)}. "
                           "Нужно -Wl,--rosegment, чтобы строки не делили "
                           "страницу с кодом")

    # Метаданные: count + пары (vaddr, size).
    struct.pack_into("<Q", d, meta + 16, len(ranges))
    for i, (va, _fo, sz) in enumerate(ranges):
        struct.pack_into("<QQ", d, meta + META_HDR + i * 16, va, sz)

    print(f"obf: секций .rodata* = {len(candidates)}, диапазонов = {len(ranges)}, "
          f"зашифровано = {total}/{candidate_total} байт, ключ = {key:#018x}")
    if total < candidate_total:
        print(f"obf: WARN пропущено {candidate_total - total} байт (страницы с кодом/"
              "relocations), проверка на открытые строки ниже")
    print("obf: проверка контрольных строк:")
    canary_report(elf, strict=True)

    if not keep_sections:
        end = strip_section_headers(elf)
        print(f"obf: таблица секций вырезана, файл обрезан до {end:#x}")

    with open(path, "wb") as f:
        f.write(d)
    return 0


def check(path):
    with open(path, "rb") as f:
        elf = Elf(path, f.read())
    print(f"obf: проверка {os.path.basename(path)} ({len(elf.d)} байт)")
    # После вырезания таблицы секций атрибуцию сделать негде: совпадение в
    # .text-immediates нельзя отличить от утечки. Строгая проверка уже прошла
    # в patch() до обрезки, здесь только предупреждаем.
    strict = bool(elf.secs)
    n = canary_report(elf, strict=strict)
    stripped = elf.shoff == 0 and elf.shnum == 0
    print(f"obf: section headers {'вырезаны' if stripped else 'ЕСТЬ (внимание)'} "
          f"| контрольных строк: {n} ({'строго' if strict else 'по файлу, без атрибуции'})")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", help="ELF-бинарник")
    ap.add_argument("--key", default=None, help="64-битный ключ сборки (0x...)")
    ap.add_argument("--check", action="store_true", help="только проверить результат")
    ap.add_argument("--keep-sections", action="store_true",
                    help="не вырезать таблицу секций (для отладки)")
    args = ap.parse_args()
    if not args.path:
        ap.error("нужен путь к ELF")
    try:
        if args.check:
            return check(args.path)
        if args.key is None:
            ap.error("--key обязателен при патче")
        key = int(args.key, 16) & MASK
        return patch(args.path, key, args.keep_sections)
    except (RuntimeError, OSError, struct.error) as e:
        print(f"obf: ОШИБКА: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
