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

### Task 1: Red test pinning the non-ASCII roundtrip bug

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

- [ ] **Step 4: Run the new test, expect failure**

```powershell
build\tests\cli\Release\cli_tests.exe "[non_ascii_metadata]"
```
Expected: FAIL. Likely failure modes:
- `save_project` returns `!ok` with `error_code: "invariant_violation"` and `error_message` mentioning `config_roundtrip` and a parser error at line 7.
- Or save succeeds but reload returns mismatched description (silent truncation/replacement).

If the test passes on the first run, **STOP** and report — the bug may already be fixed upstream and this work needs re-scoping.

- [ ] **Step 5: Commit (red)**

```powershell
git add tests/cli/roundtrip/test_non_ascii_metadata.cpp tests/cli/CMakeLists.txt
git commit -m "test(cli): pin non-ASCII metadata roundtrip bug (red)"
```

---

### Task 2: Investigate root cause and fix

**Files:**
- Create: `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md`
- Modify: One of `src/libslic3r/Format/bbs_3mf.cpp` / `src/cli/main.cpp` / a different libslic3r site, chosen by the investigation below.

- [ ] **Step 1: Instrument the writer**

Open `src/libslic3r/Format/bbs_3mf.cpp` and find the metadata write site (around line 7054):
```cpp
            // store metadata info
            for (auto item : metadata_item_map) {
                BOOST_LOG_TRIVIAL(info) << "bbs_3mf: save key= " << item.first << ", value = " << item.second;
                stream << " <" << METADATA_TAG << " name=\"" << item.first << "\">"
```

Add a hex-dump trace immediately before the `stream <<` call:
```cpp
            for (auto item : metadata_item_map) {
                BOOST_LOG_TRIVIAL(info) << "bbs_3mf: save key= " << item.first << ", value = " << item.second;
                {
                    std::ostringstream hx;
                    for (unsigned char c : item.second)
                        hx << std::hex << std::setw(2) << std::setfill('0') << (int)c << ' ';
                    BOOST_LOG_TRIVIAL(info) << "bbs_3mf: HEX(" << item.first << ") = " << hx.str();
                }
                stream << " <" << METADATA_TAG << " name=\"" << item.first << "\">"
```

(Add `#include <iomanip>` and `#include <sstream>` near the top of the file if not already present — both likely are; check first.)

- [ ] **Step 2: Build + run the failing test with trace logging**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
$env:BOOST_LOG_TRIVIAL_LEVEL = "info"
build\tests\cli\Release\cli_tests.exe "[non_ascii_metadata]" --success
```
Expected: same FAIL as before, but trace lines now show `HEX(Description) = ...` with the actual bytes leaving `description` immediately before the stream write.

- [ ] **Step 3: Hex-dump the saved archive's 3D/3dmodel.model entry**

The test prints `INFO("save error_code: ...")` only on failure; to capture the saved file independently, comment out the `fs::remove(out);` line at the end of the test temporarily and re-run. Then inspect the failed-save output:
```powershell
# After failed run, the temp file may or may not still exist depending on
# whether save_project deleted it. If guard ran and rejected, the file is
# at <out>.tmp.3mf — find it:
Get-ChildItem $env:TEMP -Filter "nonascii-*" -ErrorAction SilentlyContinue
```
Open the tmp file with 7-zip or `unzip -l` equivalent (miniz CLI works), extract `3D/3dmodel.model`, and find the `<metadata name="Description">` line. Compare its bytes to the HEX log from Step 2.

- [ ] **Step 4: Classify root cause and write the findings note**

Create `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md`:
```markdown
# Non-ASCII metadata roundtrip bug — root cause (2026-05-27)

## Symptom
Em-dash (U+2014) in `model_info.description` caused `save_project` to fail
the `config_roundtrip` invariant guard with "not well-formed (invalid token)
at line 7" on reload. Surfaced during Phase G manual GUI smoke (2026-05-27);
worked around at the time by using ASCII-only description.

## Investigation
[Hex-dump comparison from Steps 2-3:]
- Bytes leaving `description` at bbs_3mf.cpp:7054: <FILL IN OBSERVED BYTES>
- Bytes in saved archive's 3D/3dmodel.model line 7: <FILL IN OBSERVED BYTES>

## Root cause
<one of:>
- **Writer-side:** the output stream applied codepage conversion to UTF-8
  bytes, mangling them on disk. Affected sites: <list>.
- **Reader-side:** the expat parser is configured with a non-UTF-8 encoding
  override at <site>; UTF-8 input is mis-interpreted.
- **Other:** <describe>.

## Fix
<one of:>
- Replaced `<stream type>` with `<utf-8-clean alternative>` at <site>.
- Removed/corrected the expat encoding override at <site>.
- <other>.

## Pinned by
`tests/cli/roundtrip/test_non_ascii_metadata.cpp` (added in commit before
this fix).
```

Fill in the angle-bracketed sections with the actual findings.

- [ ] **Step 5: Apply the fix — pick the branch that matches your findings**

**Branch A — Writer-side (stream codepage conversion).** Most likely culprit on Windows: the `stream` is a `boost::nowide::ofstream` or wraps one that applies narrow-to-wide conversion. Locate the `stream` declaration upstream of `bbs_3mf.cpp:7054` and confirm its type. If it's a `boost::nowide::ofstream`, swap to a binary-mode `std::ofstream`:
```cpp
// Find the existing declaration (likely around bbs_3mf.cpp:6940-ish):
// boost::nowide::ofstream stream(path.string());
// Replace with:
std::ofstream stream(path.string(), std::ios::binary);
```
If `stream` is a `std::stringstream` accumulator that gets written via a separate code path, find the actual file-write site (`mz_zip_writer_add_mem` for in-memory; `mz_zip_writer_add_file` for on-disk) and verify the bytes aren't transformed in between.

**Branch B — CLI-input-side (argv encoded as windows-1252).** This branch only applies if the bug also manifests when running `bambu-cli.exe project info set --description "—"` from a terminal AND the bytes leaving description in Step 2 are *already* malformed before reaching `bbs_3mf.cpp`. In that case the bug is in `src/cli/main.cpp`:
```cpp
// In main() near argv parsing — switch to wmain on Windows and convert UTF-16
// to UTF-8 once at the entry point.
#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args_utf8;
    args_utf8.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], len, nullptr, nullptr);
        args_utf8.push_back(std::move(s));
    }
    std::vector<char*> argv_utf8;
    argv_utf8.reserve(args_utf8.size());
    for (auto& a : args_utf8) argv_utf8.push_back(&a[0]);
    return real_main(argc, argv_utf8.data());
}
int main(int argc, char** argv) { return real_main(argc, argv); }
#else
int main(int argc, char** argv) { return real_main(argc, argv); }
#endif
```
Move the body of the existing `main` into `real_main(int, char**)`. Include `<windows.h>` under `#ifdef _WIN32`.

**Branch C — Reader-side (expat encoding override).** Search `bbs_3mf.cpp` for `XML_ParserCreate` or `XML_ParserCreateNS`:
```powershell
# Use Grep (NOT bash grep)
```
If a parser is created with a non-null encoding argument (e.g. `XML_ParserCreate("US-ASCII")`), change it to `XML_ParserCreate(nullptr)` so expat honors the XML declaration.

**Branch D — None of the above.** STOP. The Phase A.4 stop condition in the spec applies: report findings and reassess scope before continuing.

- [ ] **Step 6: Remove the instrumentation**

Revert the hex-dump trace block added in Step 1 — it was diagnostic only. The file should otherwise match its pre-Step-1 state plus the chosen fix from Step 5.

- [ ] **Step 7: Re-enable test cleanup**

If you commented out `fs::remove(out);` in Step 3, uncomment both occurrences in `tests/cli/roundtrip/test_non_ascii_metadata.cpp`.

- [ ] **Step 8: Build + run**

```powershell
cmake --build build --target cli_tests --config Release --parallel 2
build\tests\cli\Release\cli_tests.exe "[non_ascii_metadata]"
```
Expected: PASS for both cases.

- [ ] **Step 9: Run the full suite**

```powershell
build\tests\cli\Release\cli_tests.exe --order rand
```
Expected: ALL pass. Assertion count > 1324.

- [ ] **Step 10: Commit (green)**

```powershell
git add docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md
git add <the production source file you modified in Step 5>
git commit -m "fix: non-ASCII metadata roundtrip (utf-8 clean writer/reader path)"
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
