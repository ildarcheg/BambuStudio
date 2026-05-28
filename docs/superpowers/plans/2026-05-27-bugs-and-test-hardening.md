# Bugs and Test Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix two user-visible bugs (non-ASCII metadata XML escaping, thumbnail passthrough after plate compaction) and add test coverage for two known gaps (invariant guard corruption cases, negative-path cover image inputs).

**Architecture:** Four independent phases (A: XML escaping, B: thumbnail compaction, C: invariant guard corruption, D: cover negative paths). Phases A and B touch production code with red-then-green TDD; Phases C and D add tests only. No new CLI flags, no new ops abstractions, no changes to existing invariant guard signatures.

**Tech Stack:** C++17, Catch2 v2.x, miniz, boost::filesystem, libslic3r (`store_bbs_3mf`/`load_bbs_3mf`, `Slic3r::Model`).

**Spec:** `docs/superpowers/specs/2026-05-27-bugs-and-test-hardening-design.md`

**Baseline test counts:** 268 cases / 1324 assertions (HEAD `3618319c4`). Final must exceed both.

---

## Build setup (apply once per shell, before any cmake invocation)

```powershell
$vsCmakeBin = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$env:Path = "$vsCmakeBin;C:\Strawberry\perl\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\site\bin;" + $env:Path
$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"
$env:CMAKE_GENERATOR = "Visual Studio 16 2019"
$env:CMAKE_GENERATOR_PLATFORM = "x64"
```

**Build command:** `cmake --build build --target cli_tests --config Release --parallel 2`

**Run command:** `build\tests\cli\Release\cli_tests.exe --order rand` (or filter by tag, e.g. `cli_tests.exe "[non_ascii_metadata]"`).

---

## File structure

```
docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md           (new — Phase A)
tests/cli/CMakeLists.txt                                      (modify — A,B)
tests/cli/roundtrip/test_non_ascii_metadata.cpp               (new — Phase A)
tests/cli/roundtrip/test_thumbnail_compaction.cpp             (new — Phase B)
tests/cli/unit/unit_helpers.{hpp,cpp}                         (modify — Phase C)
tests/cli/unit/test_invariant_cover_references.cpp            (modify — Phase C)
tests/cli/unit/test_invariant_aux_passthrough.cpp             (modify — Phase C)
tests/cli/unit/test_image_signature.cpp                       (modify — Phase D)
tests/cli/unit/test_cover_decoupling.cpp                      (modify — Phase D)
src/libslic3r/Format/bbs_3mf.cpp   OR  src/cli/main.cpp       (modify — Phase A, branch on A.2)
src/cli/io.cpp                                                (modify — Phase B, only if B.1 fails)
```

---

## Phase A — Non-ASCII metadata XML escaping (#1)

> **Amendment 2026-05-27:** Initial Task 1 dispatch showed the programmatic
> round-trip (`description` set directly on `ModelObject`, save, reload)
> passes cleanly. `xml_escape` correctly preserves non-ASCII UTF-8 bytes;
> the writer emits them faithfully; the reader accepts them. The actual
> Phase G bug is in the **CLI argv path on Windows**: PowerShell passes
> arguments in the active code page (windows-1252), the em-dash arrives
> as a single byte `0x97`, gets stored as-is, and expat correctly rejects
> the resulting non-UTF-8 byte on reload.
>
> Task 1 is reshaped: keep the now-green programmatic test as a regression
> guard (commit as green), then add a SUBPROCESS e2e test that pins the
> argv-driven bug (the new red). Task 2 narrows to Branch B (wmain on
> Windows) as the fix.

### Task 1: Programmatic regression guard (green) + argv subprocess pin (red)

**Files:**
- Create: `tests/cli/roundtrip/test_non_ascii_metadata.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

`tests/cli/roundtrip/test_non_ascii_metadata.cpp`:
```cpp
#include <catch2/catch.hpp>
#include "io.hpp"
#include "project_state.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

// Non-ASCII characters that have triggered or could trigger XML/encoding bugs
// in the bbs_3mf writer/reader pair:
//   — em-dash, U+2014, UTF-8 E2 80 94  (the case that surfaced this bug)
//   测试 CJK,        U+6D4B U+8BD5
//   "x" smart quotes,U+201C / U+201D
//   é Latin-1 supp., U+00E9
//
// All metadata writer call sites in bbs_3mf.cpp:7004-7009 are already wrapped
// in xml_escape(). xml_escape (utils.cpp:1229) only escapes " ' & < > so
// non-ASCII UTF-8 bytes pass through unchanged — which is correct behavior.
// The XML declaration at bbs_3mf.cpp:6696 etc. correctly declares UTF-8. So
// this test failing means the bug is either (a) the writer mangles bytes on
// output, (b) the reader's expat config rejects valid UTF-8, or (c) something
// in load_project's deserialization path strips/replaces non-ASCII bytes.
TEST_CASE("non-ASCII description round-trips byte-identically",
          "[roundtrip][non_ascii_metadata]") {
    const std::string kDescription =
        "Resin print \xE2\x80\x94 \xE6\xB5\x8B\xE8\xAF\x95 "
        "\xE2\x80\x9Cx\xE2\x80\x9D caf\xC3\xA9";

    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    REQUIRE(fs::exists(src));

    bambu_cli::ProjectState s;
    auto lr = bambu_cli::load_project(src, s);
    REQUIRE(lr.ok);
    REQUIRE(s.model.model_info);
    s.model.model_info->description = kDescription;

    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("nonascii-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(s, out.string());
    INFO("save error_code:    " << sr.error_code);
    INFO("save error_message: " << sr.error_message);
    REQUIRE(sr.ok);

    bambu_cli::ProjectState r;
    auto lr2 = bambu_cli::load_project(out.string(), r);
    INFO("reload error_code:    " << lr2.error_code);
    INFO("reload error_message: " << lr2.error_message);
    REQUIRE(lr2.ok);

    REQUIRE(r.model.model_info);
    REQUIRE(r.model.model_info->description == kDescription);

    fs::remove(out);
}

TEST_CASE("non-ASCII title and copyright round-trip byte-identically",
          "[roundtrip][non_ascii_metadata]") {
    const std::string kTitle     = "\xE6\xB5\x8B\xE8\xAF\x95 \xE2\x80\x94 v1"; // 测试 — v1
    const std::string kCopyright = "\xC2\xA9 2026 caf\xC3\xA9 labs";            // © 2026 café labs

    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(src, s).ok);
    REQUIRE(s.model.model_info);
    s.model.model_info->model_name = kTitle;
    s.model.model_info->copyright  = kCopyright;

    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("nonascii-tc-%%%%-%%%%.3mf");
    REQUIRE(bambu_cli::save_project(s, out.string()).ok);

    bambu_cli::ProjectState r;
    REQUIRE(bambu_cli::load_project(out.string(), r).ok);
    REQUIRE(r.model.model_info);
    REQUIRE(r.model.model_info->model_name == kTitle);
    REQUIRE(r.model.model_info->copyright  == kCopyright);

    fs::remove(out);
}
```

- [ ] **Step 2: Register in CMake**

Modify `tests/cli/CMakeLists.txt`. Find the roundtrip block (around line 48) and add:
```cmake
    roundtrip/test_reference_3mf_passthrough.cpp
    roundtrip/test_non_ascii_metadata.cpp
)
```
(insert the new line directly after the existing `test_reference_3mf_passthrough.cpp`).

- [ ] **Step 3: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean. If the build fails on a Catch2 include, re-check that the file path matches a registered source entry.

- [ ] **Step 4: Run the test, expect PASS (this is now a green regression guard)**

```powershell
build\tests\cli\Release\cli_tests.exe "[non_ascii_metadata]"
```
Expected: PASS for both cases. The programmatic UTF-8 round-trip is correct; this test exists to catch future regressions in `xml_escape`, the writer, or the reader.

If this test fails, STOP — that indicates a different bug than the one Phase A targets, and the plan needs further reshaping.

- [ ] **Step 5: Create the e2e subprocess red test pinning the argv bug**

`tests/cli/e2e/test_project_info_non_ascii.cpp`:
```cpp
#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// On Windows, narrow-main argv arrives in the active code page (typically
// windows-1252), not UTF-8. PowerShell passes "—" (em-dash) as the single
// byte 0x97 in ACP. bambu-cli stores it as-is in description; on save the
// archive's metadata XML contains 0x97, which expat correctly rejects on
// reload as not well-formed UTF-8 (invalid token at line 7).
//
// This test fails until the wmain fix lands (Branch B in Task 2): convert
// UTF-16 argv to UTF-8 once at the CLI entry point.
TEST_CASE("info set --description with em-dash (non-ASCII): persists correctly",
          "[e2e][info_set_non_ascii]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    // UTF-8 em-dash (U+2014). boost::process on Windows routes through
    // CreateProcessW after ACP conversion, mirroring real-world PowerShell
    // invocations.
    const std::string em_dash_desc = "Resin print \xE2\x80\x94 test";

    auto r = spawn_cli({"project", "info", "set", out,
                        "--description", em_dash_desc});
    INFO("stderr: " << r.stderr_text);
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    INFO("show stdout: " << r2.stdout_text);
    REQUIRE(r2.exit_code == 0);
    REQUIRE(r2.stdout_text.find("\xE2\x80\x94") != std::string::npos);
}

TEST_CASE("info set --title with CJK characters: persists correctly",
          "[e2e][info_set_non_ascii]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    const std::string cjk_title = "\xE6\xB5\x8B\xE8\xAF\x95 v1"; // 测试 v1

    auto r = spawn_cli({"project", "info", "set", out, "--title", cjk_title});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    INFO("show stdout: " << r2.stdout_text);
    REQUIRE(r2.exit_code == 0);
    REQUIRE(r2.stdout_text.find("\xE6\xB5\x8B\xE8\xAF\x95") != std::string::npos);
}
```

- [ ] **Step 6: Register the e2e test in CMake**

Modify `tests/cli/CMakeLists.txt`. In the e2e block (around line 22), add directly after `e2e/test_project_info.cpp`:
```cmake
    e2e/test_project_info.cpp
    e2e/test_project_info_non_ascii.cpp
```

- [ ] **Step 7: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean. The `bambu-cli` target also rebuilds because the e2e test depends on the exe.

- [ ] **Step 8: Run the new e2e test, expect FAILURE**

```powershell
build\tests\cli\Release\cli_tests.exe "[info_set_non_ascii]"
```
Expected: FAIL. The em-dash case is the canonical Phase G reproduction:
- `project info set` exit code != 0 (save_project rejects on invariant guard), OR
- `project info set` succeeds but `project info show` returns description without the em-dash bytes (silent corruption), OR
- `project info show` exit code != 0 (reload itself fails on invalid UTF-8 in the archive).

If this test passes on the first run, **STOP** and report — that would mean either the bug is already fixed in some way we missed, or boost::process is doing UTF-8 transcoding we didn't expect. Either way, reshape needed.

- [ ] **Step 9: Commit (one commit for both tests — green guard + argv red pin)**

```powershell
git add tests/cli/roundtrip/test_non_ascii_metadata.cpp `
        tests/cli/e2e/test_project_info_non_ascii.cpp `
        tests/cli/CMakeLists.txt
git commit -m "test(cli): non-ASCII metadata regression guard + argv red pin"
```

---

### Task 2: DESCOPED (2026-05-28)

> Both Task 1 tests landed green: the programmatic API path and the
> `boost::process`-driven subprocess path both round-trip non-ASCII
> metadata correctly. The Phase G failure was specifically PowerShell
> with non-UTF-8 active code page handing narrow-main argv. `spawn_cli`
> uses `CreateProcessW`, which bypasses that conversion, so no
> automated test we can write inside this repo reproduces the bug. A
> `wmain` fix would have no red→green test to validate it, and the
> existing workaround (ASCII-only metadata, or users opting into
> UTF-8 ACP via "Beta: Use Unicode UTF-8 for worldwide language
> support") is acceptable. Per YAGNI, Task 2 is descoped. Phase A
> exit: 4 green regression guards covering the real round-trip paths.
>
> Original Task 2 body retained below for reference. It is NOT to be
> executed.

#### (descoped) — Apply argv-to-UTF-8 conversion at the CLI boundary (wmain on Windows)

**Files:**
- Create: `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md`
- Modify: `src/cli/main.cpp`

**Root cause** (already established by Task 1's reshape):
On Windows, narrow-main argv arrives in the active code page (typically
windows-1252), not UTF-8. The em-dash gets translated from U+2014 to the
single byte `0x97`, stored as-is in `model_info.description`, written into
the metadata XML, and rejected by expat on reload as not well-formed UTF-8.

The fix is to receive arguments as UTF-16 (`wmain`) and convert each
argument to UTF-8 exactly once at the CLI entry point, before any string
enters the ops layer.

- [ ] **Step 1: Inspect the current main.cpp entry point**

```powershell
# Use Grep to find the main() definition
```
Identify: the signature of `main`, whether there's preamble code (logging
init, exception wrapping), and the call into the command-dispatch logic.

- [ ] **Step 2: Add a Windows-only UTF-8 argv adapter**

Modify `src/cli/main.cpp`. The exact placement depends on the current
file shape; the pattern is:

```cpp
// Top of file additions (under #ifdef _WIN32):
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

// ... existing includes ...

// Extract the existing main()'s body into a static helper:
static int run_cli(int argc, char** argv) {
    // ... whatever main currently does ...
}

#ifdef _WIN32
// Receive UTF-16 argv from Windows, convert to UTF-8, then dispatch.
// MSVC links wmain when present; we keep main() for non-Windows builds.
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> utf8_storage;
    utf8_storage.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                               nullptr, 0, nullptr, nullptr);
        std::string s(needed > 0 ? static_cast<size_t>(needed - 1) : 0, '\0');
        if (needed > 0) {
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                &s[0], needed, nullptr, nullptr);
        }
        utf8_storage.push_back(std::move(s));
    }
    std::vector<char*> utf8_argv;
    utf8_argv.reserve(utf8_storage.size() + 1);
    for (auto& s : utf8_storage) utf8_argv.push_back(&s[0]);
    utf8_argv.push_back(nullptr); // argv is conventionally NULL-terminated

    // Also set the C runtime locale to UTF-8 so any stream prints in the
    // CLI follow the same convention. (CRT default on modern Windows is
    // ACP; setlocale with "" picks up the user's locale, which may not be
    // UTF-8 — we want UTF-8 explicitly for output consistency.)
    SetConsoleOutputCP(CP_UTF8);

    return run_cli(argc, utf8_argv.data());
}
#else
int main(int argc, char** argv) {
    return run_cli(argc, argv);
}
#endif
```

**Notes:**
- MSVC picks `wmain` over `main` when both are present in a Windows GUI/console app.
- Do NOT keep a second `int main(int, char**)` on Windows alongside `wmain` — that causes a duplicate-symbol link error. The `#ifdef _WIN32` / `#else` guard ensures only one entry point per build.
- If the existing `main()` does locale init, exception catching, or logging setup, preserve all of it inside `run_cli`. The wmain adapter is strictly an argv conversion shim.

- [ ] **Step 3: Build**

```powershell
cmake --build build --target bambu-cli --config Release --parallel 2
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean. If MSVC complains about `wmain` vs `main` duplicate
entry points, double-check the `#ifdef _WIN32` / `#else` placement.

- [ ] **Step 4: Run the new e2e test, expect PASS**

```powershell
build\tests\cli\Release\cli_tests.exe "[info_set_non_ascii]"
```
Expected: PASS — both em-dash and CJK round-trip cleanly through the
subprocess.

- [ ] **Step 5: Run the full suite**

```powershell
build\tests\cli\Release\cli_tests.exe --order rand
```
Expected: ALL pass. Assertion count must exceed the post-Task-1 count.

- [ ] **Step 6: Write the findings note**

Create `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md`:
```markdown
# Non-ASCII metadata roundtrip bug — root cause and fix (2026-05-27)

## Symptom
Em-dash (U+2014) in `model_info.description` caused `save_project` to fail
the `config_roundtrip` invariant guard with "not well-formed (invalid token)
at line 7" on reload. Surfaced during Phase G manual GUI smoke (2026-05-27);
worked around at the time by using ASCII-only description.

## Investigation
Initial hypothesis was writer- or reader-side mangling of valid UTF-8 bytes
in `bbs_3mf.cpp`. Task 1 added a programmatic round-trip test
(`tests/cli/roundtrip/test_non_ascii_metadata.cpp`) that sets
`description` to a UTF-8 em-dash directly on `model_info`, saves, and
reloads. That test PASSED on the first run — proving the writer, expat
reader, and `xml_escape` all handle UTF-8 correctly.

## Root cause
The bug is in the CLI argv path on Windows. The narrow-main signature
(`int main(int, char**)`) receives arguments converted from the user's
UTF-16 console input via the active code page (typically windows-1252).
An em-dash typed in PowerShell or cmd.exe arrives as the single byte
`0x97` (windows-1252) rather than the UTF-8 sequence `E2 80 94`. That
byte is stored verbatim in `description`, written into the metadata XML
declared as UTF-8, and correctly rejected by expat on reload as not
well-formed UTF-8 (single continuation byte without a leader).

## Fix
`src/cli/main.cpp` now uses `wmain` on Windows. UTF-16 argv is converted
to UTF-8 via `WideCharToMultiByte(CP_UTF8, ...)` once at the entry
point, before any string enters the ops layer. Non-Windows builds keep
the conventional `int main(int, char**)`. `SetConsoleOutputCP(CP_UTF8)`
is set so CLI stdout/stderr also emit UTF-8 consistently.

## Pinned by
- `tests/cli/roundtrip/test_non_ascii_metadata.cpp` — green regression
  guard for the programmatic path.
- `tests/cli/e2e/test_project_info_non_ascii.cpp` — red→green pin for
  the CLI argv path on Windows.
```

- [ ] **Step 7: Commit (green)**

```powershell
git add docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md src/cli/main.cpp
git commit -m "fix(cli): convert argv via wmain on Windows for utf-8 input"
```

Adjust the commit message subject to match the actual fix branch:
- Branch A: `fix(bbs_3mf): use binary std::ofstream for metadata write (utf-8 clean)`
- Branch B: `fix(cli): convert argv via wmain on Windows for utf-8 input`
- Branch C: `fix(bbs_3mf): let expat honor xml encoding declaration`

---

## Phase B — Thumbnail passthrough after plate compaction (#2)

### Task 3: Repro test for thumbnail passthrough after middle-plate removal

**Files:**
- Create: `tests/cli/roundtrip/test_thumbnail_compaction.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Inspect the fixture's plate count**

Quick check to know whether to build plates in-test or use the fixture as-is:
```powershell
& "C:\Program Files\7-Zip\7z.exe" l tests\cli\fixtures\test_reference.3mf | Select-String "plate_"
```
If you see `Metadata\plate_1.png`, `plate_2.png`, `plate_3.png` (and `_small` variants), the fixture has ≥3 plates and you can use it directly. If only `plate_1`, you must build plates in-test via `add_plate`.

Record the outcome — the test code below has two variants, one per branch.

- [ ] **Step 2: Create the test file (variant matching Step 1)**

**Variant 1 — fixture has ≥3 plates:**

`tests/cli/roundtrip/test_thumbnail_compaction.cpp`:
```cpp
#include <catch2/catch.hpp>
#include "io.hpp"
#include "project_ops.hpp"
#include "project_state.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <string>

namespace fs = boost::filesystem;

static std::string read_entry(const std::string& archive,
                              const std::string& name) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_reader_init_file(&zip, archive.c_str(), 0));
    const int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    REQUIRE(idx >= 0);
    mz_zip_archive_file_stat st;
    REQUIRE(mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(idx), &st));
    std::string buf(static_cast<size_t>(st.m_uncomp_size), '\0');
    if (st.m_uncomp_size > 0) {
        REQUIRE(mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(idx),
                                             &buf[0], buf.size(), 0));
    }
    mz_zip_reader_end(&zip);
    return buf;
}

// After removing a middle plate, surviving plates' thumbnails should be
// byte-identical to the original archive's corresponding source thumbnails
// (zero-copy passthrough), not the gray-RGBA placeholder.
//
// The CLAUDE.md "Open items" note says passthrough may fall back to synthesis
// in this case. io.cpp:130-137 looks up source by pd->plate_index, which is
// expected to be the original loader-assigned index. If libslic3r preserves
// plate_index across a plate_remove, passthrough works and this test passes;
// the comment is then stale and Task 4 just updates it.
TEST_CASE("thumbnail passthrough survives middle-plate removal",
          "[roundtrip][thumbnail_compaction]") {
    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    REQUIRE(fs::exists(src));

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(src, s).ok);
    REQUIRE(s.plate_data.size() >= 3);

    // Capture source thumbnail bytes for the plates that will survive
    // (plates 1 and 3 of [1,2,3]).
    const std::string src_p1     = read_entry(src, "Metadata/plate_1.png");
    const std::string src_p1_sm  = read_entry(src, "Metadata/plate_1_small.png");
    const std::string src_p3     = read_entry(src, "Metadata/plate_3.png");
    const std::string src_p3_sm  = read_entry(src, "Metadata/plate_3_small.png");
    REQUIRE(src_p1.size()    > 0);
    REQUIRE(src_p1_sm.size() > 0);
    REQUIRE(src_p3.size()    > 0);
    REQUIRE(src_p3_sm.size() > 0);

    // Remove plate 2 by name.
    const auto names_before = bambu_cli::list_plate_names(s);
    REQUIRE(names_before.size() >= 3);
    auto rr = bambu_cli::remove_plate(s, names_before[1]);
    INFO("remove_plate: " << rr.error_message);
    REQUIRE(rr.ok);

    // Save and reload via miniz.
    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("thumb-compact-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(s, out.string());
    INFO("save error_code:    " << sr.error_code);
    INFO("save error_message: " << sr.error_message);
    REQUIRE(sr.ok);

    // Surviving plates are now at positions 1 and 2 in the saved archive,
    // but their thumbnails should be byte-identical to the source's plate_1
    // and plate_3 entries.
    REQUIRE(read_entry(out.string(), "Metadata/plate_1.png")        == src_p1);
    REQUIRE(read_entry(out.string(), "Metadata/plate_1_small.png")  == src_p1_sm);
    REQUIRE(read_entry(out.string(), "Metadata/plate_2.png")        == src_p3);
    REQUIRE(read_entry(out.string(), "Metadata/plate_2_small.png")  == src_p3_sm);

    fs::remove(out);
}
```

**Variant 2 — fixture has <3 plates (build plates in-test):**

If Step 1 showed only `plate_1`, the test cannot exercise compaction against the fixture's own thumbnails because there are none for plates 2 and 3. In that case, this phase needs a different fixture. STOP and report — the spec lists this as Risk: "If so, B.1 first generates a synthetic multi-plate fixture using existing `plate add` commands rather than baking yet another binary into the repo."

The pragmatic path: produce a 3-plate `.3mf` once by running the bambu-cli's own `project init` + `plate add` + (optionally) `object add` + save flow against a temp output, copy that to `tests/cli/fixtures/multi_plate.3mf`, and use it in the test. But this is a sub-investigation that warrants a check-in; do not proceed silently.

- [ ] **Step 3: Register in CMake**

Append to the roundtrip block in `tests/cli/CMakeLists.txt`:
```cmake
    roundtrip/test_non_ascii_metadata.cpp
    roundtrip/test_thumbnail_compaction.cpp
)
```

- [ ] **Step 4: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean.

- [ ] **Step 5: Run the new test**

```powershell
build\tests\cli\Release\cli_tests.exe "[thumbnail_compaction]"
```
Two possible outcomes:
- **PASS:** the code in `io.cpp:130-137` already handles compaction correctly. The comment at `io.cpp:121-124` is stale. Proceed to Task 4 outcome branch A.
- **FAIL:** `plate_index` is renumbered post-remove, breaking the passthrough lookup. Proceed to Task 4 outcome branch B.

Record which outcome you observed.

- [ ] **Step 6: Commit (red or green, depending on outcome)**

```powershell
git add tests/cli/roundtrip/test_thumbnail_compaction.cpp tests/cli/CMakeLists.txt
# If PASS:
git commit -m "test(cli): pin thumbnail passthrough across plate compaction (green)"
# If FAIL:
git commit -m "test(cli): pin thumbnail passthrough across plate compaction (red)"
```

---

### Task 4: Outcome branch — update stale comment OR fix the keying

**Files:** depend on Task 3 outcome.

- [ ] **Branch A — Task 3 passed: just update the stale comment**

Modify `src/cli/io.cpp:121-124`. The current block:
```cpp
// The source entry for plate at position i is looked up by plate_index+1
// (which equals the plater_id as loaded from the source .3mf). The output
// entry name is position-based (i+1) to match how store_bbs_3mf names them.
// For plates whose plate_index was compacted after a remove, the passthrough
// may fall back to synthesis — acceptable for Phase B scope.
```
Replace with:
```cpp
// The source entry for plate at position i is looked up by plate_index+1
// (which equals the plater_id as loaded from the source .3mf). The output
// entry name is position-based (i+1) to match how store_bbs_3mf names them.
// plate_index is preserved across plate_remove in libslic3r, so compaction
// does NOT break passthrough — verified by
// tests/cli/roundtrip/test_thumbnail_compaction.cpp.
```

Build + run the full suite:
```powershell
cmake --build build --target cli_tests --config Release --parallel 2
build\tests\cli\Release\cli_tests.exe --order rand
```
Expected: ALL pass. Commit:
```powershell
git add src/cli/io.cpp
git commit -m "docs(cli): update stale comment about thumbnail compaction"
```

- [ ] **Branch B — Task 3 failed: capture source-key at load, look up by position at save**

Modify `src/cli/project_state.hpp` to add a parallel vector storing the original source-key string per plate position:
```cpp
struct ProjectState {
    Slic3r::Model                 model;
    Slic3r::DynamicPrintConfig    project_config;
    Slic3r::PlateDataPtrs         plate_data;
    std::string                   source_path;
    std::vector<std::string>      source_thumbnail_keys; // size == plate_data.size() at load time

    ProjectState() = default;
    ~ProjectState();

    ProjectState(const ProjectState&) = delete;
    ProjectState& operator=(const ProjectState&) = delete;
    ProjectState(ProjectState&&) = default;
    ProjectState& operator=(ProjectState&&) = default;
};
```

(Add `#include <vector>` and `#include <string>` if not already present — both likely transitively present via libslic3r.)

In `src/cli/io.cpp`, find the `load_project` body (after the `load_bbs_3mf` call returns) and populate `source_thumbnail_keys`:
```cpp
// After load_bbs_3mf succeeds and state.plate_data is populated:
state.source_thumbnail_keys.clear();
state.source_thumbnail_keys.reserve(state.plate_data.size());
for (const auto* pd : state.plate_data) {
    state.source_thumbnail_keys.push_back(
        pd ? ("Metadata/plate_" + std::to_string(pd->plate_index + 1)) : std::string{});
}
```

In `remove_plate` (in `src/cli/project_ops.cpp`), keep `source_thumbnail_keys` in sync with `plate_data`. Find the plate removal:
```cpp
// In remove_plate, after erasing the plate from state.plate_data, also erase
// the corresponding source_thumbnail_keys entry at the same index.
const auto idx = static_cast<size_t>(std::distance(state.plate_data.begin(), it));
delete state.plate_data[idx];
state.plate_data.erase(state.plate_data.begin() + static_cast<long>(idx));
if (idx < state.source_thumbnail_keys.size())
    state.source_thumbnail_keys.erase(state.source_thumbnail_keys.begin() + static_cast<long>(idx));
```
(Adjust to match the actual `remove_plate` body shape; the key invariant is that `source_thumbnail_keys[i]` corresponds to `plate_data[i]` after every mutation.)

Modify `rewrite_thumbnails` in `src/cli/io.cpp:130-137` to consult `source_thumbnail_keys` if non-empty:
```cpp
for (size_t i = 0; i < plate_data.size(); ++i) {
    const auto* pd = plate_data[i];
    if (!pd) continue;
    std::string out_key = "Metadata/plate_" + std::to_string(i + 1);
    std::string src_key;
    if (i < state.source_thumbnail_keys.size() && !state.source_thumbnail_keys[i].empty()) {
        src_key = state.source_thumbnail_keys[i];
    } else {
        src_key = "Metadata/plate_" + std::to_string(pd->plate_index + 1);
    }
    passthrough[out_key + ".png"]       = src_key + ".png";
    passthrough[out_key + "_small.png"] = src_key + "_small.png";
}
```
Note: `rewrite_thumbnails` currently takes `(archive_path, source_path, plate_data)`. If it doesn't already have access to `state`, plumb a `const std::vector<std::string>& source_thumbnail_keys` parameter through. Keep the change minimal — don't refactor the call site signature.

Update the stale comment at `io.cpp:121-124` to reflect the new behavior.

Build + run:
```powershell
cmake --build build --target cli_tests --config Release --parallel 2
build\tests\cli\Release\cli_tests.exe --order rand
```
Expected: ALL pass, including `[thumbnail_compaction]`.

Commit:
```powershell
git add src/cli/io.cpp src/cli/project_state.hpp src/cli/project_ops.cpp
git commit -m "fix(cli): preserve source-thumbnail keys across plate compaction"
```

---

## Phase C — Invariant guard corruption tests (#8)

### Task 5: Add archive-mutation test helpers

**Files:**
- Modify: `tests/cli/unit/unit_helpers.hpp`
- Modify: `tests/cli/unit/unit_helpers.cpp`

- [ ] **Step 1: Add helper declarations**

Append to `tests/cli/unit/unit_helpers.hpp` (inside `namespace bambu_cli_unit`):
```cpp
// Open <archive_path> as a zip, copy all entries except <entry_name> into a
// new zip, atomically replace the original. Used by invariant-guard tests
// to produce "metadata points at file that no longer exists" scenarios.
// Test-fails (REQUIRE) on any miniz error.
void mutate_archive_remove_entry(const std::string& archive_path,
                                 const std::string& entry_name);

// Open <archive_path> as a zip, append a new entry <entry_name> with bytes
// <content>, atomically replace the original. Used to produce "saved
// archive has an aux file not present in temp dir" scenarios.
// Test-fails (REQUIRE) on any miniz error.
void mutate_archive_add_extra(const std::string& archive_path,
                              const std::string& entry_name,
                              const std::string& content);
```

- [ ] **Step 2: Add helper implementations**

Edit `tests/cli/unit/unit_helpers.cpp` in two places:

**(a) Add these includes to the existing top-of-file include block** (alongside `<catch2/catch.hpp>`, `"unit_helpers.hpp"`, `"../test_helpers.hpp"`, `"io.hpp"`):
```cpp
#include <miniz.h>
#include <boost/filesystem.hpp>
#include <cstring>
```

**(b) Add the two helper bodies inside the existing `namespace bambu_cli_unit { ... }` block** (after the existing `fixture_stl` function, before the closing `}`):
```cpp
namespace fs_helper = boost::filesystem;

void mutate_archive_remove_entry(const std::string& archive_path,
                                 const std::string& entry_name) {
    mz_zip_archive src;
    std::memset(&src, 0, sizeof(src));
    REQUIRE(mz_zip_reader_init_file(&src, archive_path.c_str(), 0));

    const fs_helper::path tmp = fs_helper::path(archive_path).string() + ".mut.tmp";
    mz_zip_archive dst;
    std::memset(&dst, 0, sizeof(dst));
    REQUIRE(mz_zip_writer_init_file(&dst, tmp.string().c_str(), 0));

    bool found = false;
    const mz_uint n = mz_zip_reader_get_num_files(&src);
    for (mz_uint i = 0; i < n; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&src, i, name, sizeof(name));
        if (entry_name == name) { found = true; continue; }
        REQUIRE(mz_zip_writer_add_from_zip_reader(&dst, &src, i));
    }
    mz_zip_writer_finalize_archive(&dst);
    mz_zip_writer_end(&dst);
    mz_zip_reader_end(&src);

    REQUIRE(found); // mutating a missing entry is almost always a test bug

    fs_helper::remove(archive_path);
    fs_helper::rename(tmp, archive_path);
}

void mutate_archive_add_extra(const std::string& archive_path,
                              const std::string& entry_name,
                              const std::string& content) {
    mz_zip_archive src;
    std::memset(&src, 0, sizeof(src));
    REQUIRE(mz_zip_reader_init_file(&src, archive_path.c_str(), 0));

    const fs_helper::path tmp = fs_helper::path(archive_path).string() + ".mut.tmp";
    mz_zip_archive dst;
    std::memset(&dst, 0, sizeof(dst));
    REQUIRE(mz_zip_writer_init_file(&dst, tmp.string().c_str(), 0));

    const mz_uint n = mz_zip_reader_get_num_files(&src);
    for (mz_uint i = 0; i < n; ++i)
        REQUIRE(mz_zip_writer_add_from_zip_reader(&dst, &src, i));
    REQUIRE(mz_zip_writer_add_mem(&dst, entry_name.c_str(),
                                  content.data(), content.size(),
                                  MZ_DEFAULT_COMPRESSION));
    mz_zip_writer_finalize_archive(&dst);
    mz_zip_writer_end(&dst);
    mz_zip_reader_end(&src);

    fs_helper::remove(archive_path);
    fs_helper::rename(tmp, archive_path);
}
```
(The closing `}` for `namespace bambu_cli_unit` already exists at the end of the file — do not add another.)

- [ ] **Step 3: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean.

- [ ] **Step 4: Run the full suite (no behavior change yet, just sanity)**

```powershell
build\tests\cli\Release\cli_tests.exe --order rand
```
Expected: ALL pass. Helpers don't have direct tests yet — Tasks 6 and 7 exercise them.

- [ ] **Step 5: Commit**

```powershell
git add tests/cli/unit/unit_helpers.hpp tests/cli/unit/unit_helpers.cpp
git commit -m "test(cli): add archive-mutation helpers for invariant guard tests"
```

---

### Task 6: cover_references_resolve corruption cases

**Files:**
- Modify: `tests/cli/unit/test_invariant_cover_references.cpp`

- [ ] **Step 1: Add three corruption test cases**

Append to `tests/cli/unit/test_invariant_cover_references.cpp` (after the existing four cases, before the final empty line):
```cpp
TEST_CASE("cover refs: known-good archive minus DesignerCover file fails",
          "[unit][invariant_covref]") {
    // Known-good shape: archive with both metadata pointers + both files.
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("designer.png", "profile.jpg")},
        {"Auxiliaries/Model Pictures/designer.png",   "PNGDATA"},
        {"Auxiliaries/Profile Pictures/profile.jpg",  "JPGDATA"},
    });
    std::string err0;
    REQUIRE(bambu_cli::check_cover_references_resolve(z, &err0));
    REQUIRE(err0.empty());

    bambu_cli_unit::mutate_archive_remove_entry(
        z, "Auxiliaries/Model Pictures/designer.png");

    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("DesignerCover") != std::string::npos);
    REQUIRE(err.find("designer.png")   != std::string::npos);
    fs::remove(z);
}

TEST_CASE("cover refs: known-good archive minus ProfileCover file fails",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("designer.png", "profile.jpg")},
        {"Auxiliaries/Model Pictures/designer.png",   "PNGDATA"},
        {"Auxiliaries/Profile Pictures/profile.jpg",  "JPGDATA"},
    });
    bambu_cli_unit::mutate_archive_remove_entry(
        z, "Auxiliaries/Profile Pictures/profile.jpg");

    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("ProfileCover") != std::string::npos);
    REQUIRE(err.find("profile.jpg")  != std::string::npos);
    fs::remove(z);
}

TEST_CASE("cover refs: ProfileCover injected into model XML with no file fails",
          "[unit][invariant_covref]") {
    // Start from a known-good archive with only DesignerCover, then inject
    // a ProfileCover metadata pointer via a wholesale model XML rewrite.
    // (No targeted helper for in-archive XML mutation — it's a one-line
    // replacement of the model bytes, which mutate_archive_remove_entry +
    // a fresh add via make_zip would also achieve. We use the simpler
    // path: rebuild the zip with the desired shape.)
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("d.png", "dangling.jpg")},
        {"Auxiliaries/Model Pictures/d.png", "OK"},
        // Auxiliaries/Profile Pictures/dangling.jpg deliberately absent.
    });
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("ProfileCover") != std::string::npos);
    REQUIRE(err.find("dangling.jpg") != std::string::npos);
    fs::remove(z);
}
```

Add `#include "unit_helpers.hpp"` at the top of the file alongside the existing includes (after the `<catch2/catch.hpp>` line and before `"invariant_guard.hpp"`).

- [ ] **Step 2: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean.

- [ ] **Step 3: Run the affected suite**

```powershell
build\tests\cli\Release\cli_tests.exe "[invariant_covref]"
```
Expected: ALL pass (the 4 existing cases + 3 new = 7).

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/unit/test_invariant_cover_references.cpp
git commit -m "test(cli): pin cover_references_resolve against corrupted archives"
```

---

### Task 7: auxiliary_passthrough corruption cases

**Files:**
- Modify: `tests/cli/unit/test_invariant_aux_passthrough.cpp`

- [ ] **Step 1: Add three corruption test cases**

Append to `tests/cli/unit/test_invariant_aux_passthrough.cpp` (after the existing cases, before the closing of the file):
```cpp
TEST_CASE("aux passthrough: extra entry added externally is tolerated",
          "[unit][invariant_aux]") {
    // check_auxiliary_passthrough only verifies temp-dir contents survived
    // into the archive, not the converse. Extras (e.g. .thumbnails/) are
    // legitimately written by other code paths. This case pins that
    // behavior: adding an extra aux file *to the archive* externally is
    // NOT a passthrough failure.
    const auto dir  = make_dir({{"Others/k.txt", "K"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"},
                                {"Auxiliaries/Others/k.txt", "K"}});
    bambu_cli_unit::mutate_archive_add_extra(
        post, "Auxiliaries/Others/extra.txt", "EXTRA");

    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    REQUIRE(err.empty());
    fs::remove_all(dir); fs::remove(post);
}

TEST_CASE("aux passthrough: known-good archive minus a temp-dir file fails",
          "[unit][invariant_aux]") {
    const auto dir  = make_dir({{"Bill of Materials/parts.csv", "id,name"},
                                {"Others/note.txt", "hi"}});
    const auto post = make_zip({
        {"3D/3dmodel.model", "X"},
        {"Auxiliaries/Bill of Materials/parts.csv", "id,name"},
        {"Auxiliaries/Others/note.txt", "hi"},
    });
    bambu_cli_unit::mutate_archive_remove_entry(
        post, "Auxiliaries/Bill of Materials/parts.csv");

    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    REQUIRE(err.find("Bill of Materials/parts.csv") != std::string::npos);
    fs::remove_all(dir); fs::remove(post);
}

TEST_CASE("aux passthrough: archive with no aux entries but temp dir non-empty fails",
          "[unit][invariant_aux]") {
    const auto dir  = make_dir({{"Assembly Guide/print.pdf", "PDFBYTES"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"}});

    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    REQUIRE(err.find("Assembly Guide/print.pdf") != std::string::npos);
    fs::remove_all(dir); fs::remove(post);
}
```

Add `#include "unit_helpers.hpp"` near the top alongside the existing includes.

- [ ] **Step 2: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean.

- [ ] **Step 3: Run the affected suite**

```powershell
build\tests\cli\Release\cli_tests.exe "[invariant_aux]"
```
Expected: ALL pass (existing + 3 new).

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/unit/test_invariant_aux_passthrough.cpp
git commit -m "test(cli): pin auxiliary_passthrough against corrupted archives"
```

---

## Phase D — Negative-path cover image tests (#9)

### Task 8: Signature-level negative cases for is_png_or_jpeg

**Files:**
- Modify: `tests/cli/unit/test_image_signature.cpp`

- [ ] **Step 1: Append eight new test cases**

Append to `tests/cli/unit/test_image_signature.cpp` (after the existing cases, before EOF):
```cpp
TEST_CASE("is_png_or_jpeg: zero-byte file rejected",
          "[unit][image_signature][negative]") {
    const std::string path = write_temp({}, ".bin");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: 1-byte file rejected",
          "[unit][image_signature][negative]") {
    const std::string path = write_temp({0x89}, ".bin");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: 2-byte file rejected",
          "[unit][image_signature][negative]") {
    const std::string path = write_temp({0xFF, 0xD8}, ".bin");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: 7-byte truncated PNG signature rejected",
          "[unit][image_signature][negative]") {
    const std::vector<uint8_t> trunc = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A};
    const std::string path = write_temp(trunc, ".png");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: PNG signature plus random garbage accepted (pins current behavior)",
          "[unit][image_signature][negative]") {
    // is_png_or_jpeg is a signature check only — it does not validate
    // anything beyond the first 8 (PNG) or 3 (JPEG) bytes. Document this.
    std::vector<uint8_t> bytes = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 64; ++i) bytes.push_back(static_cast<uint8_t>(0xA5 ^ i));
    const std::string path = write_temp(bytes, ".png");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: JPEG SOI plus random garbage accepted (pins current behavior)",
          "[unit][image_signature][negative]") {
    std::vector<uint8_t> bytes = {0xFF,0xD8,0xFF};
    for (int i = 0; i < 64; ++i) bytes.push_back(static_cast<uint8_t>(0xA5 ^ i));
    const std::string path = write_temp(bytes, ".jpg");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: PNG signature with embedded NUL bytes accepted (pins current behavior)",
          "[unit][image_signature][negative]") {
    std::vector<uint8_t> bytes = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
                                   0x00,0x00,0x00,0x00, 'd','a','t','a'};
    const std::string path = write_temp(bytes, ".png");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: DOS exe (MZ header) rejected",
          "[unit][image_signature][negative]") {
    const std::vector<uint8_t> mz = {'M','Z', 0x90,0x00, 0x03,0x00, 0x00,0x00};
    const std::string path = write_temp(mz, ".exe");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: PDF header (%PDF) rejected",
          "[unit][image_signature][negative]") {
    const std::vector<uint8_t> pdf = {'%','P','D','F','-','1','.','4', 0x0A};
    const std::string path = write_temp(pdf, ".pdf");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}
```

Add `#include <boost/filesystem.hpp>` and `namespace fs = boost::filesystem;` if the existing top of the file lacks them. Inspect the existing `write_temp` helper — it returns `path.string()`, and the existing tests already use `fs::remove(path)`, so both are likely already present. (Confirmed by reading the file: they are present at top of file.)

- [ ] **Step 2: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean.

- [ ] **Step 3: Run the affected suite**

```powershell
build\tests\cli\Release\cli_tests.exe "[image_signature]"
```
Expected: ALL pass (existing 8 + 9 new = 17).

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/unit/test_image_signature.cpp
git commit -m "test(cli): pin is_png_or_jpeg negative paths and current behavior"
```

---

### Task 9: Embed-path negative cases for info_set

**Files:**
- Modify: `tests/cli/unit/test_cover_decoupling.cpp`

- [ ] **Step 1: Append five new test cases**

Append to `tests/cli/unit/test_cover_decoupling.cpp` (after the last existing case):
```cpp
TEST_CASE("info_set negative: non-existent cover path throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = "C:/does/not/exist/anywhere/cover.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);
}

TEST_CASE("info_set negative: zero-byte cover throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    const fs::path empty = fs::temp_directory_path() /
                           fs::unique_path("empty-%%%%-%%%%.png");
    { std::ofstream f(empty.string(), std::ios::binary); } // touch empty

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = empty.string();
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);

    fs::remove(empty);
}

TEST_CASE("info_set negative: cover path is a directory throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    const fs::path dir = fs::temp_directory_path() /
                         fs::unique_path("notafile-%%%%-%%%%");
    fs::create_directory(dir);

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = dir.string();
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);

    fs::remove(dir);
}

TEST_CASE("info_set negative: GIF file (wrong signature) throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    const fs::path gif = fs::temp_directory_path() /
                         fs::unique_path("bad-%%%%-%%%%.gif");
    {
        std::ofstream f(gif.string(), std::ios::binary);
        const char header[] = "GIF89a";
        f.write(header, 6);
    }

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = gif.string();
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);

    fs::remove(gif);
}

TEST_CASE("info_set baseline: tiny known-good PNG succeeds and matches byte-for-byte",
          "[unit][cover_decouple][negative]") {
    // Smallest practical PNG: 8-byte signature + IHDR + IEND. Any extra bytes
    // past the signature are fine — is_png_or_jpeg is signature-only.
    const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D,
        'I','H','D','R',
        0x00,0x00,0x00,0x01, 0x00,0x00,0x00,0x01,
        0x08,0x06, 0x00,0x00,0x00,
        0x1F,0x15,0xC4,0x89,
        0x00,0x00,0x00,0x00,
        'I','E','N','D',
        0xAE,0x42,0x60,0x82
    };
    const fs::path png_path = fs::temp_directory_path() /
                              fs::unique_path("tiny-%%%%-%%%%.png");
    {
        std::ofstream f(png_path.string(), std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()),
                static_cast<std::streamsize>(png.size()));
    }

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = png_path.string();
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    REQUIRE(s.model.model_info);
    REQUIRE(s.model.model_info->cover_file == png_path.filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    const fs::path landed = aux / "Model Pictures" / png_path.filename();
    REQUIRE(fs::exists(landed));
    REQUIRE(fs::file_size(landed) == png.size());

    fs::remove(png_path);
}
```

Add `#include "exceptions.hpp"` near the top of the file (for `bambu_cli::BadCoverImage`) alongside the existing `#include "project_tab_ops.hpp"`. Add `#include <fstream>` next to the existing `<boost/filesystem.hpp>` include if not already present.

- [ ] **Step 2: Build**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: builds clean. (`bambu_cli::BadCoverImage` is defined in `src/cli/exceptions.hpp:65` and is the correct exception type for cover-validation failures.)

- [ ] **Step 3: Run the affected suite**

```powershell
build\tests\cli\Release\cli_tests.exe "[cover_decouple]"
```
Expected: ALL pass (existing 4 + 5 new = 9).

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/unit/test_cover_decoupling.cpp
git commit -m "test(cli): pin cover info_set negative paths"
```

---

## Final verification

- [ ] **Step 1: Run the full suite**

```powershell
build\tests\cli\Release\cli_tests.exe --order rand
```
Expected: ALL pass.

- [ ] **Step 2: Verify assertion count exceeded baseline**

The final test summary line should report something like `268 + N test cases | 1324 + M assertions` where N and M are both strictly positive. Baseline (HEAD `3618319c4`) was 268 cases / 1324 assertions. After this plan: expected ~268 + ~24 test cases (across Phases A–D), 1324 + ~80 assertions (rough estimate).

If the count did not exceed the baseline, something registered but didn't run — investigate.

- [ ] **Step 3: Confirm no stop conditions silently activated**

Review the plan's stop conditions (spec §"Stop conditions"):
1. Phase A.2 broader-than-CLI fix scope → should have triggered a check-in.
2. Phase B.1 repro test passed → if it passed, Task 4 Branch A applied (comment update only).
3. Phase C helpers > ~120 lines → check Task 5's diff size.
4. Any test failed for a reason different from the prediction → should have triggered a check-in.

If any of these went silent during execution, surface it now.

- [ ] **Step 4: Verify git log**

```powershell
git log --oneline 3618319c4..HEAD
```
Expected commits (some may merge per branch outcomes):
- `test(cli): pin non-ASCII metadata roundtrip bug (red)`
- `fix(...)`: non-ASCII fix (subject depends on Task 2 branch chosen)
- `test(cli): pin thumbnail passthrough across plate compaction (red OR green)`
- `docs(cli): update stale comment about thumbnail compaction` OR `fix(cli): preserve source-thumbnail keys across plate compaction`
- `test(cli): add archive-mutation helpers for invariant guard tests`
- `test(cli): pin cover_references_resolve against corrupted archives`
- `test(cli): pin auxiliary_passthrough against corrupted archives`
- `test(cli): pin is_png_or_jpeg negative paths and current behavior`
- `test(cli): pin cover info_set negative paths`

Total: 8–9 commits. None pushed.

---

**Plan complete.**
