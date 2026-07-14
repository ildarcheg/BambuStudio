# Phase C.0 — BBS 3MF Profile/Info Storage Investigation

**Date:** 2026-05-21  
**Question:** Does `store_bbs_3mf` in this Bambu fork read profile fields from
`model_info->metadata_items["ProfileTitle"]` (Orca's known behavior), from
`model.profile_info` directly, or both?

**Answer:** Bambu reads from `model.profile_info` **directly** — no mirroring
into `metadata_items` is required for the CLI.

---

## 1. Struct Definitions (Model.hpp:1538–1580)

```cpp
class ModelProfileInfo {          // Model.hpp:1540
    std::string ProfileTile;      // NOTE: "Tile" typo in Bambu source, not "Title"
    std::string ProfileCover;     // archive-relative path to cover PNG
    std::string ProfileDescription;
    std::string ProfileUserId;    // read-only (not set by CLI)
    std::string ProfileUserName;  // read-only
};

class ModelInfo {                 // Model.hpp:1559
    std::string cover_file;       // archive-relative path to cover PNG
    std::string license;
    std::string description;
    std::string copyright;
    std::string model_name;       // the "title" field
    std::string origin;           // read-only in info show; not in info set
    std::map<std::string, std::string> metadata_items; // passthrough bag
};
```

---

## 2. Store Path (`store_bbs_3mf` / `_add_model_file_to_archive`)

Source: `src/libslic3r/Format/bbs_3mf.cpp:6950–7010`

**Info fields**: read from `model.model_info` struct fields directly:
```
6976  if (model.model_info) {
6977      design_cover = model.model_info->cover_file;
6978      license      = model.model_info->license;
6979      description  = model.model_info->description;
6980      copyright    = model.model_info->copyright;
6981      name         = model.model_info->model_name;
6982      origin       = model.model_info->origin;
```

**Profile fields**: read from `model.profile_info` struct fields directly:
```
6986  if (model.profile_info) {
6987      profile_title = model.profile_info->ProfileTile;
6988      profile_cover = model.profile_info->ProfileCover;
6989      profile_description = model.profile_info->ProfileDescription;
```

**Metadata assembly**: The store function seeds a local `metadata_item_map`
from `model.model_info->metadata_items` (line 6995–6996), then OVERWRITES the
named fields:
```
6999  metadata_item_map["Title"]              = xml_escape(name);
7004  metadata_item_map["Description"]        = xml_escape(description);
7005  metadata_item_map["Copyright"]          = xml_escape(copyright);
7006  metadata_item_map["License"]            = xml_escape(license);
7007  metadata_item_map["ProfileTitle"]       = xml_escape(profile_title);
7008  metadata_item_map["ProfileCover"]       = xml_escape(profile_cover);
7009  metadata_item_map["ProfileDescription"] = xml_escape(profile_description);
```

**The profile fields in `metadata_items` are DERIVED FROM `profile_info`**,
not the other way around. Writing to `metadata_items["ProfileTitle"]` alone
would be overwritten by the value from `model.profile_info->ProfileTile`.

---

## 3. Load Path (`load_bbs_3mf` / `_handle_end_element_name`)

Source: `src/libslic3r/Format/bbs_3mf.cpp:4097–4156`, `1568–1577`, `1877–1888`

The parser reads XML elements into member variables:
```
4097  if (m_curr_metadata_name == "Title")  model_info.model_name = ...
4115  if (m_curr_metadata_name == "DesignerCover")  model_info.cover_file = ...
4118  if (m_curr_metadata_name == "Description")  model_info.description = ...
4121  if (m_curr_metadata_name == "License")  model_info.license = ...
4127  if (m_curr_metadata_name == "Copyright")  model_info.copyright = ...
4134  if (m_curr_metadata_name == "ProfileTitle")  m_profile_title = ...
4136  if (m_curr_metadata_name == "ProfileCover")  m_profile_cover = ...
4140  if (m_curr_metadata_name == "ProfileDescription")  m_Profile_description = ...
4143  if (m_curr_metadata_name == "ProfileUserId")  m_profile_user_id = ...
4146  if (m_curr_metadata_name == "ProfileUserName")  m_profile_user_name = ...
4156  else  model_info.metadata_items[m_curr_metadata_name] = ...  // generic passthrough
```

After parsing, the importer populates:
```
1568  m_model->profile_info = make_shared<ModelProfileInfo>();
1569  m_model->profile_info->ProfileTile     = m_profile_title;
1570  m_model->profile_info->ProfileCover    = m_profile_cover;
1571  m_model->profile_info->ProfileDescription = m_Profile_description;
1572  m_model->profile_info->ProfileUserId   = m_profile_user_id;
1573  m_model->profile_info->ProfileUserName = m_profile_user_name;
1576  m_model->model_info = make_shared<ModelInfo>();
1577  m_model->model_info->load(model_info);  // copies all struct fields + metadata_items
```

Note: `model_info.metadata_items` does NOT include ProfileTitle/ProfileCover/etc.
because those branches (`4134` etc.) write to `m_profile_*` vars, not to
`model_info.metadata_items`. Only truly-unrecognized metadata tags end up in
`metadata_items` via the `else` branch at line 4156.

---

## 4. Divergence from Orca

Orca's `store_bbs_3mf` reads profile fields from `metadata_items["ProfileTitle"]`
(see Orca report §5). Therefore Orca's CLI must mirror writes into both:
- `model.profile_info->ProfileTitle`
- `model.model_info->metadata_items["ProfileTitle"]`

**Bambu does NOT require this mirroring.** `store_bbs_3mf` here reads profile
fields from `model.profile_info` struct directly and derives the metadata_items
entry at write time. Writing only to `profile_info` is sufficient.

---

## 5. CLI Implementation Strategy for C1/C2

### info set / info clear
- Set `model.model_info->model_name` (title)
- Set `model.model_info->description`
- Set `model.model_info->license`
- Set `model.model_info->copyright`
- Cover (see §6 below)
- Ensure `model.model_info` shared_ptr is non-null before first write
  (create with `make_shared<ModelInfo>()` if nullptr)
- `origin` is **read-only** — appears in `info show` only; not a settable field

### profile set / profile clear
- Set `model.profile_info->ProfileTile` (note the typo: "Tile" not "Title")
- Set `model.profile_info->ProfileDescription`
- Cover (see §6 below)
- Ensure `model.profile_info` shared_ptr is non-null (create if nullptr)
- `ProfileUserId` / `ProfileUserName` are **read-only** — shown in `profile show`
- **No mirroring into metadata_items required** (divergence from Orca)

### profile show
Five fields: title (from `ProfileTile`), description (from `ProfileDescription`),
cover (from `ProfileCover` — path string), user_id (from `ProfileUserId`),
user_name (from `ProfileUserName`).

---

## 6. Cover Image Handling

`cover_file` (info cover) and `ProfileCover` (profile cover) store an
**archive-relative path string** such as `"Auxiliary/cover.png"`.

The actual image bytes live in the 3MF ZIP as a separate entry. During load with
`LoadAuxiliary`, `_extract_auxiliary_file_from_archive` extracts files to
`model.get_auxiliary_file_temp_path()` (a PID/time-keyed temp dir under the
system tmp path). During save, `_add_auxiliary_dir_to_archive`
(`bbs_3mf.cpp:8657`) walks that temp dir and re-packs entries into the archive.

**Current CLI issue**: `load_project` in `src/cli/io.cpp:79-84` uses only
`LoadModel | LoadConfig` — NOT `LoadAuxiliary`. Auxiliary files (including cover
images and aux files) are therefore never extracted to the temp dir. On save,
the temp dir is empty, so any existing auxiliary files from the original archive
are silently dropped on round-trip.

**Impact for C1/C2 (cover set)**:
- For `info set --cover IMG` / `profile set --cover IMG`, we must:
  1. Validate the 8-byte PNG signature.
  2. Write the PNG bytes to `<aux_temp_dir>/<cover_filename>`.
  3. Set `model.model_info->cover_file` / `model.profile_info->ProfileCover`
     to the archive entry name (e.g., `"Auxiliary/cover.png"`).
  4. `save_project` will then pack it from the temp dir.
- Because `LoadAuxiliary` is not used, existing cover data is NOT preserved on
  round-trip through a `set` operation unless we also pass LoadAuxiliary first.
  Decision for C1/C2: **use LoadAuxiliary for info/profile mutating verbs** so
  existing aux files (and existing covers for the non-set fields) survive.

**Impact for C3 (aux list/add/remove/export)**:
- Must use `LoadAuxiliary` for all aux verbs.
- `io.cpp::load_project` should be extended or a variant created that accepts
  a LoadStrategy parameter, OR the aux verbs call `load_bbs_3mf` with the
  additional flag at the project_tab_ops layer.

---

## 7. Summary / Decision

| Field | Bambu struct path | Write strategy |
|---|---|---|
| info title | `model.model_info->model_name` | set struct field |
| info description | `model.model_info->description` | set struct field |
| info license | `model.model_info->license` | set struct field |
| info copyright | `model.model_info->copyright` | set struct field |
| info cover | `model.model_info->cover_file` (path) | set path + write PNG to aux temp dir |
| info origin | `model.model_info->origin` | **read-only** |
| profile title | `model.profile_info->ProfileTile` | set struct field (note typo) |
| profile description | `model.profile_info->ProfileDescription` | set struct field |
| profile cover | `model.profile_info->ProfileCover` (path) | set path + write PNG to aux temp dir |
| profile user_id | `model.profile_info->ProfileUserId` | **read-only** |
| profile user_name | `model.profile_info->ProfileUserName` | **read-only** |

**No metadata_items mirroring is needed for profile fields in Bambu**
(unlike Orca where it is required).
