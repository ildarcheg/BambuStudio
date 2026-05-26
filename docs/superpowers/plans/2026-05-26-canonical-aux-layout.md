# Canonical Aux Folder Layout — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align CLI aux folder names with Bambu Studio canonical (`Model Pictures`, `Profile Pictures`, `Bill of Materials`, `Assembly Guide`, `Others`); decouple `DesignerCover` from `ProfileCover` (own folders, own basenames, no shared file); accept JPEG in addition to PNG; add `--cover-name` to pick an existing image; gate the result with two new invariant guards and a round-trip test against `test_reference.3mf`.

**Architecture:** Touch points are concentrated in `src/cli/project_tab_ops.{hpp,cpp}` (enum + cover helpers), `src/cli/commands/project_tab.cpp` (CLI flag wiring), `src/cli/invariant_guard.{hpp,cpp}` + `src/cli/io.cpp` (post-save checks). Existing module boundaries are preserved. The CLI is pre-release: no alias compatibility for old flag values.

**Tech Stack:** C++17, libslic3r, Catch2 v2.x, boost::filesystem, miniz (mz_zip), CLI11, nlohmann::json. Spec: `docs/superpowers/specs/2026-05-26-canonical-aux-layout-design.md`.

---

## File Structure

**Source files modified:**
- `src/cli/project_tab_ops.hpp` — enum + struct fields
- `src/cli/project_tab_ops.cpp` — enum strings, cover helpers, info/profile_set rewiring
- `src/cli/commands/project_tab.cpp` — `--cover-name` option, mutual-exclusion check, `parse_folder` updates
- `src/cli/invariant_guard.hpp` — new check declarations + `GuardResult.failed_check` extension
- `src/cli/invariant_guard.cpp` — new check implementations + `run_guard` extension
- `src/cli/io.cpp` — pass `source_path` through to `run_guard` (already in `ProjectState`; nothing to wire)

**Tests modified:**
- `tests/cli/unit/test_project_aux_ops.cpp` (enum name updates)
- `tests/cli/unit/test_project_info_ops.cpp` (cover decoupling)
- `tests/cli/unit/test_project_profile_ops.cpp` (cover decoupling)
- `tests/cli/roundtrip/test_project_tab.cpp` (enum name updates)
- `tests/cli/CMakeLists.txt` (register new test files + new fixture macro)

**Tests created:**
- `tests/cli/unit/test_image_signature.cpp`
- `tests/cli/unit/test_cover_decoupling.cpp`
- `tests/cli/unit/test_cover_pick_by_name.cpp`
- `tests/cli/unit/test_invariant_aux_passthrough.cpp`
- `tests/cli/unit/test_invariant_cover_references.cpp`
- `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp`
- `tests/cli/fixtures/test_reference.3mf` (binary fixture)

**Docs modified:**
- `CLAUDE.md` (remove inaccurate aux-folder divergence note)
- `docs/cli/status.md` (add Phase G entry)

**Docs created:**
- `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md`

---

## Task 1: Image signature validator

**Files:**
- Modify: `src/cli/project_tab_ops.cpp` (add static helper)
- Create: `tests/cli/unit/test_image_signature.cpp`
- Modify: `tests/cli/CMakeLists.txt` (register new test)
- Modify: `src/cli/project_tab_ops.hpp` (expose helper for tests via internal-detail header — see Step 1 note)

The current `check_png_signature` is a file-internal static. We want the new `is_png_or_jpeg` callable from a test. The cleanest way is to expose it as a free function in the `bambu_cli::detail` namespace declared in `project_tab_ops.hpp`. (Alternative — keep static and test indirectly via `info_set --cover`. Rejected: slower iteration and signature edge cases are easier to test directly.)

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

TEST_CASE("is_png_or_jpeg: accepts JPEG SOI marker",
          "[unit][image_signature]") {
    // Standard JPEG/JFIF: FF D8 FF E0 ... (also accept FF D8 FF E1 for EXIF).
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

- [ ] **Step 2: Run test to verify it fails**

Build, then:
```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe "[image_signature]"
```
Expected: build fails — `bambu_cli::detail::is_png_or_jpeg` not declared.

- [ ] **Step 3: Add the `detail` namespace + declaration to `project_tab_ops.hpp`**

At the end of `src/cli/project_tab_ops.hpp` (before `} // namespace bambu_cli`):

```cpp
namespace detail {
    // Returns true if <path> begins with the PNG magic (89 50 4E 47 0D 0A 1A 0A)
    // or the JPEG SOI sequence (FF D8 FF). Returns false on read failure,
    // truncation (<3 bytes), or any other signature.
    bool is_png_or_jpeg(const std::string& path);
}
```

- [ ] **Step 4: Implement `is_png_or_jpeg` in `project_tab_ops.cpp`**

Add near the existing `check_png_signature` (which we will remove in a later step — for now both coexist):

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

- [ ] **Step 5: Register the test in CMake**

Edit `tests/cli/CMakeLists.txt`. Insert into `BAMBU_CLI_TEST_SOURCES` after `unit/test_png_placeholder.cpp` (line 37):

```cmake
    unit/test_image_signature.cpp
```

- [ ] **Step 6: Run test to verify it passes**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe "[image_signature]"
```
Expected: 8 assertions, all PASS.

- [ ] **Step 7: Commit**

```
git add src/cli/project_tab_ops.hpp src/cli/project_tab_ops.cpp \
        tests/cli/unit/test_image_signature.cpp tests/cli/CMakeLists.txt
git commit -m "feat(cli): add is_png_or_jpeg signature helper

Pre-work for accepting JPEG covers alongside PNG. The existing
check_png_signature stays for now; will be removed when info_set/
profile_set switch over in a later task."
```

---

## Task 2: Pin canonical aux folder names with a new unit test (red phase)

**Files:**
- Create: `tests/cli/unit/test_aux_folder_canonical_names.cpp`
- Modify: `tests/cli/CMakeLists.txt`

This task is intentionally red until Task 3 lands the enum rename + new variant. We pin the expected canonical strings first.

- [ ] **Step 1: Write the failing test**

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

- [ ] **Step 2: Register in CMake**

Edit `tests/cli/CMakeLists.txt`. Insert into `BAMBU_CLI_TEST_SOURCES` immediately after `unit/test_image_signature.cpp`:

```cmake
    unit/test_aux_folder_canonical_names.cpp
```

- [ ] **Step 3: Run test to verify it fails**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```
Expected: compile fails — `AuxFolder::ModelPictures`, `ProfilePictures`, `BillOfMaterials` do not exist.

- [ ] **Step 4: Do NOT commit yet**

This test is red on purpose; the rename happens in Task 3 and we want the two to land together so the tree never compiles broken on `master`. Leave the file in the working tree.

---

## Task 3: Rename AuxFolder enum + add ProfilePictures variant + update all callsites and existing tests

**Files:**
- Modify: `src/cli/project_tab_ops.hpp`
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `src/cli/commands/project_tab.cpp` (parse_folder + help strings)
- Modify: `tests/cli/unit/test_project_aux_ops.cpp` (enum references)
- Modify: `tests/cli/roundtrip/test_project_tab.cpp:63,114` (enum references)
- Modify: `tests/cli/unit/test_project_info_ops.cpp` (any AuxFolder use)
- Modify: `tests/cli/unit/test_project_profile_ops.cpp` (any AuxFolder use)

Mechanical rename, but in one task because the enum is the type — we cannot land a partial rename. Tasks 4+ build on this.

- [ ] **Step 1: Update the enum definition**

In `src/cli/project_tab_ops.hpp`, replace:

```cpp
enum class AuxFolder {
    Pictures,
    Bom,
    AssemblyGuide,
    Others,
};
```

with:

```cpp
enum class AuxFolder {
    ModelPictures,      // archive subdir "Model Pictures"
    ProfilePictures,    // archive subdir "Profile Pictures"
    BillOfMaterials,    // archive subdir "Bill of Materials"
    AssemblyGuide,      // archive subdir "Assembly Guide"
    Others,             // archive subdir "Others"
};
```

- [ ] **Step 2: Update the three lookup functions in `project_tab_ops.cpp`**

Replace the existing `folder_flag` / `folder_json_key` / `folder_subdir` block (lines 192-220):

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

- [ ] **Step 3: Update the `aux_list` iteration set in `project_tab_ops.cpp:231`**

Replace:
```cpp
    for (const auto folder : {AuxFolder::Pictures, AuxFolder::Bom,
                               AuxFolder::AssemblyGuide, AuxFolder::Others}) {
```
with:
```cpp
    for (const auto folder : {AuxFolder::ModelPictures, AuxFolder::ProfilePictures,
                               AuxFolder::BillOfMaterials, AuxFolder::AssemblyGuide,
                               AuxFolder::Others}) {
```

- [ ] **Step 4: Update the inline `"Model Pictures"` literal in `delete_cover_file_if_unreferenced`**

In `src/cli/project_tab_ops.cpp:79`, the string literal `"Model Pictures"` is already correct (was an accidental partial fix in earlier code). Leave it — this helper will be deleted entirely in Task 4. No edit needed here.

- [ ] **Step 5: Update `parse_folder` in `src/cli/commands/project_tab.cpp:247-255`**

Replace:
```cpp
static AuxFolder parse_folder(const std::string& s, OutputMode mode) {
    if (s == "pictures")       return AuxFolder::Pictures;
    if (s == "bom")            return AuxFolder::Bom;
    if (s == "assembly-guide") return AuxFolder::AssemblyGuide;
    if (s == "others")         return AuxFolder::Others;
    emit_error(mode, "usage_error", "unknown folder: " + s +
               " (expected: pictures|bom|assembly-guide|others)");
    std::exit(to_int(ExitCode::usage_error));
}
```
with:
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

In `tests/cli/unit/test_project_aux_ops.cpp`:
- Line 74-77: change the three assertions to match the new flag strings:
  ```cpp
  REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::ModelPictures)    == "model-pictures");
  REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::BillOfMaterials)  == "bill-of-materials");
  REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::AssemblyGuide)    == "assembly-guide");
  REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::Others)           == "others");
  ```
- Line 81-82: change the json_key assertions:
  ```cpp
  REQUIRE(bambu_cli::folder_json_key(bambu_cli::AuxFolder::AssemblyGuide) == "assembly_guide");
  REQUIRE(bambu_cli::folder_json_key(bambu_cli::AuxFolder::ModelPictures) == "model_pictures");
  ```
- Lines 94, 112, 118, 132, 137, 149, 160, 173, 177, 190 and any other `AuxFolder::Pictures` / `AuxFolder::Bom` reference → rewrite to `AuxFolder::ModelPictures` / `AuxFolder::BillOfMaterials` respectively. No semantic change.

- [ ] **Step 7: Update existing roundtrip test for enum renames**

In `tests/cli/roundtrip/test_project_tab.cpp`:
- Line 63: `bambu_cli::AuxFolder::Others` (no change).
- Line 114: `bambu_cli::AuxFolder::Pictures` → `bambu_cli::AuxFolder::ModelPictures`.
- Any other Pictures/Bom references → rename to ModelPictures/BillOfMaterials.

- [ ] **Step 8: Update info/profile ops tests if they reference AuxFolder**

Grep for `AuxFolder::Pictures` and `AuxFolder::Bom` in `tests/cli/unit/test_project_info_ops.cpp` and `tests/cli/unit/test_project_profile_ops.cpp`. Rename any hits the same way.

```
grep -n "AuxFolder::Pictures\|AuxFolder::Bom" tests/cli/unit/*.cpp tests/cli/roundtrip/*.cpp
```

- [ ] **Step 9: Build and run all tests**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe
```
Expected: build succeeds; `[aux_folder_names]` passes; existing aux ops + roundtrip tests pass. The shared-cover refcount tests (in `test_project_info_ops` / `test_project_profile_ops`) still pass at this point because we haven't touched the cover plumbing yet — the helper folder name `"Model Pictures"` on line 79 was already correct.

- [ ] **Step 10: Commit**

```
git add -u src/ tests/
git add tests/cli/unit/test_aux_folder_canonical_names.cpp
git commit -m "refactor(cli): canonical AuxFolder names (Model Pictures / Profile Pictures / Bill of Materials / Assembly Guide)

Rename AuxFolder enum + flag values + JSON keys + archive subdirs to
match Bambu Studio's canonical layout (src/slic3r/GUI/Auxiliary.hpp:75,
Project.cpp:214-226 and verified against test_reference.3mf). Add
ProfilePictures variant. No alias compat for old pictures/bom flag
values (CLI is pre-release). Cover plumbing is unchanged in this task —
that is the next step."
```

---

## Task 4: Decouple DesignerCover from ProfileCover

**Files:**
- Modify: `src/cli/project_tab_ops.hpp` (add `cover_name` to params; expose `embed_image_into_folder` in `detail::`)
- Modify: `src/cli/project_tab_ops.cpp` (replace embed_cover + refcount helpers; rewire info_set/profile_set)
- Create: `tests/cli/unit/test_cover_decoupling.cpp`
- Modify: `tests/cli/CMakeLists.txt`
- Modify: `tests/cli/unit/test_project_info_ops.cpp` (existing cover tests need new expectations)
- Modify: `tests/cli/unit/test_project_profile_ops.cpp` (existing cover tests need new expectations)

- [ ] **Step 1: Add new fields to InfoSetParams / ProfileSetParams + declare helpers**

In `src/cli/project_tab_ops.hpp`:

Add `cover_name` next to `cover_path` in `InfoSetParams`:
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

Same addition to `ProfileSetParams`:
```cpp
struct ProfileSetParams {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> cover_path;
    std::optional<std::string> cover_name;  // basename of an existing aux entry in Profile Pictures
};
```

In the `namespace detail { ... }` block at the bottom of the header (next to `is_png_or_jpeg`), add:
```cpp
namespace detail {
    bool is_png_or_jpeg(const std::string& path);

    // Embed <on_disk_path> as <basename(on_disk_path)> under the aux temp
    // <folder>. Validates PNG/JPEG signature. Returns the basename written.
    // Throws BadCoverImage on bad signature / unreadable source.
    // <folder> must be ModelPictures or ProfilePictures.
    std::string embed_image_into_folder(Slic3r::Model& model,
                                        AuxFolder folder,
                                        const std::string& on_disk_path);

    // Throws std::out_of_range if Auxiliaries/<folder>/<basename> is not
    // present in the aux temp dir.
    void require_image_in_folder(const Slic3r::Model& model,
                                 AuxFolder folder,
                                 const std::string& basename);
}
```

Add a forward declaration of `Slic3r::Model` near the top of the header (after the existing `#include`s, before `namespace bambu_cli {`). The current header doesn't include `<libslic3r/Model.hpp>` (only `project_state.hpp` and `exceptions.hpp`), so a forward decl is required for the new helpers' signatures:
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

    // 1. Set designer cover (PNG).
    bambu_cli::InfoSetParams ip;
    ip.cover_path = kPng;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    // 2. Set profile cover (JPEG — new in this change).
    bambu_cli::ProfileSetParams pp;
    pp.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));

    // 3. Each metadata pointer carries its own basename.
    REQUIRE(s.model.model_info);
    REQUIRE(s.model.profile_info);
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    // 4. Each on-disk file lives under its own folder.
    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures"   / fs::path(kPng).filename()));
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));

    // 5. The two are independent — no shared cover.png anywhere.
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

    // Profile's file is untouched on disk.
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

Insert into `tests/cli/CMakeLists.txt` after `unit/test_aux_folder_canonical_names.cpp`:
```cmake
    unit/test_cover_decoupling.cpp
```

- [ ] **Step 4: Run test to verify it fails**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe "[cover_decouple]"
```
Expected: the JPEG case fails (old code rejects non-PNG) and the decouple assertions fail (old code lands ProfileCover in Model Pictures with basename `cover.png`).

- [ ] **Step 5: Replace embed_cover + refcount helpers in `project_tab_ops.cpp`**

Delete the existing `embed_cover` (lines 50-64), `info_cover_empty` (71-73), `profile_cover_empty` (74-76), `delete_cover_file_if_unreferenced` (77-82), and `check_png_signature` (lines 26-32) — `check_png_signature` is no longer referenced anywhere after this change.

Add the new helpers in the same area:

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

Replace `info_set` (around line 114) with:
```cpp
std::string info_set(ProjectState& state, const InfoSetParams& p) {
    if (p.cover_path && p.cover_name)
        throw std::invalid_argument(
            "info_set: --cover and --cover-name are mutually exclusive");
    auto& mi = ensure_model_info(state.model);
    if (p.title)       mi.model_name  = *p.title;
    if (p.description) mi.description = *p.description;
    if (p.license)     mi.license     = *p.license;
    if (p.copyright)   mi.copyright   = *p.copyright;
    if (p.cover_path) {
        mi.cover_file = detail::embed_image_into_folder(
            state.model, AuxFolder::ModelPictures, *p.cover_path);
    } else if (p.cover_name) {
        const std::string nm = sanitize_aux_name(*p.cover_name);
        detail::require_image_in_folder(state.model, AuxFolder::ModelPictures, nm);
        mi.cover_file = nm;
    }
    return "applied info edits";
}
```

Replace `info_clear` (around line 125):
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

Replace `profile_set` (around line 162):
```cpp
std::string profile_set(ProjectState& state, const ProfileSetParams& p) {
    if (p.cover_path && p.cover_name)
        throw std::invalid_argument(
            "profile_set: --cover and --cover-name are mutually exclusive");
    auto& pi = ensure_profile_info(state.model);
    if (p.title)       pi.ProfileTile        = *p.title;
    if (p.description) pi.ProfileDescription = *p.description;
    if (p.cover_path) {
        pi.ProfileCover = detail::embed_image_into_folder(
            state.model, AuxFolder::ProfilePictures, *p.cover_path);
    } else if (p.cover_name) {
        const std::string nm = sanitize_aux_name(*p.cover_name);
        detail::require_image_in_folder(state.model, AuxFolder::ProfilePictures, nm);
        pi.ProfileCover = nm;
    }
    return "applied profile edits";
}
```

Replace `profile_clear` (around line 174):
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

Open `tests/cli/unit/test_project_info_ops.cpp` and find any test asserting the embed path was `cover.png` or the refcount-delete behavior on `info clear cover`. Update each:
- The expected `cover_file` value becomes the basename of whatever path the test sets (e.g. `kPng` → `"cover_smoke.png"`).
- The expected on-disk path becomes `<aux>/Model Pictures/<basename>` (the literal `"cover.png"` filename is no longer used; the original basename is preserved).
- Any test that asserted the on-disk file is deleted by `info clear cover` should now assert the on-disk file STILL EXISTS after clear (the metadata pointer is cleared, the file is left in place).

If a test name says "shared-cover refcount" or similar, the test's premise is gone — delete the test and replace with whatever positive coverage of the new behavior is missing. The new `test_cover_decoupling.cpp` already covers the independence-of-folders cases.

- [ ] **Step 9: Update existing cover tests in test_project_profile_ops.cpp**

Open `tests/cli/unit/test_project_profile_ops.cpp`. For every test that exercises `--cover` or `clear cover`:
- Replace expected `profile_info->ProfileCover == "cover.png"` with `profile_info->ProfileCover == fs::path(<the test's source path>).filename().string()` (e.g. `"cover_smoke.png"` if the test uses `kPng`).
- Replace any existence assertion on `<aux>/Model Pictures/cover.png` with `<aux>/Profile Pictures/<basename>` — the profile cover now lives in its own folder.
- Any test that asserted the shared `cover.png` is deleted by `profile clear cover` should be flipped: the file STILL EXISTS after `profile clear cover`. Only the metadata pointer is blanked.
- Delete any test whose premise was "designer and profile share a single cover.png" — that behavior is gone. The positive coverage of independence is in `test_cover_decoupling.cpp`.

- [ ] **Step 10: Build and run cover tests**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe "[cover_decouple]"
build/tests/RelWithDebInfo/cli_tests.exe "[unit][c3]"
build/tests/RelWithDebInfo/cli_tests.exe "[unit]"
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
machinery (info_cover_empty / profile_cover_empty /
delete_cover_file_if_unreferenced). info/profile_clear cover now blanks
the metadata pointer only — the on-disk image stays as a normal aux
entry. JPEG accepted alongside PNG via is_png_or_jpeg."
```

---

## Task 5: CLI flag wiring for `--cover-name` + mutual exclusion

**Files:**
- Modify: `src/cli/commands/project_tab.cpp`
- Create: `tests/cli/unit/test_cover_pick_by_name.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

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

    // Stage a file via aux_add first.
    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ModelPictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "cover_smoke.png";
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));
    REQUIRE(s.model.model_info->cover_file == "cover_smoke.png");
}

TEST_CASE("--cover-name: throws when name not present in folder",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "absent.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), std::out_of_range);
}

TEST_CASE("--cover-name: path separator rejected by sanitize_aux_name",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "subdir/cover.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::AuxNameError);
}

TEST_CASE("--cover-name + --cover together: invalid_argument",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kPng;
    ip.cover_name = "anything.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), std::invalid_argument);
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

- [ ] **Step 2: Register in CMake**

Insert after `unit/test_cover_decoupling.cpp`:
```cmake
    unit/test_cover_pick_by_name.cpp
```

- [ ] **Step 3: Run test — expect PASS at this point**

The ops layer (info_set/profile_set) already handles `cover_name` after Task 4. So the test should compile and pass already. Run:
```
build/tests/RelWithDebInfo/cli_tests.exe "[cover_name]"
```
Expected: 5 sections, all PASS.

If it does NOT pass, the failure means Task 4 missed wiring. Go fix Task 4 before continuing.

- [ ] **Step 4: Wire `--cover-name` into the CLI `project info set` command**

In `src/cli/commands/project_tab.cpp`, find the `InfoSetArgs` struct (around line 92-95):
```cpp
struct InfoSetArgs {
    std::string file, output;
    std::string title, description, license, copyright, cover;
    bool has_title{}, has_desc{}, has_license{}, has_copyright{}, has_cover{};
};
```
Add two fields:
```cpp
struct InfoSetArgs {
    std::string file, output;
    std::string title, description, license, copyright, cover, cover_name;
    bool has_title{}, has_desc{}, has_license{}, has_copyright{}, has_cover{}, has_cover_name{};
};
```

In the `register_info` setter block (around line 116-127), update the help string on `--cover` and add `--cover-name`. Replace:
```cpp
    set->add_option("--cover",       set_a->cover,       "cover image (PNG only)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
```
with:
```cpp
    set->add_option("--cover",       set_a->cover,       "cover image to embed (PNG or JPEG)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
    set->add_option("--cover-name",  set_a->cover_name,
                    "select existing image in Model Pictures as cover (mutually exclusive with --cover)")
       ->each([set_a](const std::string&){ set_a->has_cover_name = true; });
```

In the callback (around line 132-141), update the "all args empty" check and the param assembly. Find:
```cpp
        if (!set_a->has_title && !set_a->has_desc && !set_a->has_license &&
            !set_a->has_copyright && !set_a->has_cover) {
            // ...usage error path...
        }
        InfoSetParams p;
        // ...assign existing fields...
        if (set_a->has_cover)     p.cover_path  = set_a->cover;
```
and update to:
```cpp
        if (!set_a->has_title && !set_a->has_desc && !set_a->has_license &&
            !set_a->has_copyright && !set_a->has_cover && !set_a->has_cover_name) {
            // ...same usage error path...
        }
        InfoSetParams p;
        // ...assign existing fields...
        if (set_a->has_cover)      p.cover_path = set_a->cover;
        if (set_a->has_cover_name) p.cover_name = set_a->cover_name;
```

- [ ] **Step 5: Wire `--cover-name` into the CLI `project profile set` command**

Same pattern. Edit `ProfileSetArgs` struct (around line 171-173) to add `cover_name` and `has_cover_name`:
```cpp
struct ProfileSetArgs {
    std::string file, output;
    std::string title, description, cover, cover_name;
    bool has_title{}, has_desc{}, has_cover{}, has_cover_name{};
};
```

In the `register_profile` setter block (around line 194-201), update `--cover` help text and add `--cover-name`:
```cpp
    set->add_option("--cover",       set_a->cover,       "cover image to embed (PNG or JPEG)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
    set->add_option("--cover-name",  set_a->cover_name,
                    "select existing image in Profile Pictures as cover (mutually exclusive with --cover)")
       ->each([set_a](const std::string&){ set_a->has_cover_name = true; });
```

Callback adjustments (around line 205-212):
```cpp
        if (!set_a->has_title && !set_a->has_desc && !set_a->has_cover && !set_a->has_cover_name) {
            // ...same usage error path...
        }
        ProfileSetParams p;
        // ...
        if (set_a->has_cover)      p.cover_path = set_a->cover;
        if (set_a->has_cover_name) p.cover_name = set_a->cover_name;
```

- [ ] **Step 6: Build and exercise the CLI binary**

```
cmake --build build --target bambu-cli --config RelWithDebInfo
build/RelWithDebInfo/bambu-cli.exe project info set --help
build/RelWithDebInfo/bambu-cli.exe project profile set --help
```
Expected: both `--cover` and `--cover-name` listed in help with their new descriptions.

- [ ] **Step 7: Commit**

```
git add -u src/ tests/
git add tests/cli/unit/test_cover_pick_by_name.cpp
git commit -m "feat(cli): --cover-name to select existing image as cover

project info set / project profile set now accept --cover-name NAME
alongside --cover PATH. --cover-name picks a file already present in
Model Pictures (info) or Profile Pictures (profile) as the cover without
re-embedding. The two flags are mutually exclusive."
```

---

## Task 6: New invariant guard — `check_auxiliary_passthrough`

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

// Build a tiny zip with the listed entries (archive-path, content-bytes).
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
    const auto post = make_zip({});  // empty
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
    const auto post = make_zip({{"3D/3dmodel.model", "Y"},  // changed — ignored
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

- [ ] **Step 2: Register in CMake**

Insert after `unit/test_cover_pick_by_name.cpp`:
```cmake
    unit/test_invariant_aux_passthrough.cpp
```

- [ ] **Step 3: Run test to verify it fails**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```
Expected: build fails — `bambu_cli::check_auxiliary_passthrough` is not declared.

- [ ] **Step 4: Declare the new check in `invariant_guard.hpp`**

Add at the bottom of the file, before `} // namespace bambu_cli`:

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

- [ ] **Step 5: Implement `check_auxiliary_passthrough` in `invariant_guard.cpp`**

At the top of `invariant_guard.cpp`, ensure these are present in includes:
```cpp
#include <miniz.h>
#include <cstring>
#include <vector>
#include <unordered_map>
```

Add this function:

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
        if (name.rfind("Auxiliaries/", 0) != 0) continue;  // not under Auxiliaries/
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

- [ ] **Step 6: Run test to verify it passes**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe "[invariant_aux]"
```
Expected: 5 sections, all PASS.

- [ ] **Step 7: Commit**

```
git add -u src/cli/invariant_guard.hpp src/cli/invariant_guard.cpp tests/cli/CMakeLists.txt
git add tests/cli/unit/test_invariant_aux_passthrough.cpp
git commit -m "feat(cli): check_auxiliary_passthrough invariant guard

Verifies every Auxiliaries/* entry in the source archive survives
load->store with byte-identical contents. Catches accidental folder
renames, missing files, or content drift in the save path. Not yet
wired into run_guard — that's the next task once both new checks land."
```

---

## Task 7: New invariant guard — `check_cover_references_resolve`

**Files:**
- Modify: `src/cli/invariant_guard.hpp`
- Modify: `src/cli/invariant_guard.cpp`
- Create: `tests/cli/unit/test_invariant_cover_references.cpp`
- Modify: `tests/cli/CMakeLists.txt`

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

- [ ] **Step 2: Register in CMake**

Insert after `unit/test_invariant_aux_passthrough.cpp`:
```cmake
    unit/test_invariant_cover_references.cpp
```

- [ ] **Step 3: Run test to verify it fails (compile error)**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```
Expected: build fails — `check_cover_references_resolve` not declared.

- [ ] **Step 4: Declare in `invariant_guard.hpp`**

Add to the header:

```cpp
// Verify DesignerCover and ProfileCover metadata in <archive_path>'s
// 3D/3dmodel.model reference filenames that exist in Auxiliaries/Model
// Pictures/ and Auxiliaries/Profile Pictures/ respectively. Empty metadata
// values are valid and pass. First mismatch is written to *err_out and
// the function returns false.
bool check_cover_references_resolve(const std::string& archive_path,
                                    std::string* err_out);
```

- [ ] **Step 5: Implement in `invariant_guard.cpp`**

Add:

```cpp
namespace {

// Extract <metadata name="KEY">VALUE</metadata> values from a 3MF model XML
// buffer. Returns an empty string if the key is not found. Tolerant of
// whitespace/quoting variation but does NOT do full XML parsing — it's
// matching the exact Bambu output shape (one metadata per line).
static std::string extract_metadata(const std::string& xml, const std::string& key) {
    const std::string needle = "name=\"" + key + "\">";
    const auto p = xml.find(needle);
    if (p == std::string::npos) return "";
    const auto start = p + needle.size();
    const auto end   = xml.find("</metadata>", start);
    if (end == std::string::npos) return "";
    return xml.substr(start, end - start);
}

} // namespace

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

- [ ] **Step 6: Run test**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe "[invariant_covref]"
```
Expected: 4 sections, all PASS.

- [ ] **Step 7: Commit**

```
git add -u src/cli/invariant_guard.hpp src/cli/invariant_guard.cpp tests/cli/CMakeLists.txt
git add tests/cli/unit/test_invariant_cover_references.cpp
git commit -m "feat(cli): check_cover_references_resolve invariant guard

Verifies DesignerCover/ProfileCover metadata in the saved 3D/3dmodel.model
references filenames that actually exist in Model Pictures / Profile
Pictures. Empty values are valid. Not yet wired into run_guard."
```

---

## Task 8: Wire both new guards into `run_guard`

**Files:**
- Modify: `src/cli/invariant_guard.hpp` (extend `failed_check` documentation)
- Modify: `src/cli/invariant_guard.cpp` (extend `run_guard` to call new checks)
- Modify: `src/cli/io.cpp` (no change expected — `run_guard` is already called; we extend its body)

- [ ] **Step 1: Inspect `run_guard` to find the integration point**

Read `src/cli/invariant_guard.cpp` and locate `run_guard(...)`. It currently runs three checks: rels, thumbnails, config_roundtrip. We will append two more: `auxiliary_passthrough` and `cover_references_resolve`.

`auxiliary_passthrough` requires the PRE archive path (the source archive that was loaded). That comes from `state.source_path`. `cover_references_resolve` only needs the POST path (the freshly-saved tmp).

- [ ] **Step 2: Update the `failed_check` value documentation in the header**

In `src/cli/invariant_guard.hpp`, find the `GuardResult` comment block (the one describing the three current checks `(a)`, `(b)`, `(c)`). Append:

```cpp
//   (d) auxiliary passthrough — every Auxiliaries/* file in
//       state.source_path is present at the same archive path in the
//       saved archive with byte-identical content. (Skipped if
//       state.source_path is empty, e.g. for project_init from
//       template; that case is covered by check_thumbnails_in_archive.)
//   (e) cover references resolve — DesignerCover / ProfileCover
//       metadata reference existing files in the canonical folders.
```

Also update the `failed_check` field comment to list the new strings:
```cpp
    std::string failed_check;     // "rels", "thumbnails", "config_roundtrip",
                                  // "auxiliary_passthrough", or "cover_references_resolve"
```

- [ ] **Step 3: Extend `run_guard` to call the new checks**

In `src/cli/invariant_guard.cpp`, after the three existing checks succeed and before the final `r.ok = true; return r;`, insert:

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

(Make sure `<boost/filesystem.hpp>` is included at the top — it likely already is; if not, add it.)

- [ ] **Step 4: Build and run the entire test suite**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build/tests/RelWithDebInfo/cli_tests.exe
```
Expected: all green (the new checks are run as part of every `save_project` invocation in tests; the existing roundtrip suite is the regression net).

If a test fails because some prior CLI-produced 3MF fixture had aux files in old-style folders (`Pictures/`, `Bom/`, `AssemblyGuide/`), the `auxiliary_passthrough` check will flag it. In that case the fixture itself is stale — note which fixture and either regenerate it or, if it's the committed `tests/cli/fixtures/reference.3mf`, decide whether to (a) re-export it from Bambu Studio with canonical names or (b) carve out an exemption in the guard for files under the legacy folders. Recommendation: regenerate. Discuss with the user before exempting.

- [ ] **Step 5: Commit**

```
git add -u src/cli/invariant_guard.hpp src/cli/invariant_guard.cpp
git commit -m "feat(cli): wire aux_passthrough + cover_refs into run_guard

Every save_project now also enforces (d) auxiliary passthrough and
(e) cover references resolve. (d) is skipped when state.source_path is
empty (project init from template) since there is no pre-archive to
diff against."
```

---

## Task 9: Round-trip test against `test_reference.3mf`

**Files:**
- Create: `tests/cli/fixtures/test_reference.3mf` (binary; copy of the user-supplied file)
- Modify: `tests/cli/CMakeLists.txt` (new fixture macro + new test source)
- Create: `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp`

- [ ] **Step 1: Copy the fixture into the repo**

```
cp "C:/Users/ildarcheg/Documents/GitHub/test_reference.3mf" tests/cli/fixtures/test_reference.3mf
```

Verify size and a quick sanity check on contents:
```
ls -l tests/cli/fixtures/test_reference.3mf
```
Expected: ~4.5 MB. Contains `Auxiliaries/Model Pictures/50calpellet.jpg`, `Auxiliaries/Profile Pictures/5.45x39mm.jpg`, `Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf`.

- [ ] **Step 2: Expose the fixture path to tests via CMake**

In `tests/cli/CMakeLists.txt`, in the `target_compile_definitions` block (after `BAMBU_CLI_FIXTURE_3MF`), add:

```cmake
    BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/test_reference.3mf"
```

- [ ] **Step 3: Register the new round-trip test source**

In `tests/cli/CMakeLists.txt`, append to `BAMBU_CLI_TEST_SOURCES` after `roundtrip/test_project_tab.cpp`:

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

    // Load.
    bambu_cli::ProjectState state;
    auto lr = bambu_cli::load_project(src, state);
    REQUIRE(lr.ok);

    // Save to a fresh temp location (NOT in-place — leaves the fixture intact).
    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("refmf-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(state, out.string());
    REQUIRE(sr.ok);

    // 1-4. Aux files preserved with canonical paths + identical content.
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

    // 5. Cover metadata: basename-only references.
    const auto model_xml = read_entry(out.string(), "3D/3dmodel.model");
    REQUIRE(model_xml.find("name=\"DesignerCover\">50calpellet.jpg<") != std::string::npos);
    REQUIRE(model_xml.find("name=\"ProfileCover\">5.45x39mm.jpg<")   != std::string::npos);

    // 6. Title / Description / ProfileTitle / ProfileDescription preserved.
    REQUIRE(model_xml.find("name=\"Title\">Test Project for reference<")            != std::string::npos);
    REQUIRE(model_xml.find("name=\"ProfileTitle\">Test profile name for reference<") != std::string::npos);

    // Bonus: the two new invariant guards pass on the saved archive.
    std::string ax_err, cov_err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(src, out.string(), &ax_err));
    REQUIRE(ax_err.empty());
    REQUIRE(bambu_cli::check_cover_references_resolve(out.string(), &cov_err));
    REQUIRE(cov_err.empty());

    fs::remove(out);
}
```

- [ ] **Step 5: Configure CMake (fixture macro changes require a re-config)**

```
cmake -B build -S .
cmake --build build --target cli_tests --config RelWithDebInfo
```
Expected: build succeeds.

- [ ] **Step 6: Run the round-trip test**

```
build/tests/RelWithDebInfo/cli_tests.exe "[reference_passthrough]"
```
Expected: PASS. If it fails:
- "missing in post: Auxiliaries/Profile Pictures/..." → Task 4 cover plumbing isn't writing the right folder. Investigate `profile_set`.
- DesignerCover metadata value drift → `store_bbs_3mf` is writing the full path instead of basename; revisit how `cover_file` is set in `info_set`.
- Title/Description drift → unrelated; check existing config-roundtrip guard.

- [ ] **Step 7: Run the full suite to confirm nothing regressed**

```
build/tests/RelWithDebInfo/cli_tests.exe
```
Expected: all green. Note the case + assertion count; the new tests added since master should bring it above the prior baseline of 235 cases / 1170 assertions.

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
top-level Title/Description/ProfileTitle metadata. Also runs the two
new invariant guards directly against the saved archive."
```

---

## Task 10: Refresh CLAUDE.md (remove stale aux-folder divergence note)

**Files:**
- Modify: `CLAUDE.md`

The current note at top-level CLAUDE.md states Bambu uses `Pictures / Bom / AssemblyGuide / Others` and warns the next agent NOT to "normalize." That note is wrong (it was documenting a CLI bug as if it were intentional). We replace it with a correct note.

- [ ] **Step 1: Update the aux-folder bullet in `CLAUDE.md`**

In the `## Sibling-fork divergences — LEGITIMATE, do not try to "fix"` section, find the `**Aux folder names:**` bullet:

```
- **Aux folder names:** Bambu uses `Pictures` / `Bom` / `AssemblyGuide` /
  `Others` (TitleCase) reflecting the BBS `Auxiliaries/` dir naming; Orca
  uses lowercase hyphenated. Do not "normalize."
```

Replace with:

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
actual canonical layout (Model Pictures / Profile Pictures / Bill of
Materials / Assembly Guide / Others) and a pointer to the new note."
```

---

## Task 11: New notes file + status.md entry

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
  the cover without re-embedding. Sanitized via `sanitize_aux_name`.

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

Open `docs/cli/status.md` and append a new section at the end (matching the existing milestone-block format):

```markdown
## Phase G — Canonical aux folder layout (2026-05-26)

- [x] AuxFolder enum renamed to canonical names (`ModelPictures`,
      `ProfilePictures`, `BillOfMaterials`, `AssemblyGuide`, `Others`).
- [x] DesignerCover / ProfileCover decoupled into own folders + own basenames.
- [x] `--cover-name` selects existing image in folder.
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

1. **Spec coverage** — each numbered item in the spec maps to:
   - §3.1 enum → Task 3
   - §3.2 flag/JSON/subdir mapping → Tasks 2 (pin), 3 (implement)
   - §3.3 `--cover-name` → Tasks 4 (ops), 5 (CLI)
   - §3.4 clear-cover behavior change → Task 4
   - §4 internals → Task 4
   - §5.1 invariants → Task 9 (round-trip asserts)
   - §5.2 guards → Tasks 6, 7, 8
   - §6 tests → Tasks 1, 2, 4, 5, 6, 7, 9
   - §7 files touched → all tasks
   - Docs (§7 last block) → Tasks 10, 11

2. **Placeholder scan** — none. Every step has either real code or an exact command + expected output.

3. **Type consistency** — `AuxFolder` enum names match across header / cpp / tests. `InfoSetParams::cover_name` / `ProfileSetParams::cover_name` defined in Task 4 Step 1 and used in Tasks 4, 5. `detail::is_png_or_jpeg` declared Task 1 Step 3 and used in Task 4 Step 5. `detail::embed_image_into_folder` / `require_image_in_folder` declared Task 4 Step 1, defined Task 4 Step 5, used Task 4 Step 6 + 7. `check_auxiliary_passthrough` declared Task 6 Step 4, defined Task 6 Step 5, used Tasks 8, 9. `check_cover_references_resolve` declared Task 7 Step 4, defined Task 7 Step 5, used Tasks 8, 9.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-26-canonical-aux-layout.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — I execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
