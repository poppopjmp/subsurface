# Subsurface modernization - August 2026

Branch: `van1sh/modernization`, cut from `master` at `a309fc75b`.

What this is: an audit of the tree against current C++, Qt, CMake, CI and
packaging practice, and the work that came out of it. The audit ran six
independent surveys - C++ and memory safety, CMake, CI and supply chain, test
and quality gates, platform packaging, dependency currency - and then put every
finding through an adversarial verifier whose job was to refute it against the
real files. 58 findings were raised; 44 survived verification, 14 did not.

Everything below is either quoted from the tree or was produced by building and
running it on Ubuntu 24.04 with Qt 6.4.2.

---

## 0. What the audit could not see

The audit agents read the tree; they did not build it. The first thing that
happened when the branch was actually configured was that it did not configure.

`cmake .. && ninja` against Qt 6.4.2 - the Qt that the current Ubuntu LTS ships -
failed at configure time, and then at four further points. None of it is visible
to CI, because every CI leg goes through `scripts/build.sh` with options that a
person typing `cmake` by hand does not pass.

That is the whole audit in miniature: the project builds if you hold it exactly
the way its own scripts hold it.

Fixed first, in `8c8debe50`, because nothing else on this branch can be verified
without it:

1. `qt_add_translations()` called with a target that does not exist yet.
2. `Q_DECLARE_METATYPE(dive_site *)` with only a forward declaration.
3. Five `QDateTimeEdit::setTimeZone()` calls guarded on Qt >= 6.0; it arrived in 6.7.
4. `FindLIBGIT2.cmake` preferring `libgit2.a` over the shared library, then
   failing to link it.
5. `tests/testdiveplannermodel.cpp` getting the complete `dive` type only via the
   `MAP_SUPPORT` includes.

---

## 1. Done on this branch

| Commit | What |
| --- | --- |
| `8c8debe50` | Build on the Qt current distributions ship. Five blockers; tree now configures, builds and passes 32/32. |
| `633e2a9f2` | Make the QML preference tests run, and fix what they found. |
| `da73e9df6` | Stop the binary importers walking off the end of the file, and make the sanitizer build usable. |
| `f4d96e39c` | CI: cancel superseded runs, pin floating actions, keep the release token out of argv. |

### The QML tests were a no-op

`tests/testqml.cpp` began `#ifndef THIS_IS_REPAIRED / return 0;`, and
`THIS_IS_REPAIRED` is defined nowhere in the tree. ctest reported TestQML passing
in 0.02 seconds while running nothing. Behind it were 48 assertions, of which 6
failed and several more asserted nothing:

- `qPrefCloudStorage::set_cloud_base_url()` refused to store the first change away
  from the default, while emitting the change signal and syncing to disk. A user
  moving off the default cloud server was told it had happened and it had not.
- Every `Connections` block used the Qt5 implicit `onFooChanged` syntax, which Qt6
  does not bind. 109 handlers across 11 files.
- `tst_qPrefGeocoding.qml` was commented out end to end and reported two passes.
- Nine preferences had moved to `qPrefMedia`, `qPrefLog` and `qPrefEquipment`;
  the tests still addressed them on their old objects, where assigning to a
  property a QObject does not have quietly creates one on the JavaScript wrapper.
- `PrefUnits.unit_system` is not a `Q_PROPERTY` at all.

Now 60 real assertions, passing, with three new files restoring the coverage that
was lost when those preferences moved.

### The importers had no test and several defects

Seven hand-written binary parsers read a buffer straight off disk from a file the
user picked. None had a test. `tests/testimport.cpp` feeds every one of them an
empty file, truncated files at eighteen lengths, and deterministic garbage.

It found, on the first run:

- **Liquivision**: read the first four bytes before checking there were four -
  an empty file segfaulted on open - then placed the next read using an unchecked
  length from the file, and passed `parse_dives()` a size computed as
  `buf_size - ptr`, which underflows to nearly 4 GB.
- **Logtrak**: two `find()` results going straight into `substr()`, which throws
  `std::out_of_range` on `npos`. Nothing catches it, so opening any file that is
  not a Logtrak dump terminated the application - and Subsurface tries each
  importer in turn, so an unrelated file reached it.
- **Cochran**: the size guard asked for `0x40000` bytes while the first read is at
  `0x40101`; the decode pointer was formed before the check; `exit(1)` on an
  unrecognised log format, killing the application and any unsaved log; and the
  offset table read through an `unsigned int *` cast over `std::string::data()`.

`SUBSURFACE_ASAN_BUILD` is fixed in the same commit because it is what proves the
rest: it built `CMAKE_C_FLAGS` out of `CMAKE_CXX_FLAGS` and set no linker flags.
There is a matching `SUBSURFACE_UBSAN_BUILD` now.

Proof rather than assertion: with both sanitizers on, `TestImport` is clean; put
the old Cochran guard back and it fails with a heap-buffer-overflow at
`cochran.cpp:825`.

---

## 2. Next, in order

Each is one commit with a way to verify it. The first three are locally provable
on Ubuntu 24.04 / Qt 6.4.2; the rest are labelled with what cannot be proven here.

1. **`FILE_COMPARE` and the other tests that assert nothing.** The macro ignores
   whether either file opened and loops "while both still have lines", so
   truncated or empty output matches anything. `testmerge.cpp` carries a copy.
   `TestParsePerformance` returns early on missing data and ignores every result;
   two VPMB trimix tests have their runtime assertion commented out.
   *(Partly done: `FILE_COMPARE` itself is fixed on this branch.)*
2. **Datatrak string reader.** `to_utf8` writes one byte past the end of its
   buffer; the `read_string` macro has an unchecked `calloc`, a no-op `strcat`
   and a hidden `goto`.
3. **`sscanf` return values in Logtrak and EXIF.** Uninitialised `%m` targets are
   dereferenced and freed; a `std::string` is constructed from a possibly-null
   `%m` result; `epoch()` uses uninitialised ints and a partly-filled `struct tm`.
4. **Translated strings used as printf formats**, plus four raw `sprintf` calls
   into a fixed stack buffer. A translator can change the format specifiers.
5. **A Qt 6.4 CI leg.** Nothing in CI builds at the LTS floor, which is why
   section 0 exists. Add an Ubuntu 24.04 / Qt 6 leg to the qt6 matrix.
6. **CMakePresets.json and `CMAKE_EXPORT_COMPILE_COMMANDS`.** Build configuration
   currently lives only in `scripts/build.sh` and the workflow files, which is
   the direct cause of section 0.
7. **Point CodeQL at what ships.** It currently builds a Qt5 + WebKit
   desktop-only configuration.
8. **Submodule and dependency metadata**: `.gitmodules` pins a branch the
   submodule does not track, libftdi is cloned over unauthenticated `git://`,
   three dependencies track mutable branches, libzip is pinned to a 2018 release.
9. **Packaging**: the AppImage is built on EOL Ubuntu 20.04 with bundled
   OpenSSL 1.1.1; `make install` writes into the source tree; installed file
   names do not match the AppStream component ID. *Cannot be built here.*
10. **CMake usage requirements.** `subsurface_corelib` declares none; every
    dependency is injected globally through `include_directories`,
    `add_definitions` and `link_directories`. Converting the corelib first proves
    whether propagation works before the other fifteen directories follow.
11. **Qt6 as the default build.** `BUILD_WITH_QT6` still defaults to OFF, so an
    unqualified `cmake` run configures Qt5 years after Qt5 went EOL for
    open-source users. One line, but it is a policy decision and needs the Qt 6.4
    CI leg green first.
12. **A libFuzzer harness for the importers**, seeded from the 137 files already
    in `dives/`. Largest single item, and most useful once the known defects
    above are fixed - otherwise the first ten minutes just rediscover them.

---

## 3. Deliberately not doing

- **Reformatting the tree to the regenerated `.clang-format`.** It changes 267 of
  2930 lines in `core/dive.cpp` alone; across the tree that is tens of thousands
  of lines that prevent no defect, destroy `git blame` on a codebase whose history
  is its main documentation, and conflict with every patch in flight. Fixing the
  config and gating only newly changed lines is the part with value.
- **Completing the Qt5 removal.** The 48 branch points are real and the cleanup is
  real, but `smtk-import/CMakeLists.txt` is hard-wired to Qt5 with no Qt6 path,
  and most of the deletions are in macOS, Windows and Android install logic that
  cannot be built here. Flipping the default is the reversible half; the deletion
  needs a maintainer with the platform matrix and a deprecation announcement.
- **Converting every importer to `std::span` in one pass.** The two entry points
  with confirmed defects are worth converting because there is now a test that
  proves them. Reshaping thousands of lines of offset arithmetic in
  `uemis-downloader.cpp`, `import-asd.cpp` and `ostctools.cpp` with no test
  anywhere is the exact condition under which mechanical refactoring introduces
  the bug it was meant to prevent. Let the fuzzer implicate specific functions
  first.
- **A tree-wide `[[nodiscard]]` sweep.** Correct observation, large diff, a wave
  of new warnings on call sites that legitimately ignore results, spread across
  code nobody is otherwise touching. Apply it where a status return is being
  added anyway.
- **Target-scoping all sixteen `CMakeLists.txt` files at once.** Mechanical churn
  through build logic for platforms that cannot be compiled here; a broken usage
  requirement in the Android or MXE path would not surface until a release build
  fails.
- **`timeout-minutes` on every job.** Worth having, but the right number per job
  can only come from CI history, and a number guessed too low kills legitimate
  container builds. The concurrency groups on this branch address the same
  problem - runners tied up by superseded work - without that risk.

---

## 4. Refuted findings

14 of the 58 raised did not survive verification. Recorded because "we checked
and it is fine" is worth as much as a finding:

- `core/sha1.cpp` was reported as having strict-aliasing UB from an
  `unsigned int *` cast. The cast is inside
  `#if defined(__i386__) || defined(__x86_64__) || ...`, with a portable
  byte-shift path in the `#else`. It is also vendored - it is git's block-sha1.
- Debug builds were reported as force-optimized with `-O2`. It is a documented
  default in a `CACHE STRING`, overridable with `-DGCC_OPTIMIZATION_FLAGS=-O0`.
- Android was reported as bundling SQLite 3.19.2 (2017) to parse untrusted user
  files. It does bundle it, but the SQLite import path is inside
  `#if !defined(SUBSURFACE_MOBILE)`, and every platform that does compile that
  code gets SQLite from the system or from vcpkg.
- The profile golden-file test was reported as documenting its own reference file
  as something to regenerate. That is how approval testing works everywhere, and
  that test is the strongest comparison in the suite, not the weakest.
- All 27 workflows were checked for a missing `permissions:` block. All 27 have one.
