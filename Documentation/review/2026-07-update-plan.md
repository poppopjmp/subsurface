# Subsurface Update Plan - July 2026

Companion to `2026-07-software-review.md`. That document records what is wrong;
this one records what to do, in what order, and why that order.

Finding IDs (`S1`, `C4`, `U2`, `M1`, `H1` ...) refer to the review.

## Status

Phase 0 and most of Phase 1 have been implemented and verified against a real
Qt 6.4.2 build on Ubuntu 24.04 LTS: the tree configures, builds all 65 targets
with zero errors, and `ctest` reports **32/32 passing**.

Three things that only came out of actually building and running, rather than
reading:

- A **fourth** Qt6 blocker: `FindLIBGIT2.cmake` preferred `libgit2.a` over the
  shared library and then could not link it (see review 6.3).
- `tests/testdiveplannermodel.cpp` only compiled when `MAP_SUPPORT` was defined,
  because that is the only path that pulled in `core/dive.h`. It fails on any
  distro without QtLocation - which is exactly the LTS case the new CI leg
  covers.
- Un-stubbing `TestQML` (item 0.10) turned a permanently green no-op into
  **47 real assertions** and exposed six genuine failures behind it, including a
  real defect in `qPrefCloudStorage::set_cloud_base_url()` that silently
  discarded the first change of the cloud URL.

The remaining phases below are unchanged.

Diver-facing work - the plan-versus-dive comparison, and the planner defects that
turned up while building it - is recorded separately in
`2026-08-diver-workflow.md`, together with what is left of it.

---

## 1. Guiding decisions

Four judgements shape the whole plan. Each is a decision to confirm, not an
assumption to inherit.

### D1. Decide what this repository is for

`H1`: this tree has **zero divergence** from upstream
`Subsurface-divelog/subsurface`. It is a mirror. Every item below is therefore
either (a) work to be contributed upstream, or (b) work that deliberately forks.
Nothing else in this plan can be sequenced sensibly until that is settled.

Recommended: treat it as an upstream-tracking mirror, land fixes as upstream
pull requests, and add a scheduled ahead/behind check so drift is visible.
Fork only if there is a specific product reason, and document it in `FORK.md`.

### D2. Fix the parser class, do not patch the parser instances

Four critical and roughly eight high memory-safety defects
(`S2`–`S4`, `S8`–`S12`, `C1`, `S21`, `S26`) share one root cause: a raw
`unsigned char *` cursor plus a length read from the file, with the bound
checked after the access, or not at all. Patching them individually leaves the
next importer to reintroduce the same shape. The plan therefore treats a
bounds-checked cursor plus fuzzing as the actual deliverable, with individual
patches as interim triage.

### D3. Declare a Qt floor before doing anything else with Qt

Nothing currently states which Qt versions Subsurface supports. The code needs
Qt 6.7+ (section 2.2 of the review), CI tests only 6.8+, `INSTALL.md` documents
Qt 5.9, and `find_package(Qt6 ...)` passes no version at all. Pick a floor,
enforce it in CMake, and test it in CI. Everything else about Qt6 follows.

### D4. Do not de-scope mobile

Mobile was competently modernised in Feb–Apr 2026 and is current, not
legacy-broken. It has one hard external deadline (`M1`) and one real security
gap (`S13`). Maintain it; do not expand scope into the `QMLManager` refactor
unless someone commits to ongoing mobile ownership.

---

## 2. Phase 0 — Immediate (this week)

Small, independently landable, low risk. Nothing here depends on anything else.

| # | Action | Finding | Effort |
|---|---|---|---|
| 0.1 | Bump `targetSdkVersion` to 36 in `android-mobile/build.gradle:63` and SDK levels in `scripts/docker/android-build-container/Dockerfile:6-7`; run a full `android.yml` build | `M1` | S |
| 0.2 | Remove the hostname exemption in `certificate_check_cb` (`core/git-access.cpp:297-315`) | `S1` | S |
| 0.3 | Bound `next_o2_sensor` before the `MATCH` at `core/parse-xml.cpp:970` | `S2` | S |
| 0.4 | Check `uchar_find` for `npos` and require `size >= ASD_SAMPLES` at `core/import-asd.cpp:514`; change `build_dc_data`'s `int max` to `size_t` | `S4` | S |
| 0.5 | Redact secrets before logging: `core/git-access.cpp:692,811`, `mobile-widgets/qmlmanager.cpp:735,2251` | `S14` | S |
| 0.6 | Fix `to_utf8()` off-by-one (`core/datatrak.cpp:84` — delete the redundant write); add `CHECK(membuf, profile_length)` before the DataTrak `memcpy` | `S9`, `S10` | S |
| 0.7 | Fix `prf_size` truncation in `core/import-logtrak.cpp:541` and give `lt_convert_profile` an output-size parameter | `C1` | S |
| 0.8 | Switch `scripts/get-dep-lib.sh:208` from `git://` to `https://` | `S5` | S |
| 0.9 | SHA-pin every third-party action holding secrets; add an `author_association` gate to `claude-review.yml` and drop `id-token: write` if OIDC is unused | `S18`, `S19` | S |
| 0.10 | Fix `TestQML`: delete the `#ifndef THIS_IS_REPAIRED` branch and make it run, or delete the target. A test that always reports green is worse than no test | 5.1 | S |
| 0.11 | Fix `FILE_COMPARE` to assert equal line counts (`tests/testparse.cpp:22-36`) | 5.3 | S |
| 0.12 | Fix the ASan flag bug: `CMakeLists.txt:133` must read `CMAKE_C_FLAGS`, and add `-fsanitize=address` to linker flags | 6.3 | S |

**Phase 0 gate:** all of the above merged; `make check` still green.

Note on 0.10 and 0.11: these are prerequisites for trusting any later result.
Until they land, "the test suite passes" is not a meaningful statement.

---

## 3. Phase 1 — Make the build honest (2–4 weeks)

Goal: a contributor on a current LTS distro can build Subsurface with Qt6, and
CI proves it.

### 1.1 Declare and enforce a Qt floor  `[D3]`

Decide between:

- **Qt 6.7 floor** — matches what the code already requires. Excludes Ubuntu
  24.04 LTS (6.4) and Debian bookworm (6.4). Cheapest.
- **Qt 6.5 LTS floor** — covers substantially more of the installed base but
  requires guarding the seven `setTimeZone` sites and fixing the two build
  blockers below.

Recommended: **Qt 6.5 LTS**, because shipping a desktop application that cannot
be built on the current Ubuntu LTS is a real distribution problem, and the cost
is small and bounded (three fixes, all identified).

Then add the version to all four `find_package(Qt6 ...)` calls
(`CMakeLists.txt:536,539,541,550`) so a too-old Qt fails at configure time
rather than mis-branching the eleven `QT_VERSION_CHECK(6, 9, 0)` guards.

### 1.2 Fix the three Qt 6.4/6.5 build blockers

All three were reproduced during this review; fixes are known and small.

1. **`translations/CMakeLists.txt:59`** — `qt_add_translations()` is called
   before its target exists. Either move `add_subdirectory(translations)`
   (`CMakeLists.txt:583`) after target creation, or drive `lrelease` directly
   with `add_custom_command`, which has no target dependency and behaves
   identically on every Qt6 release. The second approach was verified to
   configure cleanly on Qt 6.4.2 during this review; note that a faithful fix
   must preserve the `PLURALS_TS_FILE` and `SOURCES` handling that
   `qt_add_translations` provides.
2. **Seven `setTimeZone` sites** — guard with
   `#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)` and a `setTimeSpec` fallback,
   and add the missing `#include <QTimeZone>`.
3. **`core/subsurface-qt/divelistnotifier.h:134`** — include `core/divesite.h`
   so `dive_site` is complete for the generated metatype array.

### 1.3 Add an LTS Qt6 leg to CI  `[the gate that keeps 1.2 fixed]`

Add Ubuntu 24.04 / Qt 6 and Debian bookworm / Qt 6 to
`linux-debian-ubuntu-matrix.yml`. Without this, 1.2 regresses within a release
cycle — the current matrix (Ubuntu 25.10, Debian trixie, Fedora 42) cannot
detect any of it.

### 1.4 Run the tests everywhere they can run

- Fix `windows-msvc-qt6.yml:316`: `-DMAKE_TESTS=OFF` is not a real option
  (`BUILD_TESTS`, `CMakeLists.txt:60`). Set `-DBUILD_TESTS=ON` and run `ctest`.
- Enable `-build-tests` + `make check` on at least one `mac.yml` matrix leg.

### 1.5 Point CodeQL at the shipping configuration

`.github/workflows/codeql-analysis.yml:87` currently analyses Qt5+WebKit, so the
54 Qt6-only code paths are never scanned. Switch to `-build-with-qt6` with Qt6
packages, and fix `MAKEFLAGS: "j…"` → `"-j…"` at `:82`. Expect a batch of new
alerts; that is the finding, not a regression.

### 1.6 Free build-performance wins

`set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` (unblocks clangd and clang-tidy);
opt-in `CMAKE_CXX_COMPILER_LAUNCHER=ccache`; ccache + `actions/cache` across CI
(copy the pattern from `linux-snap.yml:45-74`); remove `-O2` from
`CMAKE_*_FLAGS_DEBUG` (`CMakeLists.txt:147-153`); fix the missing
`PLATFORM=$(uname)` in `scripts/mobilecomponents.sh:51`.

### 1.7 Delete dead build code

`.lgtm.yml` and `CMakeLists.txt:576-579`; the duplicate `.ui` glob
(`desktop-widgets/CMakeLists.txt:2-7`); the dead libssh2 version window
(`HandleFindGit2.cmake:48`); `linux-debian-trixie-5.15.yml.disabled`;
`scripts/travis-wait.sh`; the two unreferenced iOS toolchain files;
the `android/` directory (`M2`).

**Phase 1 gate:** a clean checkout builds with Qt6 on Ubuntu 24.04 LTS; CI
proves it; tests run on Linux, macOS and Windows; CodeQL scans the Qt6 path.

---

## 4. Phase 2 — Make untrusted input safe (1–2 months)  `[D2]`

This is the highest-value phase and the one most likely to be under-scoped.

### 2.1 Stand up fuzzing first, not last

Before rewriting anything, build harnesses over `parse_xml_buffer`,
`try_to_open_liquivision`, `datatrak_import`, `scubapro_asd_import`,
`try_to_open_cochran`, `ostctools_import` and `convert_base64`. The ASan
plumbing already exists (`CMakeLists.txt:35,130-133`) and is fixed by 0.12.

Doing this first means the rewrite in 2.2 is *verified* rather than asserted,
and every finding in section 3 of the review becomes a regression test.

Add an ASan+UBSan CI job running `make check`. `_GLIBCXX_ASSERTIONS` alone would
have turned `C1` into a clean abort.

### 2.2 Introduce a checked cursor and convert the importers

A `std::span<const std::byte>` reader with `read_u8/u16/u32/bytes(n)` returning
`std::optional`, or throwing a `parse_error`. Convert in this order, worst
first:

1. `core/liquivision.cpp` (`S3`)
2. `core/import-logtrak.cpp` (`C1`)
3. `core/datatrak.cpp` (`S8`, `S9`, `S10`)
4. `core/import-asd.cpp` (`S4`)
5. `core/cochran.cpp` (`S21`)
6. `core/uemis.cpp`, `core/uemis-downloader.cpp` (`S12`, `S28`)
7. `core/ostctools.cpp`, `core/exif.cpp`, `core/import-csv.cpp` (`S26`)

This collapses roughly half the confirmed findings into one change.

### 2.3 Validate before dispatch

Add magic-byte and minimum-size validation before the extension-based dispatch
at `core/file.cpp:225-253`, so a renamed file cannot reach an unrelated parser.

### 2.4 Set an explicit XML/XSLT security policy

Drop `XML_PARSE_HUGE` and add `XML_PARSE_NONET` at `core/parse-xml.cpp:1752`;
install `xsltSecurityPrefs` denying file read/write and network on all
`xsltApplyStylesheet` sites; scope the process-wide `xsltMaxDepth`/`xsltMaxVars`
overrides (`core/import-csv.cpp:375-379`) to save/restore. (`S15`)

### 2.5 Bound resource consumption

Cap zip decompression and move `core/file.cpp:79-91` to `size_t`; add the
missing `close(fd)` in `readfile` and the missing `zip_fclose` on the `continue`
path, plus the NULL check on `zip_get_name` (`S16`, `S17`).

### 2.6 Bound SQLite blob indexing

Move `core/import-suunto.cpp` off `sqlite3_exec` to prepared statements so
`sqlite3_column_bytes()` is available (`S11`).

**Phase 2 gate:** fuzzers run in CI without findings for a sustained period;
ASan+UBSan `make check` is green; the seven converted importers have regression
tests built from the fuzz corpus.

---

## 5. Phase 3 — Credentials, transport and supply chain (parallel with Phase 2)

Independent of Phase 2 and can run concurrently with different owners.

| # | Action | Finding | Effort |
|---|---|---|---|
| 3.1 | Move cloud, divelogs.de and proxy passwords to QtKeychain; keep only a handle in `QSettings`. Longer term, move the backend to revocable bearer tokens | `S13` | M |
| 3.2 | Stop forcing `set_save_password_local(true)` at `subsurface-mobile-main.cpp:87-88`; set `android:allowBackup="false"` or scope `fullBackupContent` | `S13` | S |
| 3.3 | HTTPS everywhere: `updatemanager.cpp:44` (plus `toHtmlEscaped()` and host validation), `uploadDiveShare.cpp:36-39`, `checkcloudconnection.cpp:26-28`. Then narrow or remove `cleartextTrafficPermitted` in `network_security_config.xml` | `S24`, `S25` | S |
| 3.4 | Scheme and MIME allowlists for `QDesktopServices::openUrl` and remote picture fetches; gate remote fetch behind a preference | `S22`, `S23` | S |
| 3.5 | Add `sha256sum -c` to `curl_download_library` and the macOS/Android download paths; add `--fail` to bare `curl -O` | `S6` | S |
| 3.6 | Replace the three floating dependency refs (`qt-android-cmake`, `hidapi`, googlemaps) with SHAs | 6.6 | S |
| 3.7 | Bump EOL dependencies: OpenSSL 1.1.1w → 3.x (Android already runs 3.4.4), SQLite 3.19.2 → current, curl 8.4.0 → current, libzip 1.5.1 → 1.10+ (which also deletes three per-platform `sed` hacks) | `S7` | M |
| 3.8 | Reconcile the two divergent QLiteHtml pins so Windows ships the same rendering code as Linux and macOS | 6.6 | S |
| 3.9 | Treat BLE/USB as hostile: bound `receivedPackets`, guard the `hw_credit` decrement, check `descriptors().isEmpty()`, fix `get_name`'s `strncpy` | `S27` | M |
| 3.10 | Add `SECURITY.md` with a disclosure contact; enable `_FORTIFY_SOURCE`, `-fstack-protector-strong`, full RELRO and PIE | — | S |

---

## 6. Phase 4 — Retire Qt5 (1–2 quarters)

Do not start this before Phase 1 is complete. Each step is independently
releasable and should ship separately; the CMake cleanup is trivial once no
consumer remains.

Order matters, because the blockers are release-pipeline blockers, not code
blockers:

1. ~~Port the two `QDesktopWidget` call sites to `QScreen`.~~ **Already done.**
   Both are guarded with `#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)` and have
   `QScreen` equivalents; the review was wrong to list this as a blocker, and the
   Qt6 build compiling confirms it.
2. **Bring QLiteHtml to parity with QtWebKit** for printing and the user manual
   (`desktop-widgets/printer.cpp`, `usermanual.cpp`), then delete the `#else`
   WebKit branch. This is the single thing keeping QtWebKit alive.
3. **Make QtLocation non-optional on Qt6** (`CMakeLists.txt:286-301`) and pin
   the googlemaps plugin. Today its absence is a warning and the app silently
   ships without maps.
4. **Make the Qt6/MSVC build the Windows release artifact** and retire MXE. Fix
   the impossible Qt kit globs at `packaging/windows-msvc/build.ps1:149-158` and
   the dead `&& false` publish gate at `windows-msvc-qt6.yml:414` first.
5. **Move the AppImage off Ubuntu 20.04** to a Qt6 base.
6. **Move Snap to core24 + Qt6**, which also deletes the 21-header private-header
   scrape at `snapcraft.yaml:74,85`.
7. **Update distro packaging**: `debian/control` and `subsurface.spec` to Qt6;
   drop `qtscript5-dev`, `qt5-qtwebkit-devel` and the unused
   `qt5-qtbase-{mysql,postgresql,ibase,odbc,tds}`; refresh the 2019 changelog and
   the 2016 `Standards-Version`.
8. **Flip `scripts/build.sh` to Qt6 by default** and remove `BUILD_WITH_QT6`.

Only then delete: `Qt5WebKitWidgets` support (`CMakeLists.txt:353-368`),
`MapWidgetQt5.qml`, the 18 `QT_VERSION_CHECK(5,x,0)` guards, the 31
`USINGQT6`/`QT5OR6` CMake branch points, and
`smtk-import/CMakeLists.txt:38-58`.

---

## 7. Phase 5 — Structural quality (ongoing, opportunistic)

Real value, no deadline. Sequence behind Phases 1–3.

| # | Action | Finding | Effort |
|---|---|---|---|
| 5.1 | RAII wrappers for libgit2, libxml2, libzip and fds; a `giterr_msg()` helper that NULL-checks once instead of at 20 sites | `C5` | M |
| 5.2 | Replace the global error channel with a returned error type (`std::expected`), starting at the importer entry points, so ignoring a failure is a `[[nodiscard]]` warning | `C2` | M |
| 5.3 | Make `import_thread_cancelled` a `std::atomic<bool>`; replace `lock_planner()`/`unlock_planner()` with `QMutexLocker`; change `unregister_dive(int)` to `size_t` and make `removeDive` return early on `npos` | `C4`, `C6` | S |
| 5.4 | Add `-Wextra`, `-Wconversion`, `_GLIBCXX_ASSERTIONS`; remove the blanket `-Wno-inconsistent-missing-override`, which hides exactly the virtual-signature drift a Qt5→Qt6 port produces | 6.3 | M |
| 5.5 | Convert the dependency layer to imported targets (`PkgConfig::`), then delete the 14 global `include_directories()` and the `SUBSURFACE_LINK_LIBRARIES` list; give each component `PUBLIC`/`PRIVATE` usage requirements | 6.2 | L |
| 5.6 | Test the `commands/` undo/redo layer — 3,698 lines of direct user-data mutation with no coverage. Highest-value test gap in the tree | 5.5 | M |
| 5.7 | Make `TestGitStorage` hermetic against a local bare repo and actually run it, closing the 3,200-line git sync gap | 5.2 | M |
| 5.8 | Restore the commented-out `compareDecoTime` assertions and convert the `printf`-only first-ceiling checks into real assertions | 5.4 | S |
| 5.9 | Move file load, import and cloud sync off the UI thread, replacing the `processEvents()` pump | `U1` | L |
| 5.10 | Fix `setProgressMessage` to use `invokeMethod` like its sibling `showError` | `M3` | S |
| 5.11 | Centralise the stray hardcoded colours into the existing theme system; wrap the untranslated `importgps.cpp` strings in `tr()` | `U2`, `U3` | S |
| 5.12 | Modernise the 114 string-based `SIGNAL`/`SLOT` connects, prioritising `mainwindow.cpp`, `configuredivecomputerdialog.cpp`, `divelistview.cpp` | 7 | M |
| 5.13 | Raise `cmake_minimum_required` to 3.21+, drop `CMP0071 OLD`, add `CMakePresets.json`, enable precompiled headers | 6.3 | M |
| 5.14 | Fix `map-widget`'s source-tree write; stop compiling the absolute source path into binaries | 6.3 | S |

---

## 8. Phase 6 — Process and release engineering

| # | Action | Finding | Effort |
|---|---|---|---|
| 6.1 | Document the fork's status and intent, and add a scheduled upstream ahead/behind check that opens an issue on drift | `H1`, `D1` | S |
| 6.2 | Document in `README.md`/`INSTALL.md` that tag-based releases were deliberately replaced by CI/CD rolling releases in 2022 | `H2` | S |
| 6.3 | Rewrite `INSTALL.md` around Qt6: real floors, Qt6 package lists, drop QtWebKit instructions, fix the `http://` clone URL and the `Subsurface-DS9`/`Subsurface-NG` submodule branch contradiction | `H5` | S |
| 6.4 | Move macOS signing and notarization into CI with `notarytool` and CI secrets, retiring the personal-keychain scripts; add Windows Authenticode signing | 10, `H3` | L |
| 6.5 | Add SBOM generation and build provenance attestation to release jobs | 10 | M |
| 6.6 | Pin and checksum `linuxdeployqt`; hash-pin `requirements.txt`; pin `subsurface/qt-mac` to a SHA; move `client_payload` and `ANDROID_KEYSTORE_*` into `env:` | `S20`, 10 | M |
| 6.7 | Consolidate the duplicated Linux workflow boilerplate into the existing reusable workflow | 10 | M |
| 6.8 | Add `pull_request` dry-run triggers to `fedora-copr-build.yml` and `ubuntu-launchpad-build.yml` | 10 | S |
| 6.9 | Resolve the two-Windows-pipeline situation: finish the MSVC path or document the transition end date | 10 | M |
| 6.10 | Replace the hand-rolled path-filter reimplementation in `publish-release.yml:116-140` | 10 | M |
| 6.11 | Document a second release-capable maintainer with CI publish and Transifex access | `H3` | S |
| 6.12 | Adopt code coverage reporting, even non-blocking, so gaps stay visible | 5.6 | M |
| 6.13 | Triage `TODO.CCR` (2014) and the `divetripmodel.cpp` TODOs (2017–2020) into tracked issues or delete them | `H5` | S |

---

## 9. Sequencing summary

```
Phase 0  ──┬─────────────────────────────────────────────  (week 1)
           │
Phase 1  ──┴──┬──────────────────────────────────────────  (weeks 2-5)
              │   build honest + LTS Qt6 in CI
              │
        ┌─────┴──────────────┐
        │                    │
Phase 2 │  parsers + fuzzing │  Phase 3  credentials/transport/supply chain
        │  (months 2-3)      │  (months 2-3, parallel, different owner)
        └─────┬──────────────┘
              │
Phase 4  ─────┴──────────────────────────────────────────  (quarters 2-3)
              retire Qt5, release pipeline by release pipeline
              │
Phase 5/6 ────┴──────────────────────────────────────────  (ongoing)
```

**Hard dependencies:**

- Phase 1 before Phase 4 — you cannot retire Qt5 while the Qt6 build is broken
  on LTS and unenforced by CI.
- 2.1 (fuzzing) before 2.2 (rewrite) — otherwise the rewrite is unverified.
- 0.10 and 0.11 before trusting any test result.
- Phase 4 step 2 (QLiteHtml parity) before steps 4–6 — the release pipelines are
  frozen on Qt5 specifically to keep QtWebKit alive.

**Deliberately parallel:** Phases 2 and 3 touch different files and different
skills.

---

## 10. What not to do

Recorded so effort is not spent re-litigating things that are already correct:

- **Do not restructure `commands/`.** It is the best-designed layer in the tree
  and should be the model for the importers.
- **Do not chase header hygiene.** `core/dive.h` is 218 lines; this is not where
  build time goes.
- **Do not "fix" the desktop models.** `divetripmodel.cpp` already uses granular
  row signals, not blanket resets.
- **Do not hunt for `QRegExp`, `qSort`, `QLinkedList`, `QVariant::type()`,
  `fromTime_t` or `QWheelEvent::delta()`.** There are zero occurrences.
- **Do not add TLS-verification workarounds.** The Qt side is already correct;
  `S1` is one isolated libgit2 defect.
- **Do not de-scope mobile.** See `D4`.
- **Do not "fix" the bounds checks listed as already correct** in review section
  3.4 — `add_sample_pressure`, `pop_cstring`, the Cochran dive-offset loop, the
  Uemis suit-index guards, `read_ostc_cf`, and `try_to_xslt_open_csv`'s size
  accounting are all sound.

---

## 11. Suggested first pull requests

Small enough to review properly, valuable enough to be worth landing:

1. `S1` alone — one function, unambiguous security fix.
2. `S2` alone — three lines, prevents an out-of-bounds write.
3. The three Qt 6.4/6.5 build blockers plus the Ubuntu 24.04 Qt6 CI leg, as one
   PR — the fix and the thing that keeps it fixed belong together.
4. `TestQML` and `FILE_COMPARE` (0.10, 0.11) as one PR — both restore meaning to
   an existing green signal.
5. `M1` (targetSdk 36) alone — deadline-driven, mechanical.
