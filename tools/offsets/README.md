# Offset checks

The game dumps are not included in the repository. Extract `dump11995.7z`
(or `dumpbeta11982.7z` for the beta profile) locally and run:

```sh
python3 tools/offsets/verify_release_11995.py /path/to/dump.cs /path/to/script.json
```

`verify_release_11995.py` re-derives every constant of the release profile from
the dump: 397 managed fields (base-class walk), 12 TypeInfo slots from
`script.json`, 74 pinned literals (native layouts / enum values) and 4 aliases.
It also asserts the invariants (PlayerLoopHelper slot = PM slot − 0x38, the
UniTask promise pair 8 bytes apart, TOD_Sky slot inside the scan window) and
exits non-zero on any drift. `--propose` prints the value each constant should have.

Beta:

```sh
python3 tools/offsets/verify_beta_11982.py /path/to/dump.cs /path/to/script.json \
  --libil2cpp /path/to/libil2cpp.so
```

The ELF argument is optional; when present, it also checks the ARM64 network
timing getter instructions and their relocations to the TypeInfo slots. The
checker compares the beta header with named and build-specific obfuscated
fields, TypeInfo slots, and the TOD/layer-mask candidate shapes. It does not
prove the meaning of obfuscated fields, layer-mask values, native Unity layouts,
nor behavior on a device. Re-match those candidates for each new dump.

To compare named fields between two versions:

```sh
python3 tools/offsets/offsets_diff.py old/dump.cs new/dump.cs PlayerManager KCC FPMotion
```

Profile selection and release regression tests run with
`tools/tests/run_all_host_tests.sh` without a dump.
