# Offset checks

The game dumps are not included in the repository. Extract `dumpbeta11982.7z`
locally and run:

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
