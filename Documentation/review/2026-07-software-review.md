# Subsurface Software Review - July 2026

Review of the Subsurface tree at commit `a309fc7` ("Documentation: Update the
Instructions for Contributing").

This document records **findings**. The remediation sequence lives in
`2026-07-update-plan.md`.

---

## 1. Scope and method

Eight parallel reviews covered: core C++ and data model, security and untrusted
input handling, build system and dependencies, CI/CD and release, test suite and
QA, desktop UI, mobile/QML, and project health.

Every finding below carries a `file:line` reference obtained during this review.
Findings marked **[verified]** were additionally reproduced first-hand, either
by a build performed for this review or by re-reading the cited lines directly.

A real build was attempted to ground the review in observed behaviour rather
than static reading alone:

- Environment: Ubuntu 24.04 LTS, GCC 13.3.0, CMake 3.28.3, Qt 6.4.2
- `libdivecomputer` submodule initialised at `ffb7cab` (2026-07-15) and built
  from source
- All pkg-config dependencies satisfied (libgit2 1.7.2, libxml2 2.9.14,
  libxslt 1.1.39, libzip 1.7.3, sqlite3 3.45.1, libusb 1.0.27, libmtp 1.1.21,
  libraw 0.21.2, bluez 5.72)

### Codebase size

| Area | Files | Lines |
|---|---:|---:|
| `core/` | 231 | 58,603 |
| `desktop-widgets/` | 127 | 23,018 |
| `mobile-widgets/` | 49 | 12,754 |
| `qt-models/` | 46 | 9,669 |
| `tests/` | 84 | 9,712 |
| `stats/` | 42 | 9,149 |
| `profile-widget/` | 34 | 6,127 |
| `commands/` | 20 | 5,890 |
| others (`cli`, `map-widget`, `smtk-import`, `backend-shared`) | 25 | 4,439 |

---

## 2. Headline result: the Qt6 build is broken on every current LTS distro

**[verified - reproduced by building]**

Subsurface's Qt6 build cannot be configured or compiled against Qt 6.4, which is
what Ubuntu 24.04 LTS (supported to 2029) and Debian bookworm ship. Two
independent blockers, then a third class of hard errors:

### 2.1 Configure-time failure

`translations/CMakeLists.txt:59` calls `qt_add_translations()` naming the
`subsurface` target:

```cmake
qt_add_translations(${SUBSURFACE_TARGET} TS_FILES ${TRANSLATION_FILES} ...)
```

but `add_subdirectory(translations)` runs at `CMakeLists.txt:583`, while the
executable target is not created until `CMakeLists.txt:654`/`656`. In Qt 6.4 the
macro calls `set_source_files_properties(... TARGET_DIRECTORY ${target})`
(`Qt6LinguistToolsMacros.cmake:285`), which requires the target to already
exist. Result:

```
CMake Error: set_source_files_properties given non-existent target
for TARGET_DIRECTORY subsurface
```

Qt 6.7 and later tolerate this ordering, which is why it has never been seen.

### 2.2 Unguarded use of a Qt 6.7+ API

`QDateTimeEdit::setTimeZone()` was introduced in Qt 6.7. It is used at seven
sites with no version guard and no `<QTimeZone>` include:

- `desktop-widgets/importgps.cpp:19,20`
- `desktop-widgets/tab-widgets/TabDiveNotes.cpp:33,34`
- `desktop-widgets/simplewidgets.cpp:110`
- `desktop-widgets/filterconstraintwidget.cpp:72,84`

This produces 14 of the 15 build errors observed:

```
error: 'class QTimeEdit' has no member named 'setTimeZone'
error: incomplete type 'QTimeZone' used in nested name specifier
```

The omission is inconsistent rather than deliberate: the tree already guards a
different API with `#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)` at ten sites
(`core/qthelper.cpp:481`, `qt-models/diveplannermodel.cpp:544,1483`,
`desktop-widgets/simplewidgets.cpp:148,162,225` and others).

### 2.3 Incomplete type in a queued signal signature

`core/subsurface-qt/divelistnotifier.h:134` declares

```cpp
void filteredDiveSitesChanged(std::vector<dive_site *> sites);
```

but `core/dive.h:25` only forward-declares `struct dive_site;`. Qt 6.4's
generated metatype array requires the pointee to be complete:

```
error: invalid application of 'sizeof' to incomplete type 'dive_site'
static_assert(sizeof(T), "Type argument of Q_PROPERTY or
              Q_DECLARE_METATYPE(T*) must be fully defined");
```

### 2.4 Why CI cannot catch any of this

`.github/workflows/linux-debian-ubuntu-matrix.yml:19-50` builds Ubuntu 20.04,
22.04, 24.04 and Debian bookworm **with Qt5 only**. Every Qt6 leg targets a
bleeding-edge distro: Ubuntu 25.10, Debian trixie, and Fedora 42
(`linux-fedora-qt6.yml`). No CI job builds Qt6 against an LTS Qt.

Compounding this, `CMakeLists.txt:536`, `:539`, `:541` and `:550` call
`find_package(Qt6 ...)` with **no version argument**, so nothing declares or
enforces a floor. The effective minimum is Qt 6.7; the declared minimum is
nothing.

### 2.5 Qt5 is still the default

`CMakeLists.txt:263-272` requires an explicit opt-in for Qt6:

```cmake
if(BUILD_WITH_QT6)
	set(CHECK_QT6 "Qt6")
endif()
find_package(QT NAMES ${CHECK_QT6} Qt5 REQUIRED COMPONENTS Core Widgets)
```

A plain `cmake -S . -B build` in mid-2026 still resolves to Qt5, which has been
out of open-source support since May 2023.

---

## 3. Security findings

The dominant theme: Subsurface's importers were written assuming cooperative
input from a real dive computer, but the actual threat model is "the user opens
a dive log somebody sent them". File format dispatch is by **extension only**
(`core/file.cpp:225-253`) with no magic-byte or minimum-size validation.

### 3.1 Critical

**S1. TLS certificate validation is unconditionally bypassed for all cloud
hosts. [verified]**

`core/git-access.cpp:297-315`:

```cpp
int certificate_check_cb(git_cert *cert, int valid, const char *host, void *)
{
	if ((same_string(host, CLOUD_HOST_GENERIC) || ... same_string(host, CLOUD_HOST_E2)) &&
			cert->cert_type == GIT_CERT_X509) {
		// ... let's simply always
		// tell the caller that this certificate is valid
		return 0;
	}
	return valid ? 0 : -1;
}
```

The `valid` argument is discarded for the five Subsurface cloud hostnames. This
callback is installed on clone, fetch and push (`core/git-access.cpp:335`,
`:603`, `:732`, `:873`) alongside `credential_https_cb`
(`core/git-access.cpp:282-295`), which supplies the user's cloud password via
`git_cred_userpass_plaintext_new`.

An on-path attacker (hostile Wi-Fi, DNS or ARP spoofing) presenting a
self-signed certificate for a cloud host receives the user's cloud email and
password in HTTP Basic. That same credential authorises account deletion
(`core/cloudstorage.cpp:41-44`).

The in-code justification — that `canReachCloudServer()` already validated the
certificate — does not hold: that check runs on a different connection over a
different stack (Qt QNAM, `core/checkcloudconnection.cpp:34-88`) and is not a
hard gate, since `core/git-access.cpp:715-721` proceeds on failure.
`delete_remote_branch` (`core/git-access.cpp:652,665`) sets credentials but no
certificate override, demonstrating the exemption is not required for libgit2 to
work.

**S2. Out-of-bounds write via repeated `<ppo2>` elements in dive-log XML.
[verified]**

`core/parse-xml.cpp:970-971`:

```cpp
if (MATCH("ppo2.sample", double_to_o2pressure, &sample->o2sensor[state->next_o2_sensor])) {
	state->next_o2_sensor++;
```

`o2sensor` has 7 entries (`core/sample.h:24`, `MAX_O2_SENSORS + 1` where
`MAX_O2_SENSORS` is 6 at `core/sample.h:8`). `next_o2_sensor` is reset per
sample (`core/parse.cpp:358`) and declared at `core/parse.h:81` — those four
lines are its only occurrences in the tree, confirming **no bound check
exists anywhere**.

A `<sample>` containing 8 or more `<ppo2>` children gives an attacker-chosen
2-byte write at a chosen offset, walking through `bearing`, `sensor[]`, `cns`
and `heartbeat` and out of the enclosing `std::vector<sample>` allocation. This
is the only confirmed *write* primitive in the review and is reachable from
ordinary XML dive logs.

**S3. Liquivision `.LVD`: unsigned underflow disables all downstream bounds.
[verified]**

`core/liquivision.cpp:425-440`:

```c
unsigned int len = array_uint32_le(buf);
ptr = 4 + len;
unsigned int dive_count = array_uint32_le(buf + ptr);
...
parse_dives(log_version, buf + ptr, buf_size - ptr, log->dives, log->sites);
```

`len` is a raw uint32 from the file, never compared against `buf_size`. Any
`len` greater than the file size makes `buf + ptr` a wild read and
`buf_size - ptr` underflow to near 4 GB, so `parse_dives` treats a small heap
buffer as enormous. Inside it, the only guard
(`core/liquivision.cpp:278`) is `ptr + sample_count * 4 + 4 > buf_size`, where
`sample_count * 4` itself wraps.

Attacker-controlled string construction follows directly at
`core/liquivision.cpp:178-186`, copying up to 4 GB from a heap pointer into a
`std::string` that becomes a user-visible dive site name — an information
disclosure primitive, not just a crash.

**S4. Scubapro `.asd`: `std::string::npos` truncated and used as a memcpy
length. [verified]**

`core/import-asd.cpp:514-515`:

```cpp
size = uchar_find(tmp, str_seq, 3); // size of DC data
dc_data = build_dc_data(dc_model, dc_fam, tmp.data(), size, &s);
```

`uchar_find` returns `std::size_t` (`core/import-asd.cpp:363`) and yields
`npos` when the 3-byte marker is absent. The result is never checked. In
`build_dc_data` the Smart branches reach `core/import-asd.cpp:280`:

```cpp
memcpy(buffer + LIBDC_SAMPLES_MANTIS, ptr + ASD_SAMPLES, max - ASD_SAMPLES);
```

`ASD_SAMPLES` is 183, so any `max < 183` — trivially arranged by placing the
marker early — makes the length negative and the copy `SIZE_MAX`-scale.

**S5. Unauthenticated `git://` transport for a build dependency.**

`scripts/get-dep-lib.sh:208`:

```sh
git_checkout_library libftdi1 $CURRENT_LIBFTDI git://developer.intra2net.com/libftdi
```

`git://` has no TLS and no server authentication. This is on the Android
dependency path (`scripts/get-dep-lib.sh:135`) and the macOS release path
(`packaging/macosx/build-deps.sh:218`). The SHA pin is applied *after* the
clone, so a network attacker substitutes C source that is then compiled into
shipped binaries.

**S6. No integrity verification on any downloaded dependency.**

`scripts/get-dep-lib.sh:74-89` downloads and untars with no checksum and no
signature. `packaging/macosx/build-deps.sh:237` uses `curl -O` without
`--fail`, so an HTML error page would be passed to `tar`. Same at
`scripts/docker/android-build-container/Dockerfile:33`.

**S7. End-of-life crypto and an eight-year-old SQLite in shipped builds.**

`scripts/get-dep-lib.sh:10` pins `OpenSSL_1_1_1w` (EOL 2023-09-11), which is
what the macOS release links (`packaging/macosx/build-deps.sh:72`) — while
Android already uses 3.4.4 (`:116`), proving the bump is feasible.
`scripts/get-dep-lib.sh:13` pins SQLite `3190200`, fetched from
`https://sqlite.org/2017/` (`:211`). `scripts/get-dep-lib.sh:8` pins curl 8.4.0
(Oct 2023).

### 3.2 High

**S8. DataTrak `.add`: per-dive offsets with zero size validation.**
`core/datatrak.cpp:594-612` computes `offset = 12 + (dcount * 850)` and
`memcpy`s `NOTES_LENGTH` bytes from `runner + offset` without ever consulting
`wl_mem.size()`. The only check (`:679-683`) compares two attacker-controlled
counters against each other. A `.log` claiming 5000 dives plus a 100-byte
`.add` reads ~4.25 MB past the buffer into user-visible notes fields.

**S9. DataTrak: `memcpy` runs before the bound check.** `core/datatrak.cpp:151`
copies `prf_length` bytes; the `JUMP` bound check happens afterwards at
`:553`. The destination is sized correctly, the source is not.

**S10. DataTrak `to_utf8()` one-byte heap overflow.** `core/datatrak.cpp:64-85`
allocates `inlen * 2 + 1` then writes `out_string[j + 1]` where `j` reaches
`2*inlen` — exactly one past the allocation. The write is also redundant, since
`calloc` already zeroed the buffer.

**S11. Suunto DM4/DM5 `.db`: SQLite blobs indexed by a length from a different
column.** `core/import-suunto.cpp:243-257` loops on `duration`/`interval` while
indexing `profileBlob[i]`, with neither blob length nor duration validated. Same
pattern at `:514-521`. Using `sqlite3_column_bytes()` requires moving off
`sqlite3_exec` to prepared statements.

**S12. Uemis: a single device byte drives offsets unclamped.**
`core/uemis.cpp:321-330` reads `dive_template` from `data[115]` and indexes
`116 + 25 * (gasoffset + i)`; at 0xFF this lands ~12.9 KB past the buffer. The
short-blob check at `:121-138` only logs.

**S13. Cloud password stored in plaintext.**
`core/settings/qPrefCloudStorage.cpp:67-77` writes the password through
`qPrefPrivate::propSetValue`, a bare `QSettings::setValue`
(`core/settings/qPrefPrivate.cpp:13-29`). A sweep for
`keychain|qtkeychain|secret_service|CredWrite|obfuscat|encrypt` across `core/`,
`desktop-widgets/`, `mobile-widgets/` and `CMakeLists.txt` returned nothing.
The same applies to the divelogs.de password (`:117-125`) and proxy password
(`core/settings/qPrefProxy.cpp:29`).

On mobile this is made unconditional: `subsurface-mobile-main.cpp:87-88` forces
`qPrefCloudStorage::set_save_password_local(true)` at every startup, overriding
the preference that exists precisely to make this optional. No
`android:allowBackup="false"` is set in `android-mobile/AndroidManifest.xml`, so
the credential file is eligible for Google account auto-backup.

**S14. Secrets written to the app log, which the app offers to email.**
Proxy password at `core/git-access.cpp:692` and `:811`; cloud verification PIN
at `mobile-widgets/qmlmanager.cpp:735`; divelogs.de password at
`mobile-widgets/qmlmanager.cpp:2251` (ungated by `verbose`). On mobile these
persist to `subsurface.log`, which `copyAppLogToClipboard()` and
`createSupportEmail()` (`mobile-widgets/qmlmanager.cpp:523-559`) package for
sharing. A user filing a bug report publishes their credentials.

**S15. `XML_PARSE_HUGE` on all untrusted XML; no XSLT security prefs.**
`core/parse-xml.cpp:1752-1754`, `core/save-xml.cpp:804`,
`core/uploadDiveLogsDE.cpp:170,192`. `XML_PARSE_HUGE` disables libxml2's nesting
depth cap, and `traverse()`/`visit()` (`core/parse-xml.cpp:1699`, `:1630`) are
mutually recursive over the tree — deep nesting exhausts the stack.
`xsltSetSecurityPrefs` appears nowhere in the tree.
`core/uploadDiveLogsDE.cpp:192` applies this to the *server's* response.

**S16. Unbounded zip decompression with `int` size arithmetic.**
`core/file.cpp:79-91` grows via `size = read * 3 / 2` on `int`, overflowing
above ~1.4 GB, with no cap on expansion (zip bomb). `core/file.cpp:106` also
skips `zip_fclose` on the `continue` path and passes a possibly-NULL
`zip_get_name()` result to `strstr`.

**S17. File descriptor leaked on every file read.** `core/file.cpp:47-77`
(`readfile`) has no `close(fd)` on any path. Roughly twelve call sites,
including bulk import and per-picture metadata. Bulk imports exhaust the
process fd limit.

**S18. CI: `claude-review.yml` grants write and OIDC scopes to any commenter.**
`.github/workflows/claude-review.yml:6-28` triggers on `issue_comment`
containing `@claude` with `pull-requests: write`, `issues: write` and
`id-token: write`, and no `author_association` gate. Verified *not* a pwn
request — the checkout has no `ref:` so fork code is never fetched — but any
GitHub user who can comment starts a job holding write-capable credentials.

**S19. CI: high-value secrets passed to actions pinned to mutable refs.**
`linux-snap.yml:42` `canonical/setup-lxd@main` and `:68`
`canonical/actions/build-snap@release` float on branches.
`elgohr/Publish-Docker-Github-Action@v5` receives Docker Hub credentials
(`android-dockerimage.yml:40-44`, `windows-mxe-dockerimage.yml:35-39`), and
`softprops/action-gh-release@v3` receives the `NIGHTLY_BUILDS` PAT in seven
workflows. Only `artifact-links.yml:30` is SHA-pinned. Dependabot already covers
`github-actions` (`.github/dependabot.yml:3-7`), so pinning costs nothing.

**S20. CI: unverified remote binary produces the shipped AppImage.**
`.github/workflows/linux-ubuntu-20.04-qt5-appimage.yml:119-131` curls
`linuxdeployqt-continuous-x86_64.AppImage` (a rolling asset), `chmod a+x`, and
runs it with no checksum — the workflow's own comment already flags this. On
master pushes that job holds `secrets.NIGHTLY_BUILDS`.

### 3.3 Medium (selected)

- **S21.** Cochran `.CAN` reads `decode[0x100]` at file offset `0x40101` behind
  a `mem.size() < 0x40000` guard (`core/cochran.cpp:788-800`).
- **S22.** Dive-log picture filenames are fetched with no scheme allowlist —
  SSRF and tracking-beacon vector (`core/imagedownloader.cpp:133,166-170`).
- **S23.** `QDesktopServices::openUrl` accepts any non-empty scheme
  (`desktop-widgets/simplewidgets.cpp:408-411`), and photo paths are opened with
  no extension check (`desktop-widgets/tab-widgets/TabDivePhotos.cpp:33-37`).
- **S24.** Update check over plaintext HTTP, response interpolated into rich
  text with a live link (`desktop-widgets/updatemanager.cpp:44,69-78,103`).
- **S25.** Dive-share upload sends the full log plus a persistent `X-UID` auth
  token over plaintext HTTP (`core/uploadDiveShare.cpp:36-41`). Cloud server
  selection uses two plaintext HTTP APIs (`core/checkcloudconnection.cpp:26-28`).
  These force `cleartextTrafficPermitted="true"` app-wide on Android
  (`android-mobile/res/xml/network_security_config.xml:3`).
- **S26.** Seabear CSV reads 16 bytes at a `strstr`-derived pointer with no
  remaining-length check (`core/import-csv.cpp:955-966`).
- **S27.** BLE: `descriptors().first()` on a possibly empty list
  (`core/qt-ble.cpp:609-631`); `strncpy` of a BLE-advertised name with no
  terminator (`:928-934`); `hw_credit` unsigned underflow (`:96-107`).
- **S28.** Uemis NULL deref when `logfilenr` does not match
  (`core/uemis-downloader.cpp:927-935`) — every other branch in the same block
  checks first.

### 3.4 Security practices that are already correct

Worth recording so remediation does not "fix" them:

- No TLS bypass on the Qt side. Zero hits for `ignoreSslErrors`,
  `setPeerVerifyMode`, `VerifyNone`, `http.sslVerify`. S1 is an isolated libgit2
  defect, not a pattern.
- Cloud credentials never enter the git remote URL
  (`core/qthelper.cpp:1066-1079`), so they stay out of `.git/config`. Email is
  sanitised to an allowlist (`:1055-1064`). Auth attempts are rate-limited
  (`core/git-access.cpp:240-248`). No CLI option accepts a password.
- XSLT stylesheets cannot be attacker-supplied: `get_stylesheet_doc`
  (`core/qthelper.cpp:206-221`) hard-prefixes `:/xslt/`, and `xsltSetLoaderFunc`
  (`:226`) routes `xsl:import`/`document()` through the same constrained loader.
- No shell execution at runtime anywhere outside `libdivecomputer/`, `tests/`
  and `packaging/`. Both `QProcess` uses take argv lists.
- Image cache filenames are SHA-1 hex of the URL
  (`core/imagedownloader.cpp:73-75`) — no path traversal. Zip import is
  in-memory only, so there is no zip-slip.
- Every active workflow declares an explicit `permissions:` block; no
  `write-all`; `pull_request_target` appears once and that file has no checkout.
  Recent commits (`5dedfcb0a`, `65e201f4f`) show active CI hardening.

---

## 4. Core C++ and data model

The C-to-C++ conversion is **genuinely far along in the data model and stopped
at the importer boundary**. `core/dive.cpp` — 2,930 lines — contains zero
`memcpy`/`malloc`/`free`/`sprintf`. All raw allocation now lives in eight files:
`cochran`, `datatrak`, `import-asd`, `import-logtrak`, `libdivecomputer`,
`membuffer`, `downloadfromdcthread`, `qtserialbluetooth`. That is the good news:
the remaining unsafe surface is small and geographically contained.

Counts across `core/` and `commands/`: 77 `memcpy`, 46 `free`, 6 `calloc`,
5 `sprintf`, 4 `malloc`, 1 `strdup`, 1 `strcat`, 1 `realloc`.

**C1. LogTrak hex decode writes past its output vector.**
`core/import-logtrak.cpp:541-543`:

```cpp
int prf_size = (int)ceil(d.length() / 2);   // integer division: ceil() is a no-op
std::vector<unsigned char> prf_buffer(prf_size);
```

`d.length()/2` truncates *before* `ceil`, so an odd-length hex string yields a
buffer one byte short while `lt_convert_profile` (`:277-279`) writes
`ceil(n/2)` bytes and steps past the NUL terminator. `lt_convert_profile` has no
output-size parameter at all. `prf_buffer[43]` is then written unconditionally
at `:564-567`.

**C2. Error handling: 208 `report_error` sites, 127 of which discard the
result.** `report_error` returns a constant `-1` through a global unsynchronised
`static void (*error_cb)(std::string)` (`core/errorhelper.cpp:34-51`) — a single
global channel with no severity and no way to assert a specific failure
occurred. `core/file.cpp:90` discards the parse result outright, and
`try_to_open_zip` still increments `success++` for an entry that failed to
parse.

**C3. Global mutable state.** 36 non-const globals declared in `core/*.h`. The
heavy hitters: `divelog` (`core/divelog.h:55`) referenced 476 times app-wide
across 52 files in `core/`+`commands/`; `prefs`/`default_prefs`/`git_prefs`
(`core/pref.h:220`) at 1,726 references; `current_dive`/`amount_selected`
(`core/selection.h:12-13`) as raw non-owning globals into the dive table. Note
that `struct divelog` is already a proper movable value type and `DownloadThread`
correctly owns a private one (`core/downloadfromdcthread.h:79`) — the global is
a convenience alias, not an architectural necessity.

**C4. Threading is by convention, not by type.** Only three mutexes exist in
`core/`, and no `std::atomic` anywhere. `import_thread_cancelled`
(`core/libdivecomputer.h:62`) is a plain `int` written from the GUI thread and
polled from the download thread at six sites — a data race, and the compiler may
hoist the poll out of the loop, making cancel unreliable. The planner uses a
173-line hand-locked critical section (`core/profile.cpp:845-855`, locked at
`:876`, unlocked at `:1049`); any exception inside deadlocks the planner
permanently.

**C5. Resource cleanup is entirely manual.** 49 `git_*_free`/`xmlFree`/
`xsltFree`/`sqlite3_free` calls plus 84 `free()`. No `unique_ptr` with custom
deleter for any C library handle. `core/git-access.cpp` has 20 unguarded
`giterr_last()->` dereferences; `git_error_last()` returns NULL when no error is
set.

**C6. `removeDive` warns on an unknown dive and proceeds.**
`commands/command_divelist.cpp:48-54` compares a `size_t` from `get_idx` against
`npos`, warns, then calls `unregister_dive(idx)` which takes an **`int`**
(`core/divelist.h:28`). It is saved only by the `size_t`→`int` narrowing turning
`npos` into `-1`. One signature change from an out-of-bounds access.

**Header hygiene is not a problem** and should not absorb budget: `core/dive.h`
is 218 lines, `divelist.h` 56, `pref.h` 234.

**The `commands/` undo layer is the best code in this review.**
`commands/command_base.h:24-52` documents its ownership invariants with a
diagram, every owning slot is a `std::unique_ptr`, and the backend/command
handoff is explicit. C6 is a type-signature mismatch, not a design flaw. This
layer should be a model for the importers, not a refactor target.

---

## 5. Test suite and QA

### 5.1 The QML test suite is a stub that always passes

`tests/testqml.cpp:8-13`:

```cpp
int main(int argc, char **argv)
{
#ifndef THIS_IS_REPAIRED
	return 0;
```

`THIS_IS_REPAIRED` is defined nowhere in the repository — a tree-wide grep
returns only this line. `main()` unconditionally returns 0 before constructing
the QML engine or loading any of the 12 `tst_qPref*.qml` files, yet it is
registered as a ctest test (`tests/CMakeLists.txt:171`) and reports **Passed**
in every CI run. This launders a real gap in mobile preference coverage as
green.

### 5.2 `TestGitStorage` is registered so it can never run

`tests/CMakeLists.txt:131-133`:

```cmake
# this keeps randomly failing and I don't understand why
# too many false positives, so disabling this test for now
TEST(TestGitStorage testgitstorage.cpp storageconfig)
```

The third argument becomes a CTest `CONFIGURATIONS` property, restricting
execution to `ctest -C storageconfig` — a configuration no `make check` or CI
workflow ever passes. It also requires a live remote server and self-skips via
`QSKIP` rather than failing (`tests/testgitstorage.cpp:64`). Net effect:
`core/load-git.cpp` (1,917 lines) and `core/save-git.cpp` (1,295 lines) — the
cloud sync save/load path — have no automated regression coverage.

### 5.3 The golden-file macro cannot detect truncation

`tests/testparse.cpp:22-36`:

```cpp
while (readin.size() && written.size()) { \
	QCOMPARE(written.takeFirst().trimmed(), readin.takeFirst().trimmed()); \
}
```

The loop stops when *either* list is exhausted and never compares lengths. A
save/export routine that regresses to truncated or empty output still passes.
This macro underlies more than 20 round-trip tests across XML, CSV, TSV, UDDF,
DLF and Suunto JSON — precisely the silent-data-loss bug class that matters most
for a dive log.

### 5.4 Safety-critical assertions are commented out or only printed

`tests/testplan.cpp:583` and `:609`:

```cpp
//QVERIFY(compareDecoTime(dive.dcs[0].duration.seconds, 141u * 60u + 20u, 139u * 60u + 20u));
```

Both sit in the deepest VPM-B scenarios (`testVpmbMetric100m60min`,
`testVpmbMetricMultiLevelAir`), so total plan runtime is unchecked there.
Separately, first-ceiling pressure is computed and `printf`-ed but never
asserted at `:581,607,633,659,691,723,783`. Deco correctness is the most
safety-relevant computation in the codebase.

### 5.5 Coverage gaps ranked by risk

1. **`commands/` undo/redo — effectively 0%.** 3,698 lines that mutate the
   user's dive log in place, with no direct test calls.
2. **Dive computer download — 0%.** `core/configuredivecomputerthreads.cpp`
   (2,257 lines, the second-largest file in `core/`),
   `core/libdivecomputer.cpp` (1,759), `core/uemis-downloader.cpp` (1,457),
   `core/cochran.cpp` (817), `core/qt-ble.cpp` (982).
3. **Git/cloud sync** — see 5.2.
4. **Untested importers**: `import-asd.cpp` (694), `import-logtrak.cpp` (642),
   `datatrak.cpp` (710). XML/CSV/TSV/DM4/DM5/Seabear/DLD/DLF/UDDF/Suunto-JSON
   *are* well covered in `tests/testparse.cpp`.
5. **`stats/` — 0%.** No test references any `stats/` source.
6. **`core/filterconstraint.cpp` (1,112) and `core/exif.cpp` (902) — 0%.**

### 5.6 Missing QA infrastructure

- **No fuzzing**, despite the parsers being the obvious target and the exact
  bug class found throughout section 3.
- **ASan exists but is never used.** `CMakeLists.txt:35,130-133` wires
  `SUBSURFACE_ASAN_BUILD`; no workflow sets it. `_GLIBCXX_ASSERTIONS` alone
  would have turned C1 into a clean abort.
- **No clang-tidy or cppcheck** anywhere. CodeQL is the sole static gate, and it
  analyses the Qt5+WebKit path only (see 6.4).
- **No code coverage measurement** of any kind.
- **Tests run on Linux only.** `windows-msvc-qt6.yml:316` passes
  `-DMAKE_TESTS=OFF`, which is not even the real option name (`BUILD_TESTS`,
  `CMakeLists.txt:60`) — a no-op that reads as deliberate. `mac.yml` runs only a
  Python CLI smoke test. Android and iOS run nothing.

---

## 6. Build system and dependencies

### 6.1 No dependency version floors

Across all CMake files the only version constraints are
`CMakeLists.txt:545` (`Qt5 5.11`, Downloader target only) and a broken libssh2
test. libgit2, libzip, libxml2, libxslt, sqlite3, libusb, libraw, libmtp, bluez
and KF6Kirigami are all found with no minimum. `INSTALL.md:78` claims libgit2
0.26+ but nothing enforces it.

The libssh2 check (`cmake/Modules/HandleFindGit2.cmake:47-50`) can never fire:

```cmake
if (LIBSSH2_VERSION VERSION_LESS "1.7" AND LIBSSH2_VERSION VERSION_GREATER "1.6.1")
```

matches only versions strictly inside (1.6.1, 1.7), while the pinned build is
1.11.1.

### 6.2 CMake is 2013-era style

`cmake/Modules/pkgconfig_helper.cmake:1-7` discards the `PkgConfig::` imported
targets that `pkg_check_modules` can produce and pollutes global directory
scope. Repo-wide: **14 `include_directories()` calls and zero
`target_include_directories()`** for project targets; all 14
`target_link_libraries()` calls use the keyword-less signature. Dependencies
travel in two flat globals, `SUBSURFACE_LINK_LIBRARIES` and `QT_LIBRARIES`,
appended from ~20 places.

### 6.3 Broken and dead build code

- **ASan is broken.** `CMakeLists.txt:133` reads `CMAKE_CXX_FLAGS` when setting
  `CMAKE_C_FLAGS`, so the C build inherits C++-only flags and loses its own, and
  `-fsanitize=address` is doubled. No linker flag is set, so ASan builds may not
  link.
- **Debug builds compile at `-O2`** (`CMakeLists.txt:147-153`), and
  `scripts/build.sh:51` defaults to Debug.
- **LGTM is dead** (shut down Dec 2022) but `.lgtm.yml` and the hack at
  `CMakeLists.txt:576-579` remain.
- **`.ui` files are wrapped twice** from two different lists
  (`desktop-widgets/CMakeLists.txt:2-7` glob vs `:23-64` explicit list, both
  wrapped at `:190-194`). The glob misses the six `tab-widgets/*.ui`.
- **The build writes into the source tree**: `map-widget/CMakeLists.txt:4-18`
  generates `${CMAKE_CURRENT_SOURCE_DIR}/qml/MapWidget.qml` at configure time,
  so a Qt5 configure silently overwrites a Qt6 build's QML.
- **The absolute source path is compiled into every binary**
  (`CMakeLists.txt:84-85`), defeating reproducible builds.
- **macOS packaging suppresses its own errors**: `CMakeLists.txt:919-935` uses
  `ERROR_QUIET` on three speculative copies, and attempts brace expansion inside
  `execute_process` (`{QtQuick,QtLocation,QtPositioning}`), which is a shell
  feature — those deletions are silent no-ops.

### 6.4 CodeQL analyses a configuration that is not shipped

`.github/workflows/codeql-analysis.yml:87` builds with
`-desktop -build-with-webkit` against Qt5 packages (`:55-62`). The 54 code sites
behind `#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)` are never analysed.
`:82` also sets `MAKEFLAGS: "j${{...}}"` — missing the `-`.

### 6.5 No build performance features at all

Zero hits repo-wide for `target_precompile_headers`, `UNITY_BUILD`,
`CMAKE_CXX_COMPILER_LAUNCHER`, `ccache`, `sccache` or
`INTERPROCEDURAL_OPTIMIZATION` in CMake files. `CMAKE_EXPORT_COMPILE_COMMANDS`
is never set, which breaks clangd and clang-tidy out of the box. No
`CMakePresets.json`. ccache appears in exactly one of ~14 build workflows
(`linux-snap.yml:45-74`). Parallelism is hand-rolled in four places, and the
`scripts/mobilecomponents.sh:51` copy tests `${PLATFORM}` without ever setting
it, so Kirigami always builds at 4 jobs.

### 6.6 Reproducibility

The single source of truth is 22 shell variables in `scripts/get-dep-lib.sh`.
Three are not pins: `:19` `qt-android-cmake="master"` (dead code — Qt6 uses
`androiddeployqt`), `:190` `hidapi master`, `:182-186` googlemaps branch. No
`vcpkg.json`, no `CMakePresets.json`, no lockfile.
`packaging/windows-msvc/setup-dependencies.ps1:65-77` lists 12 vcpkg ports with
no versions and no baseline.

QLiteHtml carries **two divergent pins** — `get-dep-lib.sh:22` `c43498332c…`
versus `packaging/windows-msvc/build.ps1:396` `b8f9096eae…` — so Windows ships
different rendering code from Linux and macOS. libzip `rel-1-5-1` (2018)
requires three different per-platform `sed` patch sets.

### 6.7 Qt5 removal blockers

The dual-support surface is 31 CMake branch points across 8 files and 86
`QT_VERSION_CHECK` uses across 44 C++ files (54 of them `(6,0,0)`; 18 are
`(5,x,0)` and become dead the moment Qt5 goes).

Hard blockers, in dependency order:

1. **The Windows release build is MXE/Qt5**, and its container is deliberately
   frozen to keep a dead browser engine alive —
   `scripts/docker/mxe-build-container/Dockerfile:4-6`: *"We need to stick with
   22.04 for now because the latest MXE version ... does not build a working
   version of QtWebKit, which breaks printing"*. The Qt6/MSVC path exists but
   its Qt kit globs (`packaging/windows-msvc/build.ps1:149-158`,
   `C:\Qt\6.8*\msvc2026_64`) reference a kit name Qt does not publish, and its
   publish step is dead code (`windows-msvc-qt6.yml:414`, `&& false`).
2. **The Linux AppImage release is Ubuntu 20.04/Qt5**, a distro with no Qt6
   packages.
3. **Snap is core22/Qt5 end-to-end** and scrapes Qt 5.15.3 private headers from
   GitHub at `snapcraft.yaml:74,85`.
4. **QtLocation is optional on Qt6.** `CMakeLists.txt:286-301` makes it a
   `find_package(... COMPONENTS Location)` with no failure path — if absent,
   `MAP_SUPPORT` is simply undefined and the app ships without maps. Qt5 gets it
   unconditionally (`:311-313`). The googlemaps plugin is unpinned on every
   platform and its absence is a `message(WARNING)`, not an error (`:422`).
5. **QtWebKit → QLiteHtml is functionally complete but operationally fragile**
   (two SHAs, hand-rolled discovery with no version, in-source-tree builds only).
   `desktop-widgets/printer.cpp:26` and `desktop-widgets/usermanual.cpp:79` are
   the remaining consumers.
6. **`scripts/build.sh` and `INSTALL.md` still default to Qt5**
   (`:305-309`, `INSTALL.md:344-345`), contradicting
   `.github/copilot-instructions.md:26`.

### 6.8 Distro packaging metadata is stale

`packaging/ubuntu/debian/changelog:1` reads `4.9.3-1~xenial` — Ubuntu 16.04,
EOL 2021, version from 2019. `debian/control:44` declares
`Standards-Version: 3.9.7` (2016). Both it and `packaging/copr/subsurface.spec`
still require `libqt5webkit5-dev`/`qt5-qtwebkit-devel` and `qtscript5-dev`
(QtScript was removed in Qt 6), and the spec pulls
`qt5-qtbase-{mysql,postgresql,ibase,odbc,tds}` for an app that uses SQLite only.
`packaging/copr/config.copr:6` carries `# expiration date: 2024-05-31`.

---

## 7. Desktop UI

Architecture is better than expected. There is a real Command-pattern boundary —
`Command::` call sites in `desktop-widgets/divelistview.cpp` (14),
`mainwindow.cpp` (15), `TabDiveNotes.cpp` (14) — rather than direct mutation of
`divelog`. `qt-models/divetripmodel.cpp` fires 16 granular
`beginInsertRows`/`beginRemoveRows`/`beginMoveRows` pairs rather than blanket
resets; `reset()` at `:477-486` is explicitly documented as mobile-only. There
is no god object beyond the expected `MainWindow` (1,607 lines).

Deprecated-API debt is **much smaller than a codebase of this vintage suggests**:
zero occurrences of `QRegExp`, `qSort`, `QLinkedList`, `QVariant::type()`,
`endl`, `QDateTime::fromTime_t`, or `QWheelEvent::delta()`. `SkipEmptyParts` is
already version-guarded.

What remains:

| API | Occurrences | Qt6 status |
|---|---:|---|
| `QDesktopWidget` | 2 call sites (`mainwindow.cpp:817-818`, `locationinformation.cpp:629`) | **Removed in Qt6** — hard blocker |
| `QtWebKitWidgets`/`QWebView` | `printer.cpp:26-28`, `usermanual.cpp:79` | Unavailable on Qt6; QLiteHtml path exists but is not at parity |
| String-based `SIGNAL`/`SLOT` connects | 114 across 21 files | Works, but loses compile-time checking (557 modern connects already) |
| `QMouseEvent::pos()` unguarded | 11 sites | Deprecated; 5 sites in `profilewidget2.cpp` are already correctly guarded |

**U1. File load blocks the UI thread with a re-entrant event pump.**
`desktop-widgets/mainwindow.cpp:1335-1368` calls `parse_file()` synchronously in
a loop, with responsiveness provided only by `qApp->processEvents()` at
`mainwindow.cpp:107` inside the progress callback. This risks re-entrant slot
execution and visibly stalls on large logbooks.

**U2. Dark mode inconsistencies.** `profile-widget/profilewidget2.cpp:519`
hardcodes `setBackgroundBrush(QColor("#D7E3EF"))` in `setPlanState()`, bypassing
the `paletteIsDark()` system used correctly at
`desktop-widgets/tab-widgets/maintab.cpp:149-161` and in
`stats/statscolors.cpp`. Also `btdeviceselectiondialog.cpp:185,194` and
`profile-widget/ruleritem.cpp:18,76`.

**U3. Untranslated user-facing strings** at
`desktop-widgets/importgps.cpp:62,71,95,100,105` — the rest of the tree is
consistently `tr()`-wrapped.

---

## 8. Mobile

The mobile stack was **competently modernised in Feb–Apr 2026** and is not
legacy-broken: Qt5→Qt6 and Kirigami 5→6 are done, all QML imports are
unversioned Qt6 style, all 10 `Connections{}` blocks use the new
`function onFoo()` syntax, 16 KB Android page size is already handled
(`.github/workflows/android.yml:98`), and NDK 27.2.12479018 / SDK 35 / AGP 8.5.2
/ Qt 6.10.3 are current.

**M1. Play Store target-API deadline — hard external date.**
`android-mobile/build.gradle:63` sets `targetSdkVersion 35`. Google Play
requires API 36 for new apps and updates from **31 August 2026** (extension to
1 November 2026). That is roughly five weeks from this review.

**M2. `android/` is dead Qt5-era code.** `android/AndroidManifest.xml:9` still
names `org.qtproject.qt5.android.bindings.QtApplication` with
`minSdkVersion="16"` (`:82`); last touched 2017-05-25. The real build uses
`android-mobile/` (`CMakeLists.txt:858`). Pure deletion candidate.

**M3. Cross-thread write to a QML-bound property.**
`mobile-widgets/qmlmanager.cpp:1900-1906` (`setProgressMessage`) writes
`m_progressMessage` and emits directly from the download thread, whereas the
adjacent `showError()` at `:75-81` explicitly uses `invokeMethod` with a comment
about crossing thread boundaries safely. Inconsistent handling of the same
problem.

**M4. `QMLManager` is a god object** — 2,496 + 346 lines, 41 `Q_PROPERTY`,
16 `Q_INVOKABLE`, 116 member functions, spanning cloud login, git sync, dive
field validation, BLE/USB enumeration, Android USB JNI glue, export, firmware
update and log writing.

**M5. `breeze-icons` pinned to a 2020 SHA.** `scripts/get-dep-lib.sh:17`
`4daac191…`, unchanged since 2020-02-23, while Kirigami itself went 5.62 → 5.76
→ 6.23 over the same period.

**M6. iOS has no automated signed-build path.** `.github/workflows/ios.yml`
produces an unsigned bundle; signing requires manual Xcode work via
`packaging/ios/enable-signing.sh`. No CI path yields a TestFlight-ready artifact.

Also relevant: Kirigami is carried as a **fork-by-patch** — 11 local patches in
`mobile-widgets/3rdparty/` applied via `git am` — so every Kirigami bump risks
11 rebases.

---

## 9. Project health

| Metric | Value |
|---|---|
| Total commits (master) | 21,275 |
| Unique authors, all time | 253 |
| Commits, last 12 months | 696 |
| Unique authors, last 12 months | 29 |
| Top 2 authors, last 12 months | 570 / 696 = **81.9%** |
| Top 2 authors, last 3 months | 100 / 126 = **79%** |
| Last version tag | v5.0.10, 2022-10-04 |
| Commits since last tag | ~1,997 |
| Languages translated | 34 |
| Last Transifex sync | 2026-05-05 |
| TODO/FIXME/XXX/HACK | 176 in C/C++ |
| Oldest live TODO | `qt-models/divetripmodel.cpp:371`, 2017-10-02 |
| SPDX header coverage | 571/605 core files (94%) |

**H1. This repository has zero divergence from upstream.**
`git rev-list --left-right --count upstream/master...master` returns `0 0`
against `Subsurface-divelog/subsurface`, and `git merge-base` equals `HEAD` on
both. Across 105 remote branches, the only branch present here and absent
upstream is this review's own working branch. **This is a mirror, not a modified
fork.** Any update plan premised on reconciling fork-specific changes has
nothing to reconcile — the real decision is whether to diverge deliberately or
formalise upstream tracking.

**H2. Absence of tags is a deliberate process change, not abandonment.**
`ReleaseNotes/ReleaseNotes.txt` documents the Subsurface 6 move to
"an automatic, CICD-based release process", backed by `publish-release.yml` and
`notify-release-coordinator.yml`. Velocity confirms it: 172 commits in Feb 2026,
30 in the last 30 days. This is worth documenting in `README.md`, since tag
silence since 2022 reads as dead to anyone doing exactly this kind of audit.

**H3. Bus factor.** Two people account for ~80% of commits over both the last 12
and last 3 months; one person has authored 34.5% of all 21,275 commits. Typical
for a founder-maintained project, but it means the release-capable knowledge —
notably macOS signing, which is tied to one maintainer's personal keychain
(`packaging/macosx/sign:16-19`) — is concentrated.

**H4. Current refactor targets, by churn × size (last 24 months):**
`core/dive.cpp` (2,930 lines, 102 changes), `core/divelist.cpp` (1,270 / 66),
`CMakeLists.txt` (64), `qt-models/diveplannermodel.cpp` (1,663 / 60).

**H5. Documentation drift.** `INSTALL.md:59-64` is titled "Getting Qt5" and
states Qt 5.9.1/5.12 floors, directly contradicted by
`CMakeLists.txt:627-628` (mobile hard-requires KF6/Qt6) and `:91` (C++20).
`INSTALL.md:21` clones over plain `http://`. `INSTALL.md:43` names submodule
branch `Subsurface-DS9` while `.gitmodules` says `Subsurface-NG` and the actual
checkout is `heads/Subsurface-DS9` — three sources, two answers.
`README_TESTING.md` last touched 2019; `TODO.CCR` 2014.

---

## 10. Release and CI

Platform coverage on PRs is genuinely broad — macOS, both Windows pipelines,
Android, iOS, multi-distro Linux and Snap all build on pull requests. The gaps
are elsewhere:

- **Tests run on Linux only** (section 5.6).
- **No code signing in CI.** `packaging/macosx/make-package.sh:324` does
  ad-hoc self-signing (`codesign --sign -`); the real Developer ID scripts
  (`packaging/macosx/sign:16-19`, `resign.sh:39-43`) are never called by CI and
  reference one maintainer by name. Windows has no `signtool`/`osslsigncode`
  anywhere. No notarization, no SBOM, no provenance, no artifact attestation.
- **Two full Windows pipelines run on every push/PR but only one ships**
  (`windows-msvc-qt6.yml:414`, `if: github.event_name == 'push' && false`).
- **Packaging scripts are never PR-tested.** `fedora-copr-build.yml` and
  `ubuntu-launchpad-build.yml` trigger only on push to master/current.
- **Release readiness is a bespoke reimplementation of GitHub's path-filter
  logic** in shell and jq (`publish-release.yml:116-140`). A mismatch either
  publishes early or hangs forever. The build number is an atomic-increment race
  via `git push` against a companion repo across many parallel workflows.
- **EOL runner images**: `ubuntu:20.04` containers (standard support ended
  April 2025) still produce the AppImage release.

---

## 11. Summary of severity counts

| Severity | Count | Concentration |
|---|---:|---|
| Critical | 7 | Untrusted-input parsers (4), TLS bypass (1), supply chain (2) |
| High | 20 | Parsers, credential handling, CI supply chain, test integrity |
| Medium | ~30 | Build system, Qt6 debt, UI threading, network transport |

The two things that matter most, stated plainly:

1. **Opening a dive log file someone sent you is not currently a safe
   operation.** Four critical and roughly eight high-severity memory-safety
   defects sit behind extension-only dispatch, with no fuzzing, no ASan in CI,
   and no bounds-checking abstraction. Individual patches are triage; the cure
   is a checked-cursor rewrite of the eight files that still do raw pointer
   arithmetic.

2. **The Qt6 story is further along than it looks and less usable than it
   looks.** The port itself is largely done and of good quality — but it cannot
   build on any current LTS distro, nothing declares the real Qt floor, CI only
   tests bleeding-edge Qt, and Qt5 is still both the default and the source of
   every shipped release binary on Windows, Linux AppImage and Snap.
