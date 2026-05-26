# Canonical Aux Folder Layout — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align CLI aux folder names with Bambu Studio canonical (`Model Pictures`, `Profile Pictures`, `Bill of Materials`, `Assembly Guide`, `Others`); decouple `DesignerCover` from `ProfileCover` (own folders, own basenames, no shared file); accept JPEG in addition to PNG; add `--cover-name` to pick an existing image; gate the result with two new invariant guards and a round-trip test against `test_reference.3mf`.

**Architecture:** Touch points are concentrated in `src/cli/project_tab_ops.{hpp,cpp}` (enum + cover helpers), `src/cli/commands/project_tab.cpp` (CLI flag wiring + mutual-exclusion + name sanitization at the user-facing layer), `src/cli/invariant_guard.{hpp,cpp}` + `src/cli/io.cpp` (post-save checks). Existing module boundaries are preserved. The CLI is pre-release: no alias compatibility for old flag values.

**Tech Stack:** C++17, libslic3r, Catch2 v2.x, boost::filesystem, miniz (mz_zip), CLI11, nlohmann::json. Spec: `docs/superpowers/specs/2026-05-26-canonical-aux-layout-design.md`.

**Build environment (Windows; required priming before any cmake/build call this session):**
```powershell
$vsCmakeBin = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$env:Path = "$vsCmakeBin;C:\Strawberry\perl\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\site\bin;" + $env:Path
$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"
$env:CMAKE_GENERATOR = "Visual Studio 16 2019"
$env:CMAKE_GENERATOR_PLATFORM = "x64"
```
Without this, the system PATH's CMake 4.x is selected and the regeneration step fails (the repo's `CMakeLists.txt:4-6` rejects CMake >= 4.0 on Windows). All `cmake --build` invocations in this plan use `--config Release --parallel 2`; the test executable lives at `build/tests/cli/Release/cli_tests.exe` and the CLI binary at `build/src/cli/Release/bambu-cli.exe`. (RelWithDebInfo is broken on this tree per `memory/build_environment.md` — IMPORTED Debug/Release mapping bugs in BambuStudio's own CMakeLists; cli_tests has the `MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE` insurance, but the larger libslic3r/bambu-cli link path doesn't.)

**Pre-conditions verified during plan creation (2026-05-26):**
- `tests/cli/fixtures/cover_smoke.png` and `cover_smoke.jpg` already exist, are git-tracked, and have valid magic bytes (`89 50 4E 47 0D 0A 1A 0A` / `FF D8 FF E0`). Tasks 3 and 4 use them; no creation step needed.
- Committed `.3mf` fixtures (`tests/cli/fixtures/reference.3mf`, `tests/cli/fixtures/local/temp_project_for_bambu_studio.3mf`) contain no `Auxiliaries/*` entries — the new `check_auxiliary_passthrough` guard (Task 7) will pass vacuously for them. No fixture regeneration required.
- `store_bbs_3mf` (`src/libslic3r/Format/bbs_3mf.cpp:6977-7003`) serializes `model_info.cover_file` and `profile_info.ProfileCover` verbatim (no prefix added/stripped). The plan's `embed_image_into_folder` returns a basename and assigning it to those fields produces the basename-only metadata the reference file uses.

---

## File Structure

**Source files modified:**
- `src/cli/project_tab_ops.hpp` — enum + struct fields
- `src/cli/project_tab_ops.cpp` — enum strings, cover helpers, info/profile_set rewiring
- `src/cli/commands/project_tab.cpp` — `--cover-name` option, mutual-exclusion + sanitize at CLI layer, `parse_folder` updates
- `src/cli/invariant_guard.hpp` — new check declarations + `GuardResult.failed_check` extension
- `src/cli/invariant_guard.cpp` — new check implementations + `run_guard` extension

**Tests modified:**
- `tests/cli/unit/test_project_aux_ops.cpp` (enum name updates + new ProfilePictures aux_list case)
- `tests/cli/unit/test_project_info_ops.cpp` (cover decoupling)
- `tests/cli/unit/test_project_profile_ops.cpp` (cover decoupling)
- `tests/cli/roundtrip/test_project_tab.cpp` (enum name updates)
- `tests/cli/e2e/test_project_info.cpp` (new --cover-name exit-code cases)
- `tests/cli/e2e/test_project_profile.cpp` (new --cover-name exit-code cases)
- `tests/cli/CMakeLists.txt` (register new test files + new fixture macro)

**Tests created:**
- `tests/cli/unit/test_image_signature.cpp`
- `tests/cli/unit/test_aux_folder_canonical_names.cpp`
- `tests/cli/unit/test_cover_decoupling.cpp`
- `tests/cli/unit/test_cover_pick_by_name.cpp`
- `tests/cli/unit/test_invariant_aux_passthrough.cpp`
- `tests/cli/unit/test_invariant_cover_references.cpp`
- `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp`
- `tests/cli/fixtures/test_reference.3mf` (binary fixture)

**Docs modified:**
- `CLAUDE.md` (correct aux-folder divergence note)
- `docs/cli/status.md` (add Phase G entry)

**Docs created:**
- `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md`

---

## Task 1: Image signature validator

**Files:**
- Modify: `src/cli/project_tab_ops.hpp`
- Modify: `src/cli/project_tab_ops.cpp`
- Create: `tests/cli/unit/test_image_signature.cpp`
- Modify: `tests/cli/CMakeLists.txt`

The current `check_png_signature` is a file-internal static. The new `is_png_or_jpeg` needs to be callable from a test, so we expose it as a free function in the `bambu_cli::detail` namespace declared in `project_tab_ops.hpp`.

- [ ] **Step 1: Write the failing test**

Create `tests/cli/unit/test_image_signature.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "project_tab_ops.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>

namespace fs = boost::filesystem;

static std::string write_temp(const std::vector<uint8_t>& bytes,
                              const std::string& ext) {
    const fs::path p = fs::temp_directory_path() /
                       fs::unique_path("img-%%%%-%%%%" + ext);
    std::ofstream f(p.string(), std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return p.string();
}

TEST_CASE("is_png_or_jpeg: accepts PNG signature",
          "[unit][image_signature]") {
    const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A, 0x00,0x00,0x00,0x0D
    };
    const std::string path = write_temp(png, ".png");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: accepts JPEG SOI marker (JFIF)",
          "[unit][image_signature]") {
    const std::vector<uint8_t> jpeg = {
        0xFF,0xD8,0xFF,0xE0, 0x00,0x10,'J','F','I','F'
    };
    const std::string path = write_temp(jpeg, ".jpg");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: accepts JPEG with EXIF (FF D8 FF E1)",
          "[unit][image_signature]") {
    const std::vector<uint8_t> exif = {0xFF,0xD8,0xFF,0xE1, 0x00,0x10,'E','x','i','f'};
    const std::string path = write_temp(exif, ".jpg");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects GIF",
          "[unit][image_signature]") {
    const std::vector<uint8_t> gif = {'G','I','F','8','9','a', 0x01,0x00};
    const std::string path = write_temp(gif, ".gif");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects truncated header",
          "[unit][image_signature]") {
    const std::vector<uint8_t> trunc = {0x89, 0x50};
    const std::string path = write_temp(trunc, ".png");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects empty file",
          "[unit][image_signature]") {
    const std::string path = write_temp({}, ".png");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects text file",
          "[unit][image_signature]") {
    const std::vector<uint8_t> txt(64, 'A');
    const std::string path = write_temp(txt, ".txt");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects nonexistent path",
          "[unit][image_signature]") {
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(
        "C:/does/not/exist/anywhere.png"));
}
```

- [ ] **Step 2: Register the test in CMake**

Edit `tests/cli/CMakeLists.txt`. In `BAMBU_CLI_TEST_SOURCES`, find the existing line `unit/test_png_placeholder.cpp` and insert directly after it:

```cmake
    unit/test_image_signature.cpp
```

(Step ordering note: registering in CMake before adding the declaration is what makes the red phase observable below — otherwise the build target wouldn't compile the test file and we'd see a false green.)

- [ ] **Step 3: Run test to verify it fails**

Build, then:
```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[image_signature]"
```
Expected: build fails — `bambu_cli::detail::is_png_or_jpeg` not declared.

- [ ] **Step 4: Add the `detail` namespace + declaration to `project_tab_ops.hpp`**

In `src/cli/project_tab_ops.hpp`, find the closing `} // namespace bambu_cli` at the bottom of the file. Immediately before it, insert:

```cpp
namespace detail {
    // Returns true if <path> begins with the PNG magic (89 50 4E 47 0D 0A 1A 0A)
    // or the JPEG SOI sequence (FF D8 FF). Returns false on read failure,
    // truncation (<3 bytes), or any other signature.
    bool is_png_or_jpeg(const std::string& path);
}
```

- [ ] **Step 5: Implement `is_png_or_jpeg` in `project_tab_ops.cpp`**

In `src/cli/project_tab_ops.cpp`, find the existing `check_png_signature` static helper (it begins with `static const uint8_t kPngSignature[8]` and `static bool check_png_signature`). Immediately below the `check_png_signature` function body, add:

```cpp
namespace detail {

bool is_png_or_jpeg(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t hdr[8] = {};
    f.read(reinterpret_cast<char*>(hdr), 8);
    const auto n = f.gcount();
    if (n >= 8 && std::memcmp(hdr, "\x89PNG\r\n\x1A\n", 8) == 0) return true;
    if (n >= 3 && hdr[0] == 0xFF && hdr[1] == 0xD8 && hdr[2] == 0xFF) return true;
    return false;
}

} // namespace detail
```

`check_png_signature` itself stays for now; it is removed in Task 3.

- [ ] **Step 6: Run test to verify it passes**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[image_signature]"
```
Expected: 8 assertions, all PASS.

- [ ] **Step 7: Commit**

```
git add src/cli/project_tab_ops.hpp src/cli/project_tab_ops.cpp \
        tests/cli/unit/test_image_signature.cpp tests/cli/CMakeLists.txt
git commit -m "feat(cli): add is_png_or_jpeg signature helper

Pre-work for accepting JPEG covers alongside PNG. The existing
check_png_signature stays for now; will be removed when info_set/
profile_set switch over in Task 3."
```

---

## Task 2: Rename AuxFolder enum + add ProfilePictures + pin canonical names

**Files:**
- Modify: `src/cli/project_tab_ops.hpp`
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `src/cli/commands/project_tab.cpp`
- Modify: `tests/cli/unit/test_project_aux_ops.cpp`
- Modify: `tests/cli/roundtrip/test_project_tab.cpp`
- Modify: `tests/cli/unit/test_project_info_ops.cpp` (if it references AuxFolder)
- Modify: `tests/cli/unit/test_project_profile_ops.cpp` (if it references AuxFolder)
- Create: `tests/cli/unit/test_aux_folder_canonical_names.cpp`
- Modify: `tests/cli/CMakeLists.txt`

This task is a single atomic refactor: rename the enum, add the new variant, update every callsite, update every test that references the old names, and add a new canonical-names test. The whole tree must compile + tests pass before we commit. (Earlier draft split this across two tasks, leaving an uncompilable working tree between them — bad for subagent-driven execution.)

- [ ] **Step 1: Write the new canonical-names test**

Create `tests/cli/unit/test_aux_folder_canonical_names.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "project_tab_ops.hpp"

// Pin the exact canonical names. These must match Bambu Studio's
// auxiliary directory layout (src/slic3r/GUI/Auxiliary.hpp:75 and
// src/slic3r/GUI/Project.cpp:214-226). A future accidental rename will
// fail loudly here with a clear diff.

TEST_CASE("AuxFolder: canonical subdir strings",
          "[unit][aux_folder_names]") {
    using bambu_cli::folder_subdir;
    using bambu_cli::AuxFolder;
    REQUIRE(folder_subdir(AuxFolder::ModelPictures)    == "Model Pictures");
    REQUIRE(folder_subdir(AuxFolder::ProfilePictures)  == "Profile Pictures");
    REQUIRE(folder_subdir(AuxFolder::BillOfMaterials)  == "Bill of Materials");
    REQUIRE(folder_subdir(AuxFolder::AssemblyGuide)    == "Assembly Guide");
    REQUIRE(folder_subdir(AuxFolder::Others)           == "Others");
}

TEST_CASE("AuxFolder: canonical --folder flag spellings",
          "[unit][aux_folder_names]") {
    using bambu_cli::folder_flag;
    using bambu_cli::AuxFolder;
    REQUIRE(folder_flag(AuxFolder::ModelPictures)    == "model-pictures");
    REQUIRE(folder_flag(AuxFolder::ProfilePictures)  == "profile-pictures");
    REQUIRE(folder_flag(AuxFolder::BillOfMaterials)  == "bill-of-materials");
    REQUIRE(folder_flag(AuxFolder::AssemblyGuide)    == "assembly-guide");
    REQUIRE(folder_flag(AuxFolder::Others)           == "others");
}

TEST_CASE("AuxFolder: canonical JSON keys",
          "[unit][aux_folder_names]") {
    using bambu_cli::folder_json_key;
    using bambu_cli::AuxFolder;
    REQUIRE(folder_json_key(AuxFolder::ModelPictures)    == "model_pictures");
    REQUIRE(folder_json_key(AuxFolder::ProfilePictures)  == "profile_pictures");
    REQUIRE(folder_json_key(AuxFolder::BillOfMaterials)  == "bill_of_materials");
    REQUIRE(folder_json_key(AuxFolder::AssemblyGuide)    == "assembly_guide");
    REQUIRE(folder_json_key(AuxFolder::Others)           == "others");
}
```

Register in `tests/cli/CMakeLists.txt` immediately after `unit/test_image_signature.cpp`:
```cmake
    unit/test_aux_folder_canonical_names.cpp
```

- [ ] **Step 2: Update the enum definition in the header**

In `src/cli/project_tab_ops.hpp`, find the existing `enum class AuxFolder` definition (currently lists `Pictures, Bom, AssemblyGuide, Others`) and replace its body with:

```cpp
enum class AuxFolder {
    ModelPictures,      // archive subdir "Model Pictures"
    ProfilePictures,    // archive subdir "Profile Pictures"
    BillOfMaterials,    // archive subdir "Bill of Materials"
    AssemblyGuide,      // archive subdir "Assembly Guide"
    Others,             // archive subdir "Others"
};
```

- [ ] **Step 3: Update the three lookup functions in `project_tab_ops.cpp`**

In `src/cli/project_tab_ops.cpp`, find the `// AuxFolder helpers` comment banner and replace the three function bodies (`folder_flag`, `folder_json_key`, `folder_subdir`) with:

```cpp
std::string folder_flag(AuxFolder f) {
    switch (f) {
        case AuxFolder::ModelPictures:   return "model-pictures";
        case AuxFolder::ProfilePictures: return "profile-pictures";
        case AuxFolder::BillOfMaterials: return "bill-of-materials";
        case AuxFolder::AssemblyGuide:   return "assembly-guide";
        case AuxFolder::Others:          return "others";
    }
    return "others";
}

std::string folder_json_key(AuxFolder f) {
    switch (f) {
        case AuxFolder::ModelPictures:   return "model_pictures";
        case AuxFolder::ProfilePictures: return "profile_pictures";
        case AuxFolder::BillOfMaterials: return "bill_of_materials";
        case AuxFolder::AssemblyGuide:   return "assembly_guide";
        case AuxFolder::Others:          return "others";
    }
    return "others";
}

std::string folder_subdir(AuxFolder f) {
    switch (f) {
        case AuxFolder::ModelPictures:   return "Model Pictures";
        case AuxFolder::ProfilePictures: return "Profile Pictures";
        case AuxFolder::BillOfMaterials: return "Bill of Materials";
        case AuxFolder::AssemblyGuide:   return "Assembly Guide";
        case AuxFolder::Others:          return "Others";
    }
    return "Others";
}
```

- [ ] **Step 4: Update the `aux_list` iteration set in `project_tab_ops.cpp`**

In `src/cli/project_tab_ops.cpp`, find `std::vector<AuxEntry> aux_list(ProjectState& state)`. Inside its body, locate the `for (const auto folder : {AuxFolder::Pictures, AuxFolder::Bom, ...})` and replace it with:
```cpp
    for (const auto folder : {AuxFolder::ModelPictures, AuxFolder::ProfilePictures,
                               AuxFolder::BillOfMaterials, AuxFolder::AssemblyGuide,
                               AuxFolder::Others}) {
```

- [ ] **Step 5: Update `parse_folder` in `src/cli/commands/project_tab.cpp`**

In `src/cli/commands/project_tab.cpp`, find `static AuxFolder parse_folder(const std::string& s, OutputMode mode)`. Replace its entire body with:

```cpp
static AuxFolder parse_folder(const std::string& s, OutputMode mode) {
    if (s == "model-pictures")    return AuxFolder::ModelPictures;
    if (s == "profile-pictures")  return AuxFolder::ProfilePictures;
    if (s == "bill-of-materials") return AuxFolder::BillOfMaterials;
    if (s == "assembly-guide")    return AuxFolder::AssemblyGuide;
    if (s == "others")            return AuxFolder::Others;
    emit_error(mode, "usage_error", "unknown folder: " + s +
               " (expected: model-pictures|profile-pictures|bill-of-materials|assembly-guide|others)");
    std::exit(to_int(ExitCode::usage_error));
}
```

- [ ] **Step 6: Update existing aux ops tests for enum renames**

In `tests/cli/unit/test_project_aux_ops.cpp`, find the four `folder_flag` assertions inside the test case `"folder_flag returns hyphen form"`. Replace them with:
```cpp
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::ModelPictures)    == "model-pictures");
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::BillOfMaterials)  == "bill-of-materials");
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::AssemblyGuide)    == "assembly-guide");
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::Others)           == "others");
```

Then in the `folder_json_key` test case, replace the two assertions with:
```cpp
    REQUIRE(bambu_cli::folder_json_key(bambu_cli::AuxFolder::AssemblyGuide) == "assembly_guide");
    REQUIRE(bambu_cli::folder_json_key(bambu_cli::AuxFolder::ModelPictures) == "model_pictures");
```

Then search the whole file for any other `AuxFolder::Pictures` or `AuxFolder::Bom` occurrence and rewrite to `AuxFolder::ModelPictures` / `AuxFolder::BillOfMaterials` respectively. There are no semantic changes — every existing test still asserts the same operation on the same (renamed) folder.

- [ ] **Step 7: Add a new `aux_list` test case for ProfilePictures enumeration**

In `tests/cli/unit/test_project_aux_ops.cpp`, append at the end of the file:

```cpp
TEST_CASE("aux_list: file added under Profile Pictures is enumerated",
          "[unit][c3][aux_list]") {
    bambu_cli::ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ProfilePictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    const auto entries = bambu_cli::aux_list(s);
    bool saw = false;
    for (const auto& e : entries) {
        if (e.folder == bambu_cli::AuxFolder::ProfilePictures &&
            e.name == "cover_smoke.png") {
            saw = true;
            break;
        }
    }
    REQUIRE(saw);
}
```

(Include guard: `tests/cli/unit/test_project_aux_ops.cpp` already includes `unit_helpers.hpp` and `project_tab_ops.hpp`. No new includes required.)

- [ ] **Step 8: Update the existing roundtrip test for enum renames**

In `tests/cli/roundtrip/test_project_tab.cpp`, search the whole file for `AuxFolder::Pictures` and `AuxFolder::Bom`. Rewrite each to `AuxFolder::ModelPictures` and `AuxFolder::BillOfMaterials` respectively.

- [ ] **Step 9: Update info/profile ops tests if they reference AuxFolder**

```
grep -n "AuxFolder::Pictures\|AuxFolder::Bom" tests/cli/unit/test_project_info_ops.cpp tests/cli/unit/test_project_profile_ops.cpp
```
For each hit, rewrite to the canonical names. If no hits, nothing to do.

- [ ] **Step 10: Build and run all tests**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe
```
Expected: build succeeds; `[aux_folder_names]` passes; the new `aux_list` ProfilePictures case passes; existing aux ops + roundtrip tests pass. The cover plumbing has not been touched yet, so `[unit][c3]` cover tests still pass with their current expectations (they will be updated in Task 3 when the cover decoupling lands).

- [ ] **Step 11: Commit**

```
git add -u src/ tests/
git add tests/cli/unit/test_aux_folder_canonical_names.cpp
git commit -m "refactor(cli): canonical AuxFolder names + ProfilePictures variant

Rename AuxFolder enum (Pictures->ModelPictures, Bom->BillOfMaterials)
and update flag/JSON/subdir lookups + parse_folder + all callsites and
existing tests. Add ProfilePictures variant + aux_list enumeration
test. New test_aux_folder_canonical_names.cpp pins the exact strings.
No alias compat for old pictures/bom flag values (CLI is pre-release).
Cover plumbing is unchanged in this task — see Task 3."
```

---

## Task 3: Decouple DesignerCover from ProfileCover

**Files:**
- Modify: `src/cli/project_tab_ops.hpp`
- Modify: `src/cli/project_tab_ops.cpp`
- Create: `tests/cli/unit/test_cover_decoupling.cpp`
- Modify: `tests/cli/CMakeLists.txt`
- Modify: `tests/cli/unit/test_project_info_ops.cpp`
- Modify: `tests/cli/unit/test_project_profile_ops.cpp`

Mutual-exclusion of `--cover` + `--cover-name` and `sanitize_aux_name` on `--cover-name` live in the CLI layer, NOT here — those land in Task 4. This task's `info_set` / `profile_set` trust their inputs: if both `cover_path` and `cover_name` are set, `cover_path` wins (assumed-invalid combination prevented by the CLI layer).

- [ ] **Step 1: Add new fields to InfoSetParams / ProfileSetParams + declare helpers**

In `src/cli/project_tab_ops.hpp`:

Find `struct InfoSetParams` and replace it with:
```cpp
struct InfoSetParams {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> license;
    std::optional<std::string> copyright;
    std::optional<std::string> cover_path;  // on-disk path; validated PNG/JPEG
    std::optional<std::string> cover_name;  // basename of an existing aux entry in Model Pictures
};
```

Find `struct ProfileSetParams` and replace it with:
```cpp
struct ProfileSetParams {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> cover_path;
    std::optional<std::string> cover_name;  // basename of an existing aux entry in Profile Pictures
};
```

Find the `namespace detail` block added in Task 1 (containing the `is_png_or_jpeg` declaration). Inside that block, append the new declarations:
```cpp
    // Embed <on_disk_path> as <basename(on_disk_path)> under the aux temp
    // <folder>. Validates PNG/JPEG signature. Returns the basename written.
    // Throws BadCoverImage on bad signature / unreadable source.
    // <folder> must be ModelPictures or ProfilePictures (anything else throws
    // std::invalid_argument as an internal sanity check; CLI parsing never
    // produces those values for the cover paths).
    std::string embed_image_into_folder(Slic3r::Model& model,
                                        AuxFolder folder,
                                        const std::string& on_disk_path);

    // Throws std::out_of_range if Auxiliaries/<folder>/<basename> is not
    // present in the aux temp dir. Maps to ExitCode::unknown_reference (6)
    // via run_mutation.
    void require_image_in_folder(const Slic3r::Model& model,
                                 AuxFolder folder,
                                 const std::string& basename);
```

Add a forward declaration of `Slic3r::Model` near the top of the header (after the existing `#include`s, before `namespace bambu_cli {`). The current header doesn't include `<libslic3r/Model.hpp>`, so a forward decl is required:
```cpp
namespace Slic3r { class Model; }
```

- [ ] **Step 2: Write the failing test**

Create `tests/cli/unit/test_cover_decoupling.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "io.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

TEST_CASE("cover decoupling: designer cover lands in Model Pictures, profile in Profile Pictures",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kPng;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    bambu_cli::ProfileSetParams pp;
    pp.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));

    REQUIRE(s.model.model_info);
    REQUIRE(s.model.profile_info);
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures"   / fs::path(kPng).filename()));
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));

    REQUIRE_FALSE(fs::exists(aux / "Model Pictures"   / "cover.png"));
    REQUIRE_FALSE(fs::exists(aux / "Profile Pictures" / "cover.png"));
}

TEST_CASE("cover decoupling: info clear cover leaves profile cover intact",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kPng;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kJpg;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    bambu_cli::info_clear(s, {"cover"});

    REQUIRE(s.model.model_info->cover_file.empty());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));
}

TEST_CASE("cover decoupling: profile clear cover leaves designer cover intact",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kPng;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kJpg;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    bambu_cli::profile_clear(s, {"cover"});

    REQUIRE(s.model.profile_info->ProfileCover.empty());
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures" / fs::path(kPng).filename()));
}

TEST_CASE("cover decoupling: JPEG cover accepted (was PNG-only)",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));
    REQUIRE(s.model.model_info->cover_file == fs::path(kJpg).filename().string());
}
```

- [ ] **Step 3: Register new test in CMake**

In `tests/cli/CMakeLists.txt`, insert into `BAMBU_CLI_TEST_SOURCES` immediately after `unit/test_aux_folder_canonical_names.cpp`:
```cmake
    unit/test_cover_decoupling.cpp
```

- [ ] **Step 4: Run test to verify it fails**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[cover_decouple]"
```
Expected: the JPEG case fails (old code rejects non-PNG) and the decouple assertions fail (old code lands ProfileCover in Model Pictures with basename `cover.png`).

- [ ] **Step 5: Replace embed_cover + refcount helpers in `project_tab_ops.cpp`**

In `src/cli/project_tab_ops.cpp`, delete the following entire static helpers (they sit near the top of the file under the `// Internal helpers` banner):
- `static const uint8_t kPngSignature[8] = {...};` plus `static bool check_png_signature(...)`
- `static void embed_cover(...)`
- `static bool info_cover_empty(...)`
- `static bool profile_cover_empty(...)`
- `static void delete_cover_file_if_unreferenced(...)`

These are no longer referenced after this task.

In the same area, add the two new helpers:

```cpp
namespace detail {

std::string embed_image_into_folder(Slic3r::Model& model,
                                    AuxFolder folder,
                                    const std::string& on_disk_path) {
    if (folder != AuxFolder::ModelPictures &&
        folder != AuxFolder::ProfilePictures)
        throw std::invalid_argument(
            "embed_image_into_folder: folder must be ModelPictures or ProfilePictures");
    if (!fs::exists(on_disk_path))
        throw BadCoverImage("cover file unreadable: " + on_disk_path);
    if (!is_png_or_jpeg(on_disk_path))
        throw BadCoverImage(
            "cover must be PNG or JPEG (signature mismatch): " + on_disk_path);

    const std::string aux_dir = model.get_auxiliary_file_temp_path();
    const std::string folder_dir = aux_dir + "/" + folder_subdir(folder);
    fs::create_directories(folder_dir);

    const std::string basename = fs::path(on_disk_path).filename().string();
    const std::string dest     = folder_dir + "/" + basename;
    fs::copy_file(on_disk_path, dest, fs::copy_options::overwrite_existing);
    return basename;
}

void require_image_in_folder(const Slic3r::Model& model,
                             AuxFolder folder,
                             const std::string& basename) {
    const std::string aux_dir = model.get_auxiliary_file_temp_path();
    const std::string target  = aux_dir + "/" + folder_subdir(folder) + "/" + basename;
    if (!fs::exists(target))
        throw std::out_of_range("aux entry not found: " + basename);
}

} // namespace detail
```

- [ ] **Step 6: Rewire `info_set` and `info_clear`**

In `src/cli/project_tab_ops.cpp`, find `std::string info_set(ProjectState& state, const InfoSetParams& p)` and replace its body with:
```cpp
std::string info_set(ProjectState& state, const InfoSetParams& p) {
    auto& mi = ensure_model_info(state.model);
    if (p.title)       mi.model_name  = *p.title;
    if (p.description) mi.description = *p.description;
    if (p.license)     mi.license     = *p.license;
    if (p.copyright)   mi.copyright   = *p.copyright;
    // Mutual exclusion + name sanitization are enforced by the CLI layer
    // (commands/project_tab.cpp). If both are set, cover_path wins.
    if (p.cover_path) {
        mi.cover_file = detail::embed_image_into_folder(
            state.model, AuxFolder::ModelPictures, *p.cover_path);
    } else if (p.cover_name) {
        detail::require_image_in_folder(state.model,
                                        AuxFolder::ModelPictures,
                                        *p.cover_name);
        mi.cover_file = *p.cover_name;
    }
    return "applied info edits";
}
```

Find `std::string info_clear(ProjectState& state, const std::vector<std::string>& fields)` and replace its body with:
```cpp
std::string info_clear(ProjectState& state, const std::vector<std::string>& fields) {
    validate_fields(fields, allowed_info_fields());
    auto& mi = ensure_model_info(state.model);
    int n = 0;
    for (const auto& f : fields) {
        if (f == "title")            { mi.model_name  = ""; ++n; }
        else if (f == "description") { mi.description = ""; ++n; }
        else if (f == "license")     { mi.license     = ""; ++n; }
        else if (f == "copyright")   { mi.copyright   = ""; ++n; }
        else if (f == "cover")       { mi.cover_file  = ""; ++n; }
    }
    return "cleared " + std::to_string(n) + " field(s)";
}
```

Note: the on-disk image is no longer deleted by `clear cover` — it remains as a normal aux entry under `Model Pictures/`.

- [ ] **Step 7: Rewire `profile_set` and `profile_clear`**

Find `std::string profile_set(ProjectState& state, const ProfileSetParams& p)` and replace its body with:
```cpp
std::string profile_set(ProjectState& state, const ProfileSetParams& p) {
    auto& pi = ensure_profile_info(state.model);
    if (p.title)       pi.ProfileTile        = *p.title;
    if (p.description) pi.ProfileDescription = *p.description;
    // Mutual exclusion + name sanitization are enforced by the CLI layer.
    if (p.cover_path) {
        pi.ProfileCover = detail::embed_image_into_folder(
            state.model, AuxFolder::ProfilePictures, *p.cover_path);
    } else if (p.cover_name) {
        detail::require_image_in_folder(state.model,
                                        AuxFolder::ProfilePictures,
                                        *p.cover_name);
        pi.ProfileCover = *p.cover_name;
    }
    return "applied profile edits";
}
```

Find `std::string profile_clear(ProjectState& state, const std::vector<std::string>& fields)` and replace its body with:
```cpp
std::string profile_clear(ProjectState& state, const std::vector<std::string>& fields) {
    validate_fields(fields, allowed_profile_fields());
    auto& pi = ensure_profile_info(state.model);
    int n = 0;
    for (const auto& f : fields) {
        if (f == "title")            { pi.ProfileTile        = ""; ++n; }
        else if (f == "description") { pi.ProfileDescription = ""; ++n; }
        else if (f == "cover")       { pi.ProfileCover       = ""; ++n; }
    }
    return "cleared " + std::to_string(n) + " field(s)";
}
```

- [ ] **Step 8: Update existing cover tests in test_project_info_ops.cpp**

Open `tests/cli/unit/test_project_info_ops.cpp` and find every test that exercises `--cover` or `clear cover` on the info side:
- Replace expected `model_info->cover_file == "cover.png"` with the basename of whatever path the test sets (e.g. `"cover_smoke.png"` if the test uses `kPng`).
- Replace existence assertions on `<aux>/Model Pictures/cover.png` with `<aux>/Model Pictures/<basename>`.
- Tests asserting that `info clear cover` deletes the on-disk file → flip the assertion: the file STILL EXISTS after clear. Only the metadata pointer is blanked.
- Tests whose premise is "designer and profile share a single cover.png" → delete entirely. `test_cover_decoupling.cpp` already provides positive coverage of independence.

- [ ] **Step 9: Update existing cover tests in test_project_profile_ops.cpp**

Open `tests/cli/unit/test_project_profile_ops.cpp`. For every test that exercises `--cover` or `clear cover`:
- Replace expected `profile_info->ProfileCover == "cover.png"` with the basename of the source path (e.g. `"cover_smoke.png"`).
- Replace any existence assertion on `<aux>/Model Pictures/cover.png` with `<aux>/Profile Pictures/<basename>` — the profile cover now lives in its own folder.
- Tests asserting shared-cover refcount deletion → flip: the file STILL EXISTS after `profile clear cover`.
- Tests asserting "designer + profile share a file" → delete.

- [ ] **Step 10: Build and run cover tests**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[cover_decouple]"
build/tests/cli/Release/cli_tests.exe "[unit]"
```
Expected: all green.

- [ ] **Step 11: Commit**

```
git add -u src/ tests/
git add tests/cli/unit/test_cover_decoupling.cpp
git commit -m "feat(cli): decouple DesignerCover from ProfileCover

DesignerCover now lives at Auxiliaries/Model Pictures/<basename>;
ProfileCover at Auxiliaries/Profile Pictures/<basename>. Each carries
its own basename in metadata. Removed the shared-cover.png refcount
machinery. info/profile_clear cover now blanks the metadata pointer
only — the on-disk image stays as a normal aux entry. JPEG accepted
alongside PNG via is_png_or_jpeg. Mutual exclusion + name sanitization
of cover_name remain to be wired at the CLI layer in Task 4."
```

---

## Task 4: CLI flag wiring for `--cover-name` + mutual exclusion + sanitization

**Files:**
- Modify: `src/cli/commands/project_tab.cpp`
- Create: `tests/cli/unit/test_cover_pick_by_name.cpp`
- Modify: `tests/cli/e2e/test_project_info.cpp`
- Modify: `tests/cli/e2e/test_project_profile.cpp`
- Modify: `tests/cli/CMakeLists.txt`

Two layers of tests here: unit tests cover the ops-layer contract (`require_image_in_folder` misses throw `std::out_of_range`), and e2e tests cover the CLI-layer contract (mutual exclusion + path-separator rejection produce exit code 1 from the binary).

- [ ] **Step 1: Write the failing unit test (ops-layer contract)**

Create `tests/cli/unit/test_cover_pick_by_name.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "exceptions.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";

TEST_CASE("--cover-name: selects existing image in Model Pictures",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ModelPictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "cover_smoke.png";
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));
    REQUIRE(s.model.model_info->cover_file == "cover_smoke.png");
}

TEST_CASE("--cover-name: throws std::out_of_range when name not present in folder",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "absent.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), std::out_of_range);
}

TEST_CASE("--cover-name: profile_set targets Profile Pictures",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ProfilePictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    bambu_cli::ProfileSetParams pp;
    pp.cover_name = "cover_smoke.png";
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));
    REQUIRE(s.model.profile_info->ProfileCover == "cover_smoke.png");
}
```

Note what is NOT here: tests for `--cover + --cover-name` together and for path-separator rejection. Those validations live at the CLI layer; their tests are e2e and live in Step 4-5 below.

- [ ] **Step 2: Register the unit test in CMake**

In `tests/cli/CMakeLists.txt`, insert into `BAMBU_CLI_TEST_SOURCES` immediately after `unit/test_cover_decoupling.cpp`:
```cmake
    unit/test_cover_pick_by_name.cpp
```

- [ ] **Step 3: Run unit test — expect PASS at this point**

The ops layer already handles the trusting `cover_name` contract after Task 3:
```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[cover_name]"
```
Expected: 3 sections, all PASS.

- [ ] **Step 4: Wire `--cover-name` + mutual exclusion + sanitization into `project info set`**

In `src/cli/commands/project_tab.cpp`, find `struct InfoSetArgs`. Replace the struct definition with:
```cpp
struct InfoSetArgs {
    std::string file, output;
    std::string title, description, license, copyright, cover, cover_name;
    bool has_title{}, has_desc{}, has_license{}, has_copyright{},
         has_cover{}, has_cover_name{};
};
```

Then find the `register_info` function body. Inside the `set` subcommand setup, locate the existing `set->add_option("--cover", set_a->cover, "cover image (PNG only)")` line and replace it with:
```cpp
    set->add_option("--cover",       set_a->cover,
                    "cover image to embed (PNG or JPEG)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
    set->add_option("--cover-name",  set_a->cover_name,
                    "select existing image in Model Pictures as cover "
                    "(mutually exclusive with --cover)")
       ->each([set_a](const std::string&){ set_a->has_cover_name = true; });
```

In the same `set` subcommand callback, find the existing "all args empty" usage-error check (the one referencing `set_a->has_title && set_a->has_desc && ...`) and update both that check and the param assembly:
```cpp
        if (!set_a->has_title && !set_a->has_desc && !set_a->has_license &&
            !set_a->has_copyright && !set_a->has_cover && !set_a->has_cover_name) {
            emit_error(mode, "usage_error",
                       "at least one of --title/--description/--license/"
                       "--copyright/--cover/--cover-name is required");
            std::exit(to_int(ExitCode::usage_error));
        }
        if (set_a->has_cover && set_a->has_cover_name) {
            emit_error(mode, "usage_error",
                       "--cover and --cover-name are mutually exclusive");
            std::exit(to_int(ExitCode::usage_error));
        }
        InfoSetParams p;
        if (set_a->has_title)      p.title       = set_a->title;
        if (set_a->has_desc)       p.description = set_a->description;
        if (set_a->has_license)    p.license     = set_a->license;
        if (set_a->has_copyright)  p.copyright   = set_a->copyright;
        if (set_a->has_cover)      p.cover_path  = set_a->cover;
        if (set_a->has_cover_name) {
            try {
                p.cover_name = sanitize_aux_name(set_a->cover_name);
            } catch (const AuxNameError& e) {
                emit_error(mode, "usage_error", std::string("--cover-name: ") + e.what());
                std::exit(to_int(ExitCode::usage_error));
            }
        }
```

(Match the surrounding existing-pattern for the empty-args usage error; it may already use a different exact message — preserve whatever the file currently emits and only add the two new branches.)

- [ ] **Step 5: Wire `--cover-name` + mutual exclusion + sanitization into `project profile set`**

Same pattern. In `src/cli/commands/project_tab.cpp`, find `struct ProfileSetArgs` and replace with:
```cpp
struct ProfileSetArgs {
    std::string file, output;
    std::string title, description, cover, cover_name;
    bool has_title{}, has_desc{}, has_cover{}, has_cover_name{};
};
```

Find `register_profile`. In its `set` subcommand setup, locate `set->add_option("--cover", set_a->cover, "cover image (PNG only)")` and replace with:
```cpp
    set->add_option("--cover",       set_a->cover,
                    "cover image to embed (PNG or JPEG)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
    set->add_option("--cover-name",  set_a->cover_name,
                    "select existing image in Profile Pictures as cover "
                    "(mutually exclusive with --cover)")
       ->each([set_a](const std::string&){ set_a->has_cover_name = true; });
```

In the same callback, update the empty-args check + param assembly:
```cpp
        if (!set_a->has_title && !set_a->has_desc &&
            !set_a->has_cover && !set_a->has_cover_name) {
            emit_error(mode, "usage_error",
                       "at least one of --title/--description/--cover/--cover-name is required");
            std::exit(to_int(ExitCode::usage_error));
        }
        if (set_a->has_cover && set_a->has_cover_name) {
            emit_error(mode, "usage_error",
                       "--cover and --cover-name are mutually exclusive");
            std::exit(to_int(ExitCode::usage_error));
        }
        ProfileSetParams p;
        if (set_a->has_title)      p.title       = set_a->title;
        if (set_a->has_desc)       p.description = set_a->description;
        if (set_a->has_cover)      p.cover_path  = set_a->cover;
        if (set_a->has_cover_name) {
            try {
                p.cover_name = sanitize_aux_name(set_a->cover_name);
            } catch (const AuxNameError& e) {
                emit_error(mode, "usage_error", std::string("--cover-name: ") + e.what());
                std::exit(to_int(ExitCode::usage_error));
            }
        }
```

(Add `#include "../exceptions.hpp"` to the top of `commands/project_tab.cpp` if it isn't already present — `AuxNameError` lives there.)

- [ ] **Step 6: Add e2e tests for mutual exclusion and sanitization (CLI layer)**

Open `tests/cli/e2e/test_project_info.cpp`. Append two new TEST_CASEs at the end:

```cpp
TEST_CASE("info set: --cover and --cover-name together fails with exit 1",
          "[e2e][project_info][cover_name]") {
    using namespace bambu_cli_test;
    const std::string src = canonical_committed_3mf();
    REQUIRE_FALSE(src.empty());
    const std::string dst = fresh_temp_path(".3mf");
    REQUIRE(read_zip_entry(src, "3D/3dmodel.model").size() > 0);  // sanity

    const auto result = spawn_cli({
        "project", "info", "set",
        src,
        "--output", dst,
        "--cover", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png",
        "--cover-name", "anything.png",
    });
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.stderr_text.find("mutually exclusive") != std::string::npos);
}

TEST_CASE("info set: --cover-name with path separator fails with exit 1",
          "[e2e][project_info][cover_name]") {
    using namespace bambu_cli_test;
    const std::string src = canonical_committed_3mf();
    const std::string dst = fresh_temp_path(".3mf");

    const auto result = spawn_cli({
        "project", "info", "set",
        src,
        "--output", dst,
        "--cover-name", "subdir/cover.png",
    });
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.stderr_text.find("--cover-name") != std::string::npos);
}
```

Open `tests/cli/e2e/test_project_profile.cpp`. Append the mirror cases (same structure, `info` → `profile`, message matchers unchanged):

```cpp
TEST_CASE("profile set: --cover and --cover-name together fails with exit 1",
          "[e2e][project_profile][cover_name]") {
    using namespace bambu_cli_test;
    const std::string src = canonical_committed_3mf();
    const std::string dst = fresh_temp_path(".3mf");

    const auto result = spawn_cli({
        "project", "profile", "set",
        src,
        "--output", dst,
        "--cover", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png",
        "--cover-name", "anything.png",
    });
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.stderr_text.find("mutually exclusive") != std::string::npos);
}

TEST_CASE("profile set: --cover-name with path separator fails with exit 1",
          "[e2e][project_profile][cover_name]") {
    using namespace bambu_cli_test;
    const std::string src = canonical_committed_3mf();
    const std::string dst = fresh_temp_path(".3mf");

    const auto result = spawn_cli({
        "project", "profile", "set",
        src,
        "--output", dst,
        "--cover-name", "subdir/cover.png",
    });
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.stderr_text.find("--cover-name") != std::string::npos);
}
```

(Both e2e files already include `test_helpers.hpp` and have the `using namespace bambu_cli_test` precedent — match the existing style.)

- [ ] **Step 7: Build the CLI binary + sanity-check help text**

```
cmake --build build --target bambu-cli --config Release --parallel 2
build/src/cli/Release/bambu-cli.exe project info set --help
build/src/cli/Release/bambu-cli.exe project profile set --help
```
Expected: both `--cover` and `--cover-name` listed in help with their new descriptions.

- [ ] **Step 8: Run all tests**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[cover_name]"
build/tests/cli/Release/cli_tests.exe
```
Expected: unit `[cover_name]` 3 sections green; e2e `[cover_name]` 4 sections green; full suite green.

- [ ] **Step 9: Commit**

```
git add -u src/ tests/
git add tests/cli/unit/test_cover_pick_by_name.cpp
git commit -m "feat(cli): --cover-name to select existing image as cover

project info set / project profile set accept --cover-name NAME
alongside --cover PATH. --cover-name picks a file already present in
Model Pictures (info) or Profile Pictures (profile) as the cover
without re-embedding. Mutual exclusion + sanitize_aux_name(--cover-name)
are enforced at the CLI layer with exit 1 on violation; the ops layer
trusts its inputs. require_image_in_folder miss -> exit 6."
```

---

## Task 5: New invariant guard — `check_auxiliary_passthrough`

**Files:**
- Modify: `src/cli/invariant_guard.hpp`
- Modify: `src/cli/invariant_guard.cpp`
- Create: `tests/cli/unit/test_invariant_aux_passthrough.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/cli/unit/test_invariant_aux_passthrough.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "invariant_guard.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

static std::string make_zip(const std::vector<std::pair<std::string, std::string>>& entries) {
    const fs::path p = fs::temp_directory_path() /
                       fs::unique_path("auxpass-%%%%-%%%%.zip");
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_writer_init_file(&zip, p.string().c_str(), 0));
    for (const auto& e : entries) {
        REQUIRE(mz_zip_writer_add_mem(&zip, e.first.c_str(),
                                      e.second.data(), e.second.size(),
                                      MZ_DEFAULT_COMPRESSION));
    }
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return p.string();
}

TEST_CASE("aux passthrough: identical archives pass",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"Auxiliaries/Model Pictures/a.jpg", "ABC"},
                                {"Auxiliaries/Assembly Guide/m.pdf", "PDF "}});
    const auto post = make_zip({{"Auxiliaries/Model Pictures/a.jpg", "ABC"},
                                {"Auxiliaries/Assembly Guide/m.pdf", "PDF "}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    REQUIRE(err.empty());
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: missing entry in post fails",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"Auxiliaries/Profile Pictures/x.jpg", "XYZ"}});
    const auto post = make_zip({});
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    REQUIRE(err.find("Profile Pictures/x.jpg") != std::string::npos);
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: content drift fails",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"Auxiliaries/Others/note.txt", "hello"}});
    const auto post = make_zip({{"Auxiliaries/Others/note.txt", "HELLO"}});
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    REQUIRE(err.find("note.txt") != std::string::npos);
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: non-Auxiliary entries ignored",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"3D/3dmodel.model", "X"},
                                {"Auxiliaries/Others/k.txt", "K"}});
    const auto post = make_zip({{"3D/3dmodel.model", "Y"},
                                {"Auxiliaries/Others/k.txt", "K"}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: empty pre yields pass",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"3D/3dmodel.model", "X"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    fs::remove(pre); fs::remove(post);
}
```

Register in `tests/cli/CMakeLists.txt` immediately after `unit/test_cover_pick_by_name.cpp`:
```cmake
    unit/test_invariant_aux_passthrough.cpp
```

- [ ] **Step 2: Run test to verify it fails (compile error)**

```
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: build fails — `bambu_cli::check_auxiliary_passthrough` not declared.

- [ ] **Step 3: Declare the new check in `invariant_guard.hpp`**

In `src/cli/invariant_guard.hpp`, find the closing `} // namespace bambu_cli`. Immediately before it, insert:

```cpp
// Post-write check: every regular file under "Auxiliaries/" in <pre_path>
// must be present at the same archive path in <post_path> with
// byte-identical contents. First mismatch is written to *err_out and the
// function returns false. Empty err_out on success.
//
// Pre = the source archive that was loaded; post = the tmp archive that
// store_bbs_3mf just produced. Used to detect accidental aux folder
// renames, missing files, or content corruption.
bool check_auxiliary_passthrough(const std::string& pre_path,
                                 const std::string& post_path,
                                 std::string* err_out);
```

- [ ] **Step 4: Implement `check_auxiliary_passthrough` in `invariant_guard.cpp`**

At the top of `src/cli/invariant_guard.cpp`, ensure these includes are present (add any missing):
```cpp
#include <miniz.h>
#include <cstring>
#include <vector>
```

Add the helpers + function. Place them in an anonymous namespace at the top of the file (or extend the existing anonymous namespace if one is already there):

```cpp
namespace {

struct ZipFile {
    mz_zip_archive zip;
    bool open = false;
    ZipFile() { std::memset(&zip, 0, sizeof(zip)); }
    ~ZipFile() { if (open) mz_zip_reader_end(&zip); }
};

static bool open_zip(ZipFile& z, const std::string& path) {
    z.open = mz_zip_reader_init_file(&z.zip, path.c_str(), 0);
    return z.open;
}

static bool read_entry_bytes(mz_zip_archive& zip, mz_uint idx, std::string& out) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, idx, &st)) return false;
    out.resize(static_cast<size_t>(st.m_uncomp_size));
    if (st.m_uncomp_size == 0) return true;
    return mz_zip_reader_extract_to_mem(&zip, idx, &out[0],
                                        static_cast<size_t>(st.m_uncomp_size), 0);
}

} // namespace

bool check_auxiliary_passthrough(const std::string& pre_path,
                                 const std::string& post_path,
                                 std::string* err_out) {
    auto fail = [err_out](std::string msg) {
        if (err_out) *err_out = std::move(msg);
        return false;
    };
    if (err_out) err_out->clear();

    ZipFile pre, post;
    if (!open_zip(pre,  pre_path))  return fail("cannot open pre archive: "  + pre_path);
    if (!open_zip(post, post_path)) return fail("cannot open post archive: " + post_path);

    const mz_uint n_pre = mz_zip_reader_get_num_files(&pre.zip);
    for (mz_uint i = 0; i < n_pre; ++i) {
        char name_buf[1024] = {};
        mz_zip_reader_get_filename(&pre.zip, i, name_buf, sizeof(name_buf));
        std::string name = name_buf;
        if (name.rfind("Auxiliaries/", 0) != 0) continue;
        if (mz_zip_reader_is_file_a_directory(&pre.zip, i)) continue;

        const int idx_post = mz_zip_reader_locate_file(&post.zip, name.c_str(), nullptr, 0);
        if (idx_post < 0) return fail("missing in post: " + name);

        std::string pre_bytes, post_bytes;
        if (!read_entry_bytes(pre.zip,  i,                              pre_bytes))
            return fail("read failed for pre: "  + name);
        if (!read_entry_bytes(post.zip, static_cast<mz_uint>(idx_post), post_bytes))
            return fail("read failed for post: " + name);
        if (pre_bytes != post_bytes)
            return fail("content drift: " + name);
    }
    return true;
}
```

- [ ] **Step 5: Run test to verify it passes**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[invariant_aux]"
```
Expected: 5 sections, all PASS.

- [ ] **Step 6: Commit**

```
git add -u src/cli/invariant_guard.hpp src/cli/invariant_guard.cpp tests/cli/CMakeLists.txt
git add tests/cli/unit/test_invariant_aux_passthrough.cpp
git commit -m "feat(cli): check_auxiliary_passthrough invariant guard

Verifies every Auxiliaries/* entry in the source archive survives
load->store with byte-identical contents. Catches accidental folder
renames, missing files, or content drift. Not yet wired into run_guard
— that lands in Task 7 once both new checks exist."
```

---

## Task 6: New invariant guard — `check_cover_references_resolve`

**Files:**
- Modify: `src/cli/invariant_guard.hpp`
- Modify: `src/cli/invariant_guard.cpp`
- Create: `tests/cli/unit/test_invariant_cover_references.cpp`
- Modify: `tests/cli/CMakeLists.txt`

Note on `extract_metadata`: this helper does a literal substring match for `name="KEY">VALUE</metadata>`. It is **by-construction** — it matches Bambu's exact output shape (one metadata element per line, attribute order `name="..."` first, value immediately following). It is NOT a general XML parser and would miss attribute reordering, comments, or namespace prefixes. Acceptable here because we run it on archives just produced by `store_bbs_3mf` itself, whose output shape is fixed. A code comment in Step 4 documents this.

- [ ] **Step 1: Write the failing test**

Create `tests/cli/unit/test_invariant_cover_references.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "invariant_guard.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

static const std::string kModelHead =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<model xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
static const std::string kModelTail =
    " <resources/>\n <build/>\n</model>\n";

static std::string model_with(const std::string& designer_cover,
                              const std::string& profile_cover) {
    std::string s = kModelHead;
    if (!designer_cover.empty())
        s += " <metadata name=\"DesignerCover\">" + designer_cover + "</metadata>\n";
    if (!profile_cover.empty())
        s += " <metadata name=\"ProfileCover\">" + profile_cover + "</metadata>\n";
    s += kModelTail;
    return s;
}

static std::string make_zip(const std::vector<std::pair<std::string, std::string>>& entries) {
    const fs::path p = fs::temp_directory_path() /
                       fs::unique_path("covref-%%%%-%%%%.zip");
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_writer_init_file(&zip, p.string().c_str(), 0));
    for (const auto& e : entries)
        REQUIRE(mz_zip_writer_add_mem(&zip, e.first.c_str(),
                                      e.second.data(), e.second.size(),
                                      MZ_DEFAULT_COMPRESSION));
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return p.string();
}

TEST_CASE("cover refs: empty metadata passes", "[unit][invariant_covref]") {
    const auto z = make_zip({{"3D/3dmodel.model", model_with("", "")}});
    std::string err;
    REQUIRE(bambu_cli::check_cover_references_resolve(z, &err));
    fs::remove(z);
}

TEST_CASE("cover refs: both covers present in canonical folders passes",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("a.jpg", "b.jpg")},
        {"Auxiliaries/Model Pictures/a.jpg",   "AAA"},
        {"Auxiliaries/Profile Pictures/b.jpg", "BBB"},
    });
    std::string err;
    REQUIRE(bambu_cli::check_cover_references_resolve(z, &err));
    fs::remove(z);
}

TEST_CASE("cover refs: DesignerCover references absent file fails",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("missing.png", "")},
    });
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("DesignerCover") != std::string::npos);
    REQUIRE(err.find("missing.png")   != std::string::npos);
    fs::remove(z);
}

TEST_CASE("cover refs: ProfileCover references absent file fails",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("", "missing.jpg")},
    });
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("ProfileCover") != std::string::npos);
    fs::remove(z);
}
```

Register in `tests/cli/CMakeLists.txt` immediately after `unit/test_invariant_aux_passthrough.cpp`:
```cmake
    unit/test_invariant_cover_references.cpp
```

- [ ] **Step 2: Run test to verify it fails (compile error)**

```
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: build fails — `check_cover_references_resolve` not declared.

- [ ] **Step 3: Declare in `invariant_guard.hpp`**

In `src/cli/invariant_guard.hpp`, immediately after the `check_auxiliary_passthrough` declaration added in Task 5, insert:

```cpp
// Verify DesignerCover and ProfileCover metadata in <archive_path>'s
// 3D/3dmodel.model reference filenames that exist in Auxiliaries/Model
// Pictures/ and Auxiliaries/Profile Pictures/ respectively. Empty metadata
// values are valid and pass. First mismatch is written to *err_out and
// the function returns false.
bool check_cover_references_resolve(const std::string& archive_path,
                                    std::string* err_out);
```

- [ ] **Step 4: Implement in `invariant_guard.cpp`**

Inside the same anonymous namespace as `ZipFile`/`open_zip`/`read_entry_bytes` (added in Task 5), append:

```cpp
// extract_metadata is a by-construction substring matcher, NOT a general
// XML parser. It assumes Bambu's exact output shape for <metadata> elements:
// (a) attribute order is `name="KEY"` first; (b) the value follows immediately
// after the closing `">` of the name attribute; (c) the closing tag is the
// literal "</metadata>" on the same logical text run. Reordered attributes,
// XML comments inside the element, or namespace prefixes would all defeat
// this. Acceptable here because we only run it on archives that
// store_bbs_3mf itself just produced (see src/libslic3r/Format/bbs_3mf.cpp).
static std::string extract_metadata(const std::string& xml, const std::string& key) {
    const std::string needle = "name=\"" + key + "\">";
    const auto p = xml.find(needle);
    if (p == std::string::npos) return "";
    const auto start = p + needle.size();
    const auto end   = xml.find("</metadata>", start);
    if (end == std::string::npos) return "";
    return xml.substr(start, end - start);
}
```

Outside the anonymous namespace, add the function:

```cpp
bool check_cover_references_resolve(const std::string& archive_path,
                                    std::string* err_out) {
    auto fail = [err_out](std::string msg) {
        if (err_out) *err_out = std::move(msg);
        return false;
    };
    if (err_out) err_out->clear();

    ZipFile z;
    if (!open_zip(z, archive_path))
        return fail("cannot open archive: " + archive_path);

    const int idx_model = mz_zip_reader_locate_file(&z.zip, "3D/3dmodel.model", nullptr, 0);
    if (idx_model < 0) return fail("missing 3D/3dmodel.model");

    std::string model_xml;
    if (!read_entry_bytes(z.zip, static_cast<mz_uint>(idx_model), model_xml))
        return fail("read failed for 3D/3dmodel.model");

    const std::string designer = extract_metadata(model_xml, "DesignerCover");
    const std::string profile  = extract_metadata(model_xml, "ProfileCover");

    auto check_one = [&](const std::string& meta_key,
                         const std::string& subdir,
                         const std::string& basename) -> bool {
        if (basename.empty()) return true;
        const std::string target = "Auxiliaries/" + subdir + "/" + basename;
        const int idx = mz_zip_reader_locate_file(&z.zip, target.c_str(), nullptr, 0);
        if (idx < 0)
            return fail(meta_key + " references missing entry: " + basename
                        + " (expected at " + target + ")");
        return true;
    };

    if (!check_one("DesignerCover", "Model Pictures",   designer)) return false;
    if (!check_one("ProfileCover",  "Profile Pictures", profile))  return false;
    return true;
}
```

- [ ] **Step 5: Run test**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe "[invariant_covref]"
```
Expected: 4 sections, all PASS.

- [ ] **Step 6: Commit**

```
git add -u src/cli/invariant_guard.hpp src/cli/invariant_guard.cpp tests/cli/CMakeLists.txt
git add tests/cli/unit/test_invariant_cover_references.cpp
git commit -m "feat(cli): check_cover_references_resolve invariant guard

Verifies DesignerCover/ProfileCover metadata in the saved 3D/3dmodel.model
references filenames that actually exist in Model Pictures / Profile
Pictures. Empty values are valid. extract_metadata is documented as a
by-construction substring matcher, not a general XML parser. Not yet
wired into run_guard."
```

---

## Task 7: Wire both new guards into `run_guard`

**Files:**
- Modify: `src/cli/invariant_guard.hpp`
- Modify: `src/cli/invariant_guard.cpp`

`auxiliary_passthrough` needs the PRE archive path (the source archive that was loaded) — available as `state.source_path`. `cover_references_resolve` only needs the POST path.

- [ ] **Step 1: Update the `failed_check` documentation in the header**

In `src/cli/invariant_guard.hpp`, find the `GuardResult` block describing checks (a), (b), (c). After the (c) line, append:

```cpp
//   (d) auxiliary passthrough — every Auxiliaries/* file in
//       state.source_path is present at the same archive path in the
//       saved archive with byte-identical content. (Skipped if
//       state.source_path is empty, e.g. for project_init from
//       template; that case is covered by check_thumbnails_in_archive.)
//   (e) cover references resolve — DesignerCover / ProfileCover
//       metadata reference existing files in the canonical folders.
```

Update the `failed_check` field comment to include the new strings:
```cpp
    std::string failed_check;     // "rels", "thumbnails", "config_roundtrip",
                                  // "auxiliary_passthrough", or "cover_references_resolve"
```

- [ ] **Step 2: Extend `run_guard` to call the new checks**

In `src/cli/invariant_guard.cpp`, find `run_guard(...)`. After the three existing check blocks succeed and before the final `r.ok = true; return r;`, insert:

```cpp
    // (d) auxiliary passthrough — only meaningful when we have a source
    // archive (i.e. not project_init from template).
    if (!state.source_path.empty() && fs::exists(state.source_path)) {
        std::string ax_err;
        if (!check_auxiliary_passthrough(state.source_path, saved_path, &ax_err)) {
            r.failed_check   = "auxiliary_passthrough";
            r.failure_detail = ax_err;
            return r;
        }
    }

    // (e) cover references resolve.
    {
        std::string cov_err;
        if (!check_cover_references_resolve(saved_path, &cov_err)) {
            r.failed_check   = "cover_references_resolve";
            r.failure_detail = cov_err;
            return r;
        }
    }
```

(Verify `<boost/filesystem.hpp>` is included at the top; add if missing.)

- [ ] **Step 3: Build and run the entire test suite**

```
cmake --build build --target cli_tests --config Release --parallel 2
build/tests/cli/Release/cli_tests.exe
```
Expected: all green. Per pre-conditions (plan header): no committed `.3mf` fixture contains `Auxiliaries/*` entries, so `auxiliary_passthrough` passes vacuously for the existing roundtrip suite.

If any future fixture is added that produces aux content via the CLI's `aux add`, the guard catches accidental folder-name regressions automatically.

- [ ] **Step 4: Commit**

```
git add -u src/cli/invariant_guard.hpp src/cli/invariant_guard.cpp
git commit -m "feat(cli): wire aux_passthrough + cover_refs into run_guard

Every save_project now also enforces (d) auxiliary passthrough and
(e) cover references resolve. (d) is skipped when state.source_path is
empty (project init from template) since there is no pre-archive to
diff against."
```

---

## Task 8: Round-trip test against `test_reference.3mf`

**Files:**
- Create: `tests/cli/fixtures/test_reference.3mf` (binary; copy of the user-supplied file)
- Modify: `tests/cli/CMakeLists.txt`
- Create: `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp`

- [ ] **Step 1: Copy the fixture into the repo**

```
cp "C:/Users/ildarcheg/Documents/GitHub/test_reference.3mf" tests/cli/fixtures/test_reference.3mf
ls -l tests/cli/fixtures/test_reference.3mf
```
Expected: ~4.5 MB. Contains `Auxiliaries/Model Pictures/50calpellet.jpg`, `Auxiliaries/Profile Pictures/5.45x39mm.jpg`, `Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf`.

- [ ] **Step 2: Expose the fixture path to tests via CMake**

In `tests/cli/CMakeLists.txt`, find the `target_compile_definitions(cli_tests PRIVATE ...)` block. Immediately after the existing `BAMBU_CLI_FIXTURE_3MF=...` line, add:

```cmake
    BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/test_reference.3mf"
```

- [ ] **Step 3: Register the new round-trip test source**

In `tests/cli/CMakeLists.txt`, in `BAMBU_CLI_TEST_SOURCES`, find `roundtrip/test_project_tab.cpp` and insert immediately after it:

```cmake
    roundtrip/test_reference_3mf_passthrough.cpp
```

- [ ] **Step 4: Write the round-trip test**

Create `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "io.hpp"
#include "invariant_guard.hpp"
#include "project_state.hpp"

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
    REQUIRE(mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(idx),
                                         &buf[0], buf.size(), 0));
    mz_zip_reader_end(&zip);
    return buf;
}

static bool has_entry(const std::string& archive, const std::string& name) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, archive.c_str(), 0)) return false;
    const int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    mz_zip_reader_end(&zip);
    return idx >= 0;
}

TEST_CASE("test_reference.3mf round-trip preserves canonical aux layout",
          "[roundtrip][reference_passthrough]") {
    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    REQUIRE(fs::exists(src));

    bambu_cli::ProjectState state;
    auto lr = bambu_cli::load_project(src, state);
    REQUIRE(lr.ok);

    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("refmf-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(state, out.string());
    REQUIRE(sr.ok);

    const auto pre_jpg_a  = read_entry(src,           "Auxiliaries/Model Pictures/50calpellet.jpg");
    const auto post_jpg_a = read_entry(out.string(),  "Auxiliaries/Model Pictures/50calpellet.jpg");
    REQUIRE(pre_jpg_a == post_jpg_a);

    const auto pre_jpg_b  = read_entry(src,           "Auxiliaries/Profile Pictures/5.45x39mm.jpg");
    const auto post_jpg_b = read_entry(out.string(),  "Auxiliaries/Profile Pictures/5.45x39mm.jpg");
    REQUIRE(pre_jpg_b == post_jpg_b);

    const auto pre_pdf  = read_entry(src,           "Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf");
    const auto post_pdf = read_entry(out.string(),  "Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf");
    REQUIRE(pre_pdf == post_pdf);

    REQUIRE(has_entry(out.string(), "Auxiliaries/.thumbnails/thumbnail_3mf.png"));
    REQUIRE(has_entry(out.string(), "Auxiliaries/.thumbnails/thumbnail_middle.png"));
    REQUIRE(has_entry(out.string(), "Auxiliaries/.thumbnails/thumbnail_small.png"));

    const auto model_xml = read_entry(out.string(), "3D/3dmodel.model");
    REQUIRE(model_xml.find("name=\"DesignerCover\">50calpellet.jpg<") != std::string::npos);
    REQUIRE(model_xml.find("name=\"ProfileCover\">5.45x39mm.jpg<")   != std::string::npos);

    REQUIRE(model_xml.find("name=\"Title\">Test Project for reference<")            != std::string::npos);
    REQUIRE(model_xml.find("name=\"ProfileTitle\">Test profile name for reference<") != std::string::npos);

    std::string ax_err, cov_err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(src, out.string(), &ax_err));
    REQUIRE(ax_err.empty());
    REQUIRE(bambu_cli::check_cover_references_resolve(out.string(), &cov_err));
    REQUIRE(cov_err.empty());

    fs::remove(out);
}
```

- [ ] **Step 5: Configure CMake (fixture macro requires a re-config)**

```
cmake -B build -S .
cmake --build build --target cli_tests --config Release --parallel 2
```
Expected: build succeeds.

- [ ] **Step 6: Run the round-trip test**

```
build/tests/cli/Release/cli_tests.exe "[reference_passthrough]"
```
Expected: PASS. If it fails:
- "missing in post: Auxiliaries/Profile Pictures/..." → Task 3 cover plumbing isn't writing the right folder. Investigate `profile_set`.
- DesignerCover metadata value drift → check `info_set`'s `cover_file` assignment is a basename.
- Title/Description drift → unrelated; check existing config-roundtrip guard.

- [ ] **Step 7: Run the full suite to confirm nothing regressed**

```
build/tests/cli/Release/cli_tests.exe
```
Expected: all green. The new tests should bring the count above the prior baseline of 235 cases / 1170 assertions.

- [ ] **Step 8: Commit**

```
git add tests/cli/fixtures/test_reference.3mf \
        tests/cli/CMakeLists.txt \
        tests/cli/roundtrip/test_reference_3mf_passthrough.cpp
git commit -m "test(cli): round-trip test_reference.3mf passthrough

New fixture is a verbatim copy of a real Bambu Studio-produced 3MF.
Asserts the load->store->reload cycle preserves canonical aux folder
names (Model Pictures / Profile Pictures / Assembly Guide), file
content byte-for-byte, cover metadata as basename-only references, and
top-level Title/ProfileTitle metadata. Also runs the two new invariant
guards directly against the saved archive."
```

---

## Task 9: Refresh CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update the aux-folder bullet**

In `CLAUDE.md`, find the `## Sibling-fork divergences — LEGITIMATE, do not try to "fix"` section and locate the `**Aux folder names:**` bullet (text currently begins `Bambu uses` Pictures / Bom / AssemblyGuide / Others ``). Replace the entire bullet with:

```
- **Aux folder names:** Bambu's canonical layout is `Model Pictures` /
  `Profile Pictures` / `Bill of Materials` / `Assembly Guide` / `Others`
  (TitleCase + spaces, per `src/slic3r/GUI/Auxiliary.hpp:75` and
  `src/slic3r/GUI/Project.cpp:214-226`, verified against
  `tests/cli/fixtures/test_reference.3mf`). The CLI emits exactly these
  names. (Prior versions of this note incorrectly stated
  `Pictures` / `Bom` / `AssemblyGuide` — that was a CLI bug, since
  fixed; see `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md`.)
  Orca uses lowercase-hyphenated; we explicitly do not match Orca here.
```

- [ ] **Step 2: Commit**

```
git add CLAUDE.md
git commit -m "docs(claude.md): correct aux-folder divergence note

The previous note documented a CLI bug as if it were a deliberate
divergence and told future agents not to normalize. Replace with the
actual canonical layout and a pointer to the new note."
```

---

## Task 10: New notes file + status.md entry

**Files:**
- Create: `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md`
- Modify: `docs/cli/status.md`

- [ ] **Step 1: Write the notes file**

Create `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md`:

```markdown
# Canonical Aux Folder Layout (post-2026-05-26)

## What the canonical layout is

Bambu Studio writes auxiliary content under `Auxiliaries/` using these
exact subdir names, with spaces and TitleCase:

| Subdir              | Holds                                              | Metadata reference         |
|---------------------|----------------------------------------------------|----------------------------|
| `Model Pictures`    | Model preview images (PNG/JPEG)                    | `DesignerCover` (basename) |
| `Profile Pictures`  | Profile preview images (PNG/JPEG)                  | `ProfileCover`  (basename) |
| `Bill of Materials` | BOM documents                                      | n/a                        |
| `Assembly Guide`    | Assembly/print guide documents (PDF, txt, etc.)    | n/a                        |
| `Others`            | Anything else                                      | n/a                        |
| `.thumbnails`       | Cover-thumbnail PNGs at 3MF/middle/small sizes     | from `_rels/.rels`         |

Sources of truth:
- `src/slic3r/GUI/Auxiliary.hpp:75` — `s_default_folders` array
- `src/slic3r/GUI/Project.cpp:214-226` — JSON dispatch
- `tests/cli/fixtures/test_reference.3mf` — real Bambu output

## DesignerCover vs ProfileCover (decoupled 2026-05-26)

Before this change, the CLI shared a single `Model Pictures/cover.png`
between `model_info.cover_file` and `profile_info.ProfileCover` via a
refcount in `project_tab_ops.cpp`. That was wrong — Bambu treats them
as fully separate: each lives in its own folder under its own basename.

After this change:
- `project info set --cover X.png|X.jpg` embeds the image at
  `Model Pictures/<basename>` and sets `model_info.cover_file = "<basename>"`.
- `project profile set --cover X.png|X.jpg` does the same for
  `Profile Pictures/` and `profile_info.ProfileCover`.
- `info clear cover` / `profile clear cover` blank the metadata pointer
  only — the on-disk image is left in place as a normal aux entry.
  Users can delete it explicitly via `project aux remove --folder
  model-pictures --name X.jpg`.
- `--cover-name NAME` (new) picks an existing image in the folder as
  the cover without re-embedding. Validation (mutual exclusion with
  `--cover`, `sanitize_aux_name`) happens at the CLI layer; the ops
  layer trusts its inputs.

## Why this matters

Round-tripping any real Bambu-produced 3MF (e.g. the new
`test_reference.3mf` fixture) requires the canonical folder names — if
the CLI re-saves under `Pictures/`, Bambu Studio no longer recognizes
the contents in its Project tab. The `check_auxiliary_passthrough`
invariant guard catches any future regression by diffing every
`Auxiliaries/*` entry between the source and the saved archive.

## Why CLAUDE.md previously got this wrong

The early audit (pre-M1) mis-read which folder names were canonical
and recorded the CLI's then-incorrect layout as a "deliberate
divergence from Orca." The 2026-05-26 audit against
`test_reference.3mf` corrected this. The current `CLAUDE.md` divergence
note points here.
```

- [ ] **Step 2: Update `docs/cli/status.md`**

Open `docs/cli/status.md` and append at the end:

```markdown
## Phase G — Canonical aux folder layout (2026-05-26)

- [x] AuxFolder enum renamed to canonical names (`ModelPictures`,
      `ProfilePictures`, `BillOfMaterials`, `AssemblyGuide`, `Others`).
- [x] DesignerCover / ProfileCover decoupled into own folders + own basenames.
- [x] `--cover-name` selects existing image in folder; mutual exclusion +
      `sanitize_aux_name` enforced at CLI layer.
- [x] PNG + JPEG accepted (via `is_png_or_jpeg`).
- [x] `check_auxiliary_passthrough` + `check_cover_references_resolve`
      invariant guards live in the save path.
- [x] `tests/cli/fixtures/test_reference.3mf` committed; round-trip
      test asserts canonical layout preservation.
- [ ] Manual GUI smoke: open a CLI-produced 3MF in Bambu Studio and
      confirm the Project tab renders Model Pictures / Profile Pictures
      / Assembly Guide tabs correctly with the embedded covers and PDF.
```

- [ ] **Step 3: Commit**

```
git add docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md docs/cli/status.md
git commit -m "docs(cli): canonical aux layout note + Phase G status entry

Records why the canonical names matter, what changed in the
DesignerCover/ProfileCover decoupling, and why the prior CLAUDE.md
note was wrong. status.md gets the new Phase G block; only open item
is the manual Bambu Studio GUI smoke (carried over from M0-M10)."
```

---

## Self-Review Checklist

After all tasks land:

1. **Spec coverage**:
   - §1 corrected "CLI today" table → Task 9 + Task 10 (CLAUDE.md + notes file)
   - §3.1 enum → Task 2
   - §3.2 flag/JSON/subdir mapping → Task 2
   - §3.3 `--cover-name` + layering of validation → Task 3 (ops), Task 4 (CLI exit-code semantics)
   - §3.4 clear-cover behavior change → Task 3
   - §4 internals → Task 3
   - §5.1 invariants → Task 8 (round-trip asserts)
   - §5.2 guards → Tasks 5, 6, 7
   - §6 tests → Tasks 1, 2, 3, 4, 5, 6, 8
   - §7 files touched → all tasks
   - Docs (§7 last block) → Tasks 9, 10

2. **Placeholder scan**: every step has either real code or an exact command + expected output. Anchor strings replace line numbers throughout (e.g. "find `static AuxFolder parse_folder`" instead of "lines 247-255").

3. **Type consistency**:
   - `AuxFolder` enum names: `ModelPictures` / `ProfilePictures` / `BillOfMaterials` / `AssemblyGuide` / `Others` — pinned in Task 2 Step 1 test, defined in Task 2 Step 2 header, used everywhere downstream.
   - `InfoSetParams::cover_name` / `ProfileSetParams::cover_name` — added in Task 3 Step 1, populated by Task 3 Steps 6-7 (ops layer trust contract) and Task 4 Steps 4-5 (CLI layer validation).
   - `detail::is_png_or_jpeg` — declared Task 1 Step 3, used in `detail::embed_image_into_folder` body in Task 3 Step 5.
   - `detail::embed_image_into_folder` / `require_image_in_folder` — declared Task 3 Step 1, defined Task 3 Step 5, used Task 3 Steps 6-7.
   - `check_auxiliary_passthrough` — declared Task 5 Step 3, defined Task 5 Step 4, wired Task 7 Step 2, used directly in Task 8 round-trip.
   - `check_cover_references_resolve` — declared Task 6 Step 3, defined Task 6 Step 4, wired Task 7 Step 2, used directly in Task 8 round-trip.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-26-canonical-aux-layout.md`.

**Task dependency graph:**
```
1 → 2 → 3 → 4 → 8 → 9, 10
        ↘ 5 ↘
          6 ↘
            7 → 8
```
- Task 1 is standalone.
- Task 2 depends on Task 1 (uses `detail::` namespace).
- Task 3 depends on Task 2 (enum names).
- Task 4 depends on Task 3 (cover_name field in InfoSetParams/ProfileSetParams).
- Tasks 5 and 6 are independent of each other; both can start after Task 4.
- Task 7 depends on 5 and 6.
- Task 8 depends on Tasks 3 (canonical folders), 5, 6, 7.
- Tasks 9 and 10 depend on Task 8 (docs reference the test fixture).

So 5/6 are the only pair that can run in parallel; the rest is sequential. Two execution options:

1. **Subagent-Driven** — Dispatch one subagent per task, sequentially, with a review checkpoint after each commit. Tasks 5 and 6 may be dispatched in parallel as the lone parallelism opportunity. Best when you want isolated context per task and review gates.
2. **Inline Execution** — Execute tasks in this session using executing-plans, with checkpoints after each commit. Faster when the parallelism gain is limited (only 5+6 here) and the surface is well-bounded.

Neither is the obvious default given the near-linear chain. Pick based on whether you want review checkpoints per task (subagent) or batched checkpoints (inline).
