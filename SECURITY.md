# Security Policy

## Reporting a vulnerability

Please report security issues privately rather than in a public issue.

Use GitHub's private vulnerability reporting on this repository
(Security -> Report a vulnerability), or mail the maintainers listed in
`CONTRIBUTING.md` with `SECURITY` in the subject.

Please include:

- what the issue is and which component it affects
- a file, capture or set of steps that reproduces it
- which version or commit you tested

We will confirm receipt, agree a disclosure timeline with you, and credit you in
the release notes unless you would rather stay anonymous.

## Scope

Subsurface is a desktop and mobile application, so the interesting attack
surface is the input it accepts from somewhere other than the person using it:

- **Dive log and dive computer files.** Everything under `core/parse-*.cpp`,
  `core/import-*.cpp`, `core/datatrak.cpp`, `core/liquivision.cpp`,
  `core/cochran.cpp`, `core/uemis*.cpp`, `core/ostctools.cpp` and
  `smtk-import/`. These parse files that users routinely receive from other
  divers, dive shops and web sites. This is the highest value area to look at.
- **Dive computer traffic.** USB, serial and Bluetooth LE input
  (`core/qt-ble.cpp`, `core/qtserialbluetooth.cpp`,
  `core/libdivecomputer.cpp`). A device is not a trusted peer.
- **Network paths.** Cloud storage sync (`core/git-access.cpp`), the
  divelogs.de and dive-share uploads, and the update check.
- **Credential handling.** `core/settings/qPref*`.

Out of scope: anything that requires the attacker to already be able to run code
as the user, and denial of service that needs a file the user would have to go
well out of their way to open.

## Testing for these yourself

The parsers can be fuzzed directly:

```
cmake -S . -B build-fuzz -G Ninja \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DBUILD_WITH_QT6=ON -DSUBSURFACE_FUZZ_BUILD=ON
cmake --build build-fuzz --target fuzz-parse-xml fuzz-liquivision \
      fuzz-datatrak fuzz-cochran fuzz-ostctools
./build-fuzz/tests/fuzz/fuzz-liquivision corpus/ -max_len=65536
```

The test suite can be run under AddressSanitizer and UndefinedBehaviorSanitizer
with `-DSUBSURFACE_ASAN_BUILD=ON`. Both run in CI - see
`.github/workflows/linux-fuzz-asan.yml`.
