# Canonical Aux Folder Layout + Profile Pictures + Assembly-Guide PDFs

**Date:** 2026-05-26
**Branch target:** `master` (working branch TBD — likely `canonical-aux-layout`)
**Reference fixture:** `C:\Users\ildarcheg\Documents\GitHub\test_reference.3mf`

## 1. Problem

The CLI's aux-folder layout diverges from canonical Bambu Studio output in
three concrete ways, confirmed by inspecting `test_reference.3mf`:

| Surface | CLI today | Bambu canonical (`src/slic3r/GUI/Auxiliary.hpp:75`, `Project.cpp:214-226`) |
|---|---|---|
| Generic aux folder name — model previews (`aux add --folder pictures`) | `Pictures` (`project_tab_ops.cpp::folder_subdir`) | `Model Pictures` |
| Generic aux folder name — profile previews | *(none — no enum variant)* | `Profile Pictures` |
| Generic aux folder name — BOM docs (`aux add --folder bom`) | `Bom` | `Bill of Materials` |
| Generic aux folder name — assembly guide (`aux add --folder assembly-guide`) | `AssemblyGuide` | `Assembly Guide` |
| Embed target for `info set --cover` (DesignerCover) | `Model Pictures/cover.png` — already canonical folder, but forced filename and shared on-disk file (see next row) | `Model Pictures/<basename>` |
| Embed target for `profile set --cover` (ProfileCover) | `Model Pictures/cover.png` — wrong folder AND shared with DesignerCover via a refcount in `project_tab_ops.cpp::delete_cover_file_if_unreferenced` | `Profile Pictures/<basename>` |
| Cover image format | PNG only (`check_png_signature`) | PNG or JPEG (reference file uses `.jpg`) |

Two distinct bugs, not one:
1. The generic `aux add --folder ...` code path uses the wrong subdir names (`Pictures`/`Bom`/`AssemblyGuide`) — purely a string-table issue in `folder_subdir`.
2. The cover-specific embed path in `embed_cover` lands the DesignerCover file in the correct folder (`Model Pictures/`) but forces filename `cover.png` and shares that single on-disk file with ProfileCover via a refcount. The ProfileCover should be in `Profile Pictures/<basename>` and entirely independent.

The reference file's `3D/3dmodel.model` carries `DesignerCover="50calpellet.jpg"`
(filename only, from `Auxiliaries/Model Pictures/`) and
`ProfileCover="5.45x39mm.jpg"` (filename only, from
`Auxiliaries/Profile Pictures/`). Both are JPEGs.

The earlier `CLAUDE.md` divergence note ("Bambu uses `Pictures` / `Bom` /
`AssemblyGuide` / `Others`") was wrong; this spec corrects it.

## 2. Goals

1. Emit aux folders under their canonical Bambu names so CLI-produced 3MFs are
   structurally indistinguishable from Bambu Studio's output (the reference
   round-trips byte-for-byte under the new invariant guards).
2. Decouple DesignerCover and ProfileCover: each lives in its own folder under
   its own basename. Eliminate the shared-`cover.png` refcount.
3. Accept JPEG in addition to PNG for cover images.
4. Let the user choose a cover by picking an existing image in the folder, not
   only by re-embedding from disk.
5. PDF support is automatic once `Assembly Guide` is the correct folder name —
   no separate "add-pdf" command is needed. Users invoke
   `bambu-cli project aux add --folder assembly-guide --file manual.pdf`.

### Non-goals

- Migrating 3MFs produced by the current CLI (which have `Pictures/`, `Bom/`,
  `AssemblyGuide/`). The CLI is pre-release; no migration helper is shipped.
- Renaming any of the existing thumbnail handling (`Auxiliaries/.thumbnails/`).
  Already canonical.
- Mirroring `model.profile_info` into `metadata_items["ProfileTile"]`. The
  Bambu storage format reads `model.profile_info` directly — see
  `docs/cli/notes/2026-05-21-bbs-profile-storage.md`.
- Any GUI changes.

## 3. Public surface

### 3.1 `AuxFolder` enum (`src/cli/project_tab_ops.hpp`)

```cpp
enum class AuxFolder {
    ModelPictures,      // was Pictures
    ProfilePictures,    // NEW
    BillOfMaterials,    // was Bom
    AssemblyGuide,      // (renamed string only)
    Others,
};
```

### 3.2 Flag + JSON + subdir mapping

| Enum | `--folder` flag | JSON key | Archive subdir |
|---|---|---|---|
| `ModelPictures` | `model-pictures` | `model_pictures` | `Model Pictures` |
| `ProfilePictures` | `profile-pictures` | `profile_pictures` | `Profile Pictures` |
| `BillOfMaterials` | `bill-of-materials` | `bill_of_materials` | `Bill of Materials` |
| `AssemblyGuide` | `assembly-guide` | `assembly_guide` | `Assembly Guide` |
| `Others` | `others` | `others` | `Others` |

Old `pictures` / `bom` flag values are no longer accepted (exit 1 — usage
error). The CLI is pre-release; no alias compatibility layer.

### 3.3 Cover commands

`project info set` gains a new flag alongside the existing `--cover PATH`:

- `--cover PATH` — embed image from disk. Validated as PNG/JPEG by signature.
  Copied to `Model Pictures/<basename>`. Sets
  `model_info.cover_file = "<basename>"`.
- `--cover-name NAME` — select an image already present in `Model Pictures/`
  as the cover. Sets `model_info.cover_file = "<NAME>"`. If
  `Model Pictures/<NAME>` does not exist in the aux temp dir → exit 6
  (`aux entry not found: <NAME>`).
- Passing both `--cover` and `--cover-name` → exit 1 (usage error).

**Layering of the new validations.** The ops layer (`info_set` /
`profile_set` in `project_tab_ops.cpp`) deliberately does NOT enforce
mutual-exclusion or run `sanitize_aux_name` on `cover_name` itself —
those are pre-conditions enforced one layer up, in the CLI command
callbacks (`commands/project_tab.cpp::register_info` /
`register_profile`). Rationale: exit codes belong to the CLI surface;
the ops layer signals failure via C++ exceptions and doesn't know about
`ExitCode::usage_error`. The CLI layer:

- Rejects `--cover` + `--cover-name` together via
  `emit_error(mode, "usage_error", "--cover and --cover-name are mutually exclusive")`
  + `std::exit(to_int(ExitCode::usage_error))` (exit 1).
- Runs `sanitize_aux_name` on the user-supplied `--cover-name` value
  before assigning it to `InfoSetParams::cover_name`; on
  `AuxNameError`, emits a usage error and exits 1.

The ops-layer contract for `info_set` / `profile_set` is then
"at most one of `cover_path`/`cover_name` is set, and if `cover_name`
is set the value has already been sanitized." A `require_image_in_folder`
miss still throws `std::out_of_range`, which `run_mutation` maps to
exit 6 (`ExitCode::unknown_reference`) — that mapping pre-exists in
`src/cli/commands/mutation_runner.hpp`.

`project profile set` gains the same pair, targeting `Profile Pictures/` and
`profile_info.ProfileCover`.

### 3.4 `clear cover` behavior change

Today `info clear cover` deletes the shared `cover.png` on disk via the
refcount helper. Going forward, `info clear cover` and `profile clear cover`
only blank the metadata pointer (`model_info.cover_file = ""` /
`profile_info.ProfileCover = ""`). The image file remains in
`Model Pictures/` / `Profile Pictures/` as a normal aux entry; the user can
remove it explicitly via `aux remove --folder ... --name ...`.

## 4. Internals

### 4.1 New helpers in `project_tab_ops.cpp`

```cpp
static bool is_png_or_jpeg(const std::string& path);

// Throws BadCoverImage if signature check fails or source unreadable.
// Returns the basename written. Overwrites any pre-existing same-named file.
static std::string embed_image_into_folder(
    Slic3r::Model& model,
    AuxFolder folder,            // ModelPictures or ProfilePictures
    const std::string& on_disk_path);

// Throws std::out_of_range (exit 6) if the named file is absent.
static void require_image_in_folder(
    const Slic3r::Model& model,
    AuxFolder folder,
    const std::string& basename);
```

Removed: `embed_cover`, `info_cover_empty`, `profile_cover_empty`,
`delete_cover_file_if_unreferenced`. The shared-cover refcount mechanism is
gone — designer and profile covers are independent files in independent
folders.

### 4.2 `info_set` / `profile_set` rewiring

The ops layer assumes the CLI has already validated mutual-exclusion and
sanitized `cover_name`. If both fields are set, `cover_path` wins (an
intentional no-op of the assumed-invalid combination — the CLI prevents
it from reaching here in practice).

```cpp
// info_set:
if (p.cover_path) {
    mi.cover_file = embed_image_into_folder(state.model,
                                            AuxFolder::ModelPictures,
                                            *p.cover_path);
} else if (p.cover_name) {
    require_image_in_folder(state.model,
                            AuxFolder::ModelPictures,
                            *p.cover_name);
    mi.cover_file = *p.cover_name;
}
```

`profile_set` is the structural mirror, targeting `AuxFolder::ProfilePictures`
and `pi.ProfileCover`. The mutual-exclusion + `sanitize_aux_name` checks live
in `commands/project_tab.cpp::register_info` / `register_profile` (see §3.3).

### 4.3 Image signature check

```cpp
static bool is_png_or_jpeg(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t hdr[8] = {};
    f.read(reinterpret_cast<char*>(hdr), 8);
    auto n = f.gcount();
    if (n >= 8 && std::memcmp(hdr, "\x89PNG\r\n\x1A\n", 8) == 0) return true;
    if (n >= 3 && hdr[0] == 0xFF && hdr[1] == 0xD8 && hdr[2] == 0xFF) return true;
    return false;
}
```

Applied to `--cover PATH` only. `aux add` continues to accept any file type
(PDFs, txt, csv, etc. — already the current behavior; the user is
responsible for putting sensible files in `Bill of Materials/` and
`Assembly Guide/`).

## 5. Round-trip invariants

### 5.1 Reference file invariants (must hold post-save)

When `test_reference.3mf` is loaded and re-saved, the saved archive must
satisfy:

1. `Auxiliaries/Model Pictures/50calpellet.jpg` present, byte-identical.
2. `Auxiliaries/Profile Pictures/5.45x39mm.jpg` present, byte-identical.
3. `Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf` present, byte-identical.
4. `Auxiliaries/.thumbnails/{thumbnail_3mf,thumbnail_middle,thumbnail_small}.png` present.
5. `3D/3dmodel.model` carries `DesignerCover="50calpellet.jpg"`,
   `ProfileCover="5.45x39mm.jpg"` (basenames only; no `/Auxiliaries/...` prefix).
6. `ProfileTitle="Test profile name for reference"`,
   `ProfileDescription="..."`, top-level `Description="..."`,
   `Title="Test Project for reference"` preserved.
7. `Metadata/project_settings.config`, `model_settings.config`,
   `slice_info.config` preserved (covered by existing
   `check_vector_config_roundtrip`).

### 5.2 New invariant guards

Two new functions in `src/cli/invariant_guard.{hpp,cpp}`, called from
`src/cli/io.cpp`'s save path immediately after the existing three checks:

```cpp
// Every regular file present under "Auxiliaries/" in `pre_path` must be
// present at the same archive path in `post_path` with byte-identical
// contents. First mismatch returned via err_out.
bool check_auxiliary_passthrough(const std::string& pre_path,
                                 const std::string& post_path,
                                 std::string* err_out);

// Verify that DesignerCover and ProfileCover metadata in the post-save model
// file reference filenames that actually exist in "Auxiliaries/Model Pictures"
// and "Auxiliaries/Profile Pictures" respectively. Empty metadata is valid
// and passes.
bool check_cover_references_resolve(const std::string& post_path,
                                    std::string* err_out);
```

## 6. Tests

### 6.1 Existing tests — updates

- `tests/cli/unit/test_project_aux_ops.cpp` — replace `AuxFolder::Pictures` /
  `AuxFolder::Bom` references; update `folder_flag` / `folder_json_key` /
  `folder_subdir` assertions to canonical strings; add the equivalent row
  for `ProfilePictures`.
- `tests/cli/roundtrip/test_project_tab.cpp:63,114` — enum renames; add a
  case asserting `Profile Pictures/<basename>` write path.
- Any test relying on the shared-`cover.png` refcount semantics — update to
  the new "two independent files, two independent folders" model.

### 6.2 New tests

1. `tests/cli/unit/test_image_signature.cpp` — `is_png_or_jpeg` accepts
   PNG (RFC 2083 signature), JPEG (SOI `FF D8 FF`), rejects GIF, rejects
   truncated, rejects empty, rejects non-image text.
2. `tests/cli/unit/test_aux_folder_canonical_names.cpp` — pin every
   canonical subdir string. A future accidental rename fails loudly with a
   diff-friendly assertion.
3. `tests/cli/unit/test_cover_decoupling.cpp` — `info set --cover a.png`
   then `profile set --cover b.jpg`: assert both files exist in their own
   folders, both metadata pointers carry their own basenames, `info clear
   cover` leaves `profile_info.ProfileCover` and on-disk `b.jpg` intact.
4. `tests/cli/unit/test_cover_pick_by_name.cpp` — `aux add --folder
   model-pictures --file img.jpg` then `info set --cover-name img.jpg`
   succeeds; `--cover-name absent.jpg` throws → exit 6; `--cover-name`
   with a path separator → exit 1; `--cover` + `--cover-name` together
   → exit 1.
5. `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp` — loads the
   reference fixture, saves it to a temp path, reopens the saved archive
   and asserts §5.1 invariants 1–6 + runs the two new guards explicitly.

### 6.3 Fixture

`tests/cli/fixtures/local/test_reference.3mf` — verbatim copy of the
user-supplied file. ~4.5 MB (the embedded `D_02_40sw_PRINT_GUIDE.pdf`
dominates at ~4 MB). Kept full-fidelity because a real Bambu-produced
file is what we are validating structural equivalence against; a
synthetic stand-in could pass while still missing real Bambu-isms.

## 7. Files touched

**Code**
- `src/cli/project_tab_ops.hpp`
- `src/cli/project_tab_ops.cpp`
- `src/cli/commands/info.cpp`
- `src/cli/commands/profile.cpp`
- `src/cli/invariant_guard.hpp`
- `src/cli/invariant_guard.cpp`
- `src/cli/io.cpp`

**Tests**
- `tests/cli/unit/test_project_aux_ops.cpp` (update)
- `tests/cli/roundtrip/test_project_tab.cpp` (update)
- `tests/cli/unit/test_image_signature.cpp` (new)
- `tests/cli/unit/test_aux_folder_canonical_names.cpp` (new)
- `tests/cli/unit/test_cover_decoupling.cpp` (new)
- `tests/cli/unit/test_cover_pick_by_name.cpp` (new)
- `tests/cli/roundtrip/test_reference_3mf_passthrough.cpp` (new)
- `tests/cli/fixtures/local/test_reference.3mf` (new — fixture)

**Docs**
- `CLAUDE.md` — fix the aux-folder names list under "Sibling-fork
  divergences"; this entry will be **removed** (it documented an unintentional
  bug as if it were a deliberate divergence, which is misleading).
- `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md` — new note
  recording the canonical names, where they came from, the cover
  decoupling rationale, and pointer to the reference fixture.
- `docs/cli/status.md` — append a "Phase G — canonical aux layout" entry
  marking M0..M10 work intact and the new feature green.

## 8. Risks

- **Build/link surface unchanged.** No new libslic3r dependencies; the
  stubs (`stubs_for_libslic3r.cpp`) are unaffected.
- **Manual GUI smoke gate still open.** None of the produced 3MFs have
  been signed off in Bambu Studio (`docs/cli/status.md`); after this
  change the user should open one in Bambu Studio to confirm the GUI's
  Project tab renders Model Pictures / Profile Pictures / Assembly Guide
  tabs correctly. This is the only way to catch UI-layer regressions —
  the invariant guards verify file presence and metadata but not UI
  rendering.
- **CLAUDE.md divergence note removal.** The current note advises future
  agents not to "fix" what was actually a bug. Replacing it with a
  correct note prevents a future agent from re-introducing the wrong
  names. Low risk; correction documented in the new note.
