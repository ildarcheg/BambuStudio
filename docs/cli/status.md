# bambu-cli — milestone status

One section per milestone. Smoke-gate checkbox is `[ ]` until the user
opens the produced 3MF in Bambu Studio and signs off; `[x]` after.
Update this file with each milestone landing.

---

## M0 — build skeleton + fixtures

- **Commit:** `55c588bde`
- **Date:** 2026-05-19
- **Acceptance:**
  - `src/cli/CMakeLists.txt` builds `bambu-cli` target
  - In-tree fixtures committed: `tests/cli/fixtures/reference.3mf` +
    `tests/cli/fixtures/stls/{cube,cylinder,cone}.stl`
  - CMake cache vars `BAMBU_CLI_REFERENCE_3MF` / `BAMBU_CLI_STL_DIR`
    declared
  - `bambu-cli --help` runs and prints subcommand list
- **Smoke-gate (Layer 2 / Bambu Studio):** N/A — no 3MF produced.

## M1 — `project init` + runtime invariant guard

- **Commit:** `b28ed12c9` (refactor `02d49c26e`)
- **Date:** 2026-05-19
- **Acceptance:**
  - `project init <out> --template <ref>` clones, loads, saves, runs
    guard, atomically renames
  - Guard checks (a) `.rels` Target resolution, (b) per-plate thumbnails,
    (c) vector-typed config round-trip
  - Negative test: corrupted template → exit 8 with named check failure
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — open the cloned
  reference 3MF; should look indistinguishable from input. _User to
  sign off._

## M2 — `inspect`

- **Commit:** `bda66c435`
- **Date:** 2026-05-19
- **Acceptance:**
  - `inspect <in>` prints plate / object / filament counts
  - `--json` emits Shape A
  - Missing file → exit 2
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — counts match what BS
  displays for the same 3MF. _User to sign off._

## M3 — `plate add` + `plate list`

- **Commits:** `db4e2b36e` (project_ops), `e020e7a1d` (tests),
  `e0bd8e0a4` (CLI wiring)
- **Date:** 2026-05-19
- **Acceptance:**
  - New plate gets a 128x128 placeholder thumbnail (G3)
  - Duplicate name → exit 5; empty name → exit 1
  - `plate list` lists names including the new plate
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — new plate visible in
  plate selector; switching to it shows correct bed dimensions. _User
  to sign off._

## M4 — `object add` (no flags) + `object list`

- **Commits:** `b5a82bead` (project_ops), `a950fa10d` (CLI wiring),
  fixes `bc98b0767`, `12d3039a7`
- **Date:** 2026-05-19
- **Acceptance:**
  - Bug B day-one stamp: every volume's `source.input_file` is set
  - Auto-arrange places object on bed
  - `<part>` in `model_settings.config` carries `source_file`
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — new object visible
  on target plate, renders normally. _User to sign off._

## M5 — `object add --filament` (Bug B regression)

- **Commits:** `bebda3727` (validation), `c74bcae6a` (failing test),
  `32ea80da2` (green)
- **Date:** 2026-05-19
- **Acceptance:**
  - Filament index validated against `filament_settings_id` length
  - `<part>` has BOTH `extruder=N` AND `source_file` (Bug B fix proof)
  - Out-of-range → exit 1
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — AMS slot N's color
  visible on the new object; no silent drop. _User to sign off._

## M6 — `object add --translate / --rotate / --scale / --count`

- **Commits:** `d91c054a2` (k-loop), `6739c96bd` (wire),
  `a812703fb` (tests), `209178aa3`, `07187c0d1`, `851b36c5f` (hotfixes)
- **Date:** 2026-05-19 → 2026-05-20
- **Acceptance:**
  - Each transform flag applies; combinations stack
  - `--count N` produces N independent ModelObjects (sqrt-grid layout)
  - Off-bed → exit 9 with rollback (no state change)
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — object at expected
  position / orientation / scale; stacked copies visible. _User to
  sign off._

## M7 — `config set / unset / list`

- **Commits:** `be898e436` (ops), `ab91f8c32` (tests),
  `7b376b4c5` + `ad1bd2315` (different_settings_to_system hotfix)
- **Date:** 2026-05-20
- **Acceptance:**
  - Project-level keys register in `different_settings_to_system` slot
    0 (process tab)
  - Per-object override goes to `ModelObject::config`
  - `--changed-only` uses `DynamicPrintConfig::diff` against
    `new_from_defaults_keys` (G6, no SEH)
  - Unknown key → exit 4
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — project override
  visible in process panel; per-object override visible in Object
  Manipulation panel. _User to sign off._

## M8 — `plate remove` + `plate rename`

- **Commits:** `e8b19486e` (ops), `d70cc75f8` (tests),
  `63794625f` (plate_index compaction hotfix)
- **Date:** 2026-05-20
- **Acceptance:**
  - `plate remove` rejects non-empty plates (exit 6) and compacts
    `plate_index` so reload doesn't reject the gap
  - `plate rename` updates name; collision → exit 5
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — removed plate gone;
  renamed plate's new name appears in selector. _User to sign off._

## M9 — `object remove` + `object set-filament`

- **Commits:** `6731bc0c1` (ops), `282599f47` (tests),
  `a8a321aff` (obj_inst_map by loaded_id fix)
- **Date:** 2026-05-20
- **Acceptance:**
  - Group-by-name semantics: remove all copies sharing a name
  - Bug B retrofit guard: `set-filament` re-stamps empty
    `source.input_file` before setting `extruder`
  - Out-of-range filament → exit 1
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — removed object gone;
  retrofit-filament object visible with new AMS slot color. _User to
  sign off._

## M10 — polish (JSON Shape A audit, exit-code matrix, manual-test recipe)

- **Commits:** `bb14cb899` (JSON audit + em-dash fix),
  `aee4a8168` (exit-code matrix tests), `a45cbe157` (manual-test.md),
  `2bda953c2` (vendored canonical fixtures)
- **Date:** 2026-05-20
- **Acceptance:**
  - Every command emits Shape A on success and error under `--json`
  - Exit codes 1, 2, 4, 5, 6, 9 each have at least one deterministic
    triggering test
  - `docs/cli/manual-test.md` covers the full v1 end-to-end workflow
- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` — run the full
  `docs/cli/manual-test.md` recipe end-to-end; final 3MF slices in BS
  with all changes visible. _User to sign off._

---

## Audit follow-ups (post-M10, 2026-05-20)

- **(P1) Cross-plate stride proven + docs corrected** — `8578a3351` +
  fixup `9b0faaa3e`. Adds `tests/cli/test_plate_stride.cpp`.
- **(P1) Archive invariants extracted** — `4f2b191a4` + fixup
  `7f8152b49`. `tests/cli/archive_invariants.{hpp,cpp}` + `run_all_basic`
  composer. Closes printable_area=4-points coverage gap.
- **(P1) `ExitCode` enum class** — `d6d4d49c5` + fixup `d5792c2ac`.
  `src/cli/exit_codes.hpp`.
- **(P2) Unit-test layer** — `037abb19b` + fixup `8a4e015dc`.
  `tests/cli/unit/` adds 39 unit tests over project_ops.
- **(P2) This status doc** — `9ae766993`.
- **(P3) Sister-project fixtures removed** — `46acbee50`.
  `tests/cli/fixtures/local/temp_project_for_orca_slicer.3mf` +
  `tests/cli/fixtures/local/stls/box_with_text.stl` deleted.

## Sibling-parity follow-ups (2026-05-21)

Cross-project review against OrcaSlicer's `src/cli/` (see
`docs/superpowers/plans/2026-05-21-cli-sibling-parity.md`). Four
correctness/safety/feature/hygiene gaps closed; four larger items
(Phase 10 info/profile/aux, split-to-parts, merge-parts, plate
thumbnail passthrough) de-scoped to dedicated follow-up plans.

- **(P1 — item 2) Classifier-aware `different_settings_to_system` routing**
  — `6b3afe863` (failing tests) + `944f151ca` (classifier port). Adds
  `classify_key_slot` dispatch via `Preset::{print,printer,filament}_options()`;
  filament keys broadcast to slots 2..fc+1, printer keys to slot 1, process
  and unknown keys remain at slot 0. Fixes silent-ignore of filament-tab
  and printer-tab project overrides at slice time.
- **(P1 — item 3) `.bak`-swap atomic save** — `ef3abc574` (pin test) +
  `ab4fbf3e6` (rewrite). `save_project` now does `rename(dst -> .bak)`,
  `rename(tmp -> dst)`, `remove(.bak)`; destination is never absent
  during the swap.
- **(P2 — item 4) `object set-filament --part Y`** — `8a0b943ef`
  (failing tests) + `a4dced83f` (op signature) + `dca182330` (CLI flag).
  Per-volume extruder assignment; pre-validates part index across all
  matching objects before any mutation. Unblocks merge-parts workflows.
- **(P3 — item 9) `nlohmann::json` migration** — `e7188d901` (include
  path precursor) + `c692c0ff5` (emitter + 4 command rewrites). Replaces
  hand-rolled `json_escape` + `std::ostringstream` with `nlohmann::json`
  object construction; envelope keys now alphabetically ordered. Fixes
  a silent-compile bug in `inspect.cpp` where `data` was emitted as a
  JSON string instead of a JSON object.

### De-scoped to follow-up plans (require their own plans before execution)

- **(P1 — item 5) Phase 10: `project info` / `project profile` / `project aux`** —
  ~398 LoC port from `OrcaSlicer/src/cli/project_tab_ops.{cpp,hpp}`.
  Needs Bambu-side verification of whether `store_bbs_3mf` reads
  `metadata_items["ProfileTitle"]` (Orca confirmed; Bambu unknown).
- **(P2 — item 6) `object split-to-parts`** — ~50 LoC delegation to
  `ModelVolume::split(remap_paint=true)` + `stamp_source_if_missing`
  Bug-C defense. Reference: `OrcaSlicer/src/cli/project_ops.cpp:651-699`.
- **(P2 — item 7) `object merge-parts`** — ~280 LoC with 8-step
  validation precedence + bake-in transform + single-volume
  serialization shim. Reference: `OrcaSlicer/src/cli/project_ops.cpp:701-984`.
  Critical: lowest-existing-index wins both placement AND attribution;
  use `add_volume(mesh, /*modify_to_center_geometry=*/false)`; never
  `ModelObject::merge_volumes` (3 documented bugs).
- **(P3 — item 8) Plate thumbnail passthrough from source** — ~250 LoC
  across 4 helpers. Requires `ProjectState::source_path` field addition.
  Reference: `OrcaSlicer/src/cli/io.cpp:200-342`.
- **(P3 — item 1, verification only)** Re-verified `bbs_3mf.cpp:4806`
  obj_inst_map.emplace still collapses duplicate object_id; no fork
  divergence. N-`ModelObject`s-per-`--count` model stands.

## Phase A — Architecture refactor (2026-05-21)

Sibling-parity architecture pass. Six atomic commits, each verified
green at the commit boundary. No new CLI surface introduced; user-
visible behavior of `bambu-cli` is unchanged (verified by
`bambu-cli --help` output diff and by the unchanged
`test_json_and_exit_codes.cpp` envelope + exit-code matrix).

Final test count: **92 cases / 445 assertions / 0 failures** (89 baseline
+ 3 new group-by-name unit tests added in A.6). The 25-assertion drop
from the baseline 447 in A.3 reflects the typed-exception refactor
replacing 2- and 3-assertion `OpResult.ok/error_code/exit_code` triples
with single `REQUIRE_THROWS_AS` calls.

- **(A.1) Test directory restructure** — `244f76d1b`. Moved 11 e2e test
  files from flat `tests/cli/` into `tests/cli/e2e/`, moved the pure
  unit test `test_plate_stride.cpp` into `tests/cli/unit/`, and added
  `tests/cli/roundtrip/` as an empty placeholder for future phases.
  Shared helpers (`cli_tests_main.cpp`, `test_helpers.{cpp,hpp}`,
  `archive_invariants.{cpp,hpp}`) stay at the `tests/cli/` root.
- **(A.2) Static lib split** — `49dcebae9`. Introduced
  `bambu_cli_core` STATIC library in `src/cli/CMakeLists.txt`
  containing every CLI translation unit except `main.cpp`. The
  `bambu-cli` executable and the `cli_tests` test binary now link
  the same lib instead of compiling the sources twice. Sibling parity
  with OrcaSlicer's `orca_cli_core`.
- **(A.3) Typed exceptions in project_ops** — `f00ca1bf3`. Added
  `src/cli/exceptions.hpp` with five typed exceptions
  (`FileNotFoundError`, `BadConfigError`, `DuplicateNameError`,
  `InvariantViolation`, `PlacementFailure`) plus the Bambu stdlib
  conventions (`std::invalid_argument` → exit 1,
  `std::out_of_range` → exit 6, `std::runtime_error` → exit 3). Every
  error site in `project_ops.cpp` now throws instead of populating
  `OpResult`. Unit tests updated to `REQUIRE_THROWS_AS`. The
  transitional helper `src/cli/commands/op_dispatch.hpp` lived for
  one phase, then was removed in A.4.
- **(A.4) MutationExceptionMap + run_mutation envelope** —
  `ac21aac05`. Added
  `src/cli/commands/mutation_runner.hpp` with
  `MutationExceptionMap` (typeindex → `{exit_code, error_code}`
  override map) and `template <Mutator> run_mutation(mode, in_path,
  out_path, mut, overrides={})`. Folds the
  `load_project → mutate → save_project → emit_ok` scaffolding plus
  the exception-to-ExitCode dispatch into a single envelope. Every
  mutating callback in `commands/{plate,object,config}.cpp` now uses
  `run_mutation`; no mutating callback calls `std::exit` directly.
  Read-only callbacks (list verbs, inspect) and `project init`
  (clone-and-verify) retain their own flows.
- **(A.5) emit_list_response template** — `96465bcfc`. Added
  `emit_list_response<Row, ToJson, ToLine>` to
  `src/cli/json_output.hpp`. Refactored `plate list`, `object list`,
  and `config list` to delegate the JSON-array-vs-text-line branching
  to the template. `inspect` (single-record emit) keeps its own
  inline path.
- **(A.6) Group-by-name on `config set/unset/list --object`** —
  `474478f9b`. Replaced the first-match `find_object_by_name` lookup
  in `config_set`, `config_unset`, and `config_list` with a walk over
  every matching `ModelObject`. Sibling parity with OrcaSlicer commit
  `c2ddf51d87`. Added three unit tests under `[unit][config][group]`
  exercising set / unset / list across `--count 2` clones.

**Delta over `src/cli` + `tests/cli`**: 26 files changed, +660 / -455
(2 new headers: `exceptions.hpp`, `commands/mutation_runner.hpp`).

---

## Phase C — Project tab (2026-05-21 / 2026-05-22)

10 new leaf verbs, 5 new typed exceptions, PNG cover validator,
Windows-safe aux-name sanitizer. All verbs use the Phase-A architecture
(run_mutation / emit_list_response / typed exceptions / static lib).

### Sub-phases

- **(C.0) BBS 3MF profile storage investigation** — `a8b885eed`
  (docs-only commit). Grepped `src/libslic3r/Format/bbs_3mf.cpp` for
  ProfileTitle, metadata_items, profile_info, cover. Finding:
  `store_bbs_3mf` reads profile fields from `model.profile_info` struct
  directly, NOT from `metadata_items["ProfileTitle"]` (Orca reads from
  metadata_items — hence Orca CLI mirrors into both; Bambu CLI does NOT
  need to mirror). Info fields read from `model.model_info` struct.
  Cover paths are archive-relative strings; image bytes live in the
  aux temp dir (`Auxiliaries/`). Note: Bambu has a typo `ProfileTile`
  (not `ProfileTitle`) in `Model.hpp`. Findings documented in
  `docs/cli/notes/2026-05-21-bbs-profile-storage.md`.

- **(C.1) project info show/set/clear + PNG cover validator** —
  `faf0504e4`. New files: `src/cli/project_tab_ops.{hpp,cpp}`,
  `src/cli/commands/project_tab.{hpp,cpp}`. Exceptions: `BadCoverImage`
  (exit 4), `InvalidField` (exit 4) — derived from `BadConfigError`.
  Cover embed: validates 8-byte PNG signature, writes bytes to aux
  temp dir, sets `model_info->cover_file = "Auxiliaries/cover.png"`.
  Tests: 27 new cases / 98 new assertions.

- **(C.2) project profile show/set/clear** — `78e4cb81e`.
  Implementation scaffolded in C.1; this commit adds tests only.
  Applies C.0 finding: writes go directly to `model.profile_info`
  struct (ProfileTile/ProfileDescription/ProfileCover) — no
  metadata_items mirroring. user_id/user_name read-only (not settable
  or clearable). Tests: 26 new cases / 82 new assertions.

- **(C.3) project aux list/add/remove/export + sanitize_aux_name** —
  `c8186e0ac`. New exceptions: `BadAuxFile` (exit 2), `AuxNameError`
  (exit 4), `AuxCollisionError` (exit 5). `AuxFolder` enum + helpers
  `folder_flag` / `folder_json_key` / `folder_subdir`. sanitize_aux_name
  rejects: path separators, dot-only names, leading/trailing whitespace,
  22 Windows reserved names (CON PRN AUX NUL COM1-9 LPT1-9) case-
  insensitive. Critical fix: added `LoadAuxiliary` to `load_model_and_config()`
  in `io.cpp` so aux files extracted from archive persist across CLI
  invocations. Tests: `DYNAMIC_SECTION` over the full reserved-name set
  per Orca §9 pattern. Fixture: `assembly_smoke.txt` (12 bytes).
  26 new cases.

### Summary

| Item | Detail |
|---|---|
| New leaf verbs | 10 (`project info show/set/clear`, `profile show/set/clear`, `aux list/add/remove/export`) |
| New typed exceptions | 5 (`BadCoverImage`, `InvalidField`, `BadAuxFile`, `AuxNameError`, `AuxCollisionError`) |
| New source files | `project_tab_ops.{hpp,cpp}`, `commands/project_tab.{hpp,cpp}` |
| Phase C test growth | +79 cases / +313 assertions (92→171 total) |
| Final suite | **171 cases / 758 assertions / 0 failures** |
| C.0 commit | `a8b885eed` |
| C.1 commit | `faf0504e4` |
| C.2 commit | `78e4cb81e` |
| C.3 commit | `c8186e0ac` |

- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` pending user verification.

---

## Phase D — split / merge (2026-05-22)

Two new leaf verbs on `object`. Both use the Phase A architecture
(run_mutation / typed exceptions / static lib). Both use first-match
semantics on `--name` (not group-by-name) — splitting or merging across
a clone-group is ambiguous; deferred per Orca CLI report §10.

### Sub-phases

- **(D.0) Multi-component STL fixture** — `d3621a33c`. Added
  `tests/cli/fixtures/stls/two_cubes.stl` (two 10 mm cubes offset 30 mm
  along X, exactly 1284 bytes binary STL, 24 triangles). Extended
  `tests/cli/fixtures/gen_fixtures.cpp` with `make_two_cubes()`.

- **(D.1) `object split-to-parts`** — `0f85e20bd`. Delegates to
  `ModelVolume::split(filament_count)`. Validates: exactly 1 volume,
  must be `MODEL_PART`. Captures `source.input_file` before split (split
  resets it on the first resulting volume); re-stamps all volumes
  after. `std::invalid_argument` remapped to exit 7 (`invalid_state`)
  via `MutationExceptionMap` override. Text output:
  `split-to-parts: <name> -> <K> parts.` Tests: +9 cases / +37
  assertions.

- **(D.2) `object merge-parts`** — `a541471ae`. 8-step deterministic
  validation (fail-fast order: object-lookup → part-lookup → into-
  collision → filament-range → MODEL_PART-check → empty-mesh-check →
  filament-agreement → non-extruder-config-key). Execution: bakes each
  source's `get_matrix()` transform into a `TriangleMesh` copy, merges
  via `TriangleMesh::merge`, calls `add_volume(mesh, false)` to bypass
  bbox-center shift, places result at lowest-source-index slot via
  `std::rotate`, deletes sources in reverse-index order. Single-volume
  serialization shim: if only 1 volume remains after merge, extruder is
  also written to `obj.config` (BBS strips it from volume-level config
  at save time). Tests: +19 cases / +95 assertions.

### Summary

| Item | Detail |
|---|---|
| New leaf verbs | 2 (`object split-to-parts`, `object merge-parts`) |
| New fixture | `two_cubes.stl` (1284 bytes, 2 disconnected 10 mm cubes) |
| New source files | `tests/cli/unit/test_project_ops_{split,merge}.cpp`, `tests/cli/e2e/test_object_{split,merge}.cpp` |
| Phase D test growth | +28 cases / +132 assertions (171→199 total) |
| Final suite | **199 cases / 890 assertions / 0 failures** |
| D.0 commit | `d3621a33c` |
| D.1 commit | `0f85e20bd` |
| D.2 commit | `a541471ae` |

- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` pending user verification.

---

## Phase B — Thumbnail passthrough (2026-05-22)

Plate thumbnail passthrough and valid-PNG placeholder generator. No new
CLI surface; purely internal change to `save_project`.

### Sub-phases

- **(B.1) PNG placeholder generator** — `d0d315154`. New files:
  `src/cli/png_placeholder.{hpp,cpp}`. `make_placeholder_png_128()`
  emits a 128×128 RGBA 0xC0 PNG via hand-rolled IHDR/IDAT/IEND chunk
  emitter backed by `mz_crc32` + `mz_compress2(MZ_NO_COMPRESSION)`.
  6 unit tests in `tests/cli/unit/test_png_placeholder.cpp` covering
  PNG signature, IHDR fields, IDAT decompression size and pixel values,
  IEND presence, and CRC validation (including known IEND CRC
  `0xAE426082`).

- **(B.2) Thumbnail passthrough in `save_project`** — `e054819a5`.
  Added `source_path` to `ProjectState` (set in `load_project`). Added
  `rewrite_thumbnails()` in `io.cpp`: after `store_bbs_3mf` writes the
  output archive, replaces each `plate_N.png` / `plate_N_small.png`
  entry with either a zero-copy passthrough from the source archive
  (`mz_zip_writer_add_from_zip_reader`) or a synthesized placeholder PNG
  for new plates. Updated `assert_plate_thumbnails_128` in
  `archive_invariants.cpp` to allow `plate_N.png` at any valid size
  (source thumbnails may be 512×512); `plate_N_small.png` must still be
  exactly 128×128. 3 e2e tests in
  `tests/cli/e2e/test_thumbnail_passthrough.cpp`.

### Summary

| Item | Detail |
|---|---|
| New source files | `src/cli/png_placeholder.{hpp,cpp}`, `tests/cli/unit/test_png_placeholder.cpp`, `tests/cli/e2e/test_thumbnail_passthrough.cpp` |
| Phase B test growth | +9 cases / +76 assertions (199→208 total) |
| Final suite | **208 cases / 966 assertions / 0 failures** |
| B.1 commit | `d0d315154` |
| B.2 commit | `e054819a5` |

- **Smoke-gate (Layer 2 / Bambu Studio):** `[ ]` pending user
  verification — open a CLI-produced .3mf; plate thumbnail should
  now show the original source image rather than a gray placeholder.

---

## Phase F — Cleanups (2026-05-22)

Targeted cleanups across three sub-phases. No new CLI surface.

### F.1 — Save-path polish (`864abad7a`)

- **`io.cpp` `fs::remove(bak)` overload fix:** pre-swap stale-.bak
  cleanup now uses the error_code overload (best-effort) rather than the
  throwing overload, matching the post-swap remove at line 302. A locked
  .bak from another process no longer propagates into the outer catch and
  surfaces as "rename failed:" before the rename even attempted.
- **`--verbose` no-op:** hidden from `--help` via `group("")` and marked
  intentional no-op in a comment. Wiring to stage callbacks would require
  >30 LOC across 5 register functions. Flag is still parsed so existing
  scripts don't break.

### F.2 — Code organization (`dd0e70886`, `0a5ce5ba0`)

- **All 10 typed exceptions in one file:** `BadCoverImage`, `InvalidField`,
  `BadAuxFile`, `AuxNameError`, `AuxCollisionError` moved from
  `project_tab_ops.hpp` to `exceptions.hpp` alongside the 5 Phase A
  bases. Include chain unchanged (`project_tab_ops.hpp` already includes
  `exceptions.hpp`).
- **Doc nit fixed:** comment at `project_tab_ops.hpp:99` now reads
  `"Auxiliaries/"` (matching the Bambu archive convention) instead of
  `"Auxiliary/"` (Orca convention).
- **`find_object_by_name` inlined and deleted:** sole caller in
  `commands/config.cpp` replaced with `std::any_of`; helper removed from
  `project_ops.hpp/.cpp`. Associated test and stale comment also removed.

### F.3 — Test coverage gaps (`ba627a119`)

- **Step a added to `merge_object_parts`:** empty `--parts` now throws
  `std::invalid_argument` before the object-lookup step, making it
  unit-testable.
- **7 new fail-fast pairing tests** in `test_project_ops_merge.cpp`:
  a→b, b→c, c→d, e→f, f→g, g→h, h→i.
- **2 dedicated step tests** added: non-MODEL_PART source (step f) and
  empty-mesh source (step g).
- **Vacuous-skip fixed** in `test_project_ops_config.cpp`: `if (!opt)
  return` replaced with `REQUIRE(opt != nullptr)` so the M1-unset loop
  assertion cannot be silently bypassed.

### Summary

| Item | Detail |
|---|---|
| Phase F test growth | +9 cases / +37 assertions (208→216 total) |
| Final suite | **216 cases / 1003 assertions / 0 failures** |
| F.1 commit | `864abad7a` |
| F.2 commits | `dd0e70886`, `0a5ce5ba0` |
| F.3 commit | `ba627a119` |

---

## Phase E — `--part` NAME rename (2026-05-22)

**Breaking change.** The `--part` option of `object set-filament` changes
type from integer index to string volume name, aligning with OrcaSlicer's
group-aware design.

### E.0 — Investigation

`ModelVolume::split` (Model.cpp:3503) assigns names as
`original_vol_name + "_" + std::to_string(idx + 1)`, so splitting an object
named `"twin"` produces volumes `"twin_1"`, `"twin_2"`, etc. The CLI's
`split_object_to_parts` pre-aligns the volume name to the object name
(project_ops.cpp:940) before calling split, so the naming is deterministic.

### E.1 — Implementation + tests + docs

**Breaking changes:**

- **`--part` type:** `INT` → `TEXT`. Any script passing `--part 0` will now
  receive exit 6 (unknown_reference) unless a volume happens to be named
  `"0"`.
- **Exit code for unknown part:** was exit 1 (usage_error), now exit 6
  (unknown_reference), matching how unknown object name is handled.
  Error message: `"part name '<NAME>' not found across <K> matching object(s)"`.

**New semantics:**

When `--part NAME` is given, all volumes named `NAME` across **all matched
objects** (the group matched by `--name`) receive the extruder stamp. Throws
`std::out_of_range` (exit 6) if no matching volume is found.

**Tests migrated:**

- `test_project_ops_objects.cpp` — two `[m3_part_filament]` tests rewritten:
  use `two_cubes.stl` + `split_object_to_parts` → real volume names
  `"twin_1"` / `"twin_2"`; unknown-name case now expects `std::out_of_range`
  (not `std::invalid_argument`).
- `test_object_merge.cpp` — step-h disagreement test updated from
  `--part 0` / `--part 1` to `--part twin_1` / `--part twin_2`.

### Summary

| Item | Detail |
|---|---|
| Phase E test growth | +6 assertions (216 cases / 1003→1009 assertions) |
| Final suite | **216 cases / 1009 assertions / 0 failures** |

## Phase G — Canonical aux folder layout (2026-05-26)

- [x] AuxFolder enum renamed to canonical names (`ModelPictures`,
      `ProfilePictures`, `BillOfMaterials`, `AssemblyGuide`, `Others`).
- [x] DesignerCover / ProfileCover decoupled into own folders + own basenames.
- [x] `--cover-name` selects existing image in folder; mutual exclusion +
      `sanitize_aux_name` enforced at CLI layer.
- [x] PNG + JPEG accepted (via `is_png_or_jpeg`).
- [x] `check_auxiliary_passthrough` + `check_cover_references_resolve`
      invariant guards live in the save path. `check_auxiliary_passthrough`
      compares the in-memory aux temp dir against the saved archive
      (redesigned during execution from the original pre-vs-post diff
      to avoid false positives on legitimate `aux remove` / `aux add
      --force` mutations).
- [x] `tests/cli/fixtures/test_reference.3mf` committed; round-trip
      test asserts canonical layout preservation (43 assertions).
- [x] Final suite: **266 cases / 1307 assertions / 0 failures**.
- [x] Manual GUI smoke (2026-05-27): user opened
      `smoke-out/02_full_build.3mf` in Bambu Studio and confirmed all
      Project tab fields populate (title, description, license,
      copyright, profile title, profile description), both real-image
      covers render (SAMPLE designer cover, ORIGINAL profile cover),
      the 4 MB Assembly Guide PDF is clickable and opens in a PDF
      viewer, Bill of Materials + Others files are listed, and the
      `smoke_cube` object renders in the 3D view on `Plate 01 test`.
      Build recipe: `bambu-cli object add` then `project info set`
      then `project profile set` then three `project aux add` calls
      with `--folder assembly-guide / bill-of-materials / others`.

## M11 — Layout operations (2026-05-29)

- [x] `plate center` (`project_ops::plate_center`)
- [x] `plate drop-to-bed` (`project_ops::plate_drop_to_bed`, hull-based min-Z)
- [x] `plate arrange` (`project_ops::plate_arrange`, libslic3r helpers)
- [x] `plate auto-orient` (`project_ops::plate_auto_orient`, orient + implicit drop)
- [x] `object auto-orient` (`project_ops::object_auto_orient`, group-by-name)
- [x] `object add` default placement: center + Z-drop (replaces sqrt-grid)
- [x] Manual GUI sign-off (Steps 16–20 of `docs/cli/manual-test.md`) — 2026-05-30, six smoke artifacts verified in Bambu Studio (one per verb plus the `object add` default-placement change)

Spec: `docs/superpowers/specs/2026-05-29-arrange-center-drop-orient-design.md`
Plan: `docs/superpowers/plans/2026-05-29-arrange-center-drop-orient.md`
Notes: `docs/cli/notes/2026-05-29-drop-to-bed-hull-vs-mesh.md`

## M12 — `project apply` batch-manifest verb (2026-06-01)

- [x] `project apply <in.3mf> --manifest m.json [--output out.3mf] [--dry-run]` registered as a `project`-subcommand
- [x] Manifest schema: `{"version":1,"operations":[{"op":...,...args}]}`; strict unknown-key/unknown-field rejection; 10,000-op cap
- [x] 14 mutating-verb handlers via `HandlerRegistry`/`HandlerEntry` (one entry per op):
  - `plate.add` / `plate.remove` / `plate.rename`
  - `plate.center` / `plate.drop-to-bed` / `plate.arrange` / `plate.auto-orient` (override `runtime_error → exit 7`)
  - `object.add` / `object.remove` / `object.set-filament`
  - `object.auto-orient` (override `runtime_error → exit 7`)
  - `object.split-to-parts` (override `invalid_argument → exit 7`)
  - `object.merge-parts` (override `invalid_argument → exit 7`)
  - `config.set` / `config.unset` (single-key OR batch `values:` / `keys:` form, mutually exclusive)
- [x] `ManifestFieldError` (subclass of `std::invalid_argument`) thrown by all schema validators; `exception_dispatch::dispatch` short-circuits it to exit 1 BEFORE the per-op `MutationExceptionMap` lookup, so manifest typos can't be misclassified as `invalid_state` on the four exit-7 verbs
- [x] `--dry-run` runs stages 1–5 of the dispatch flow and skips `save_project` (one boolean guard at stage 6, no `ProjectState` deep-copy)
- [x] `ConfigBatchError` carries `failing_key` context for mid-batch `config.set`/`config.unset` failures; surfaces in both the error message prefix and the `failing_key` field of the JSON error envelope
- [x] STL paths in the manifest resolve relative to the manifest file's directory (thread-local `g_manifest_dir`)
- [x] `exception_dispatch.{hpp,cpp}` refactor lifts the exception → exit-code table out of `commands/mutation_runner.hpp` (behaviour-preserving for every existing single-verb call site; 331 pre-existing tests stay green)
- [x] `emit_error` extended with optional `data` blob (merges keys at top level; backwards-compatible default)
- [x] Test coverage: +116 cases / +239 assertions (331/4032 → 447/4271). Breakdown:
  - unit/test_apply_helpers.cpp — 18 cases (require_only / parse_filament / parse_transform)
  - unit/test_apply_manifest.cpp — 11 cases (manifest header validation)
  - unit/test_project_apply_handlers.cpp — 79 cases (per-handler happy + rejection + override-shape + ConfigBatchError + functional reach-through)
  - e2e/test_project_apply.cpp — 12 cases (dry-run × 2, manifest × 4, schema-vs-semantic exit-7 × 5, 12-plate workflow × 1)
  - roundtrip/test_apply_roundtrip.cpp — 3 cases (empty-manifest roundtrip, sequential-vs-batch equivalence, plus one consequence assertion)
- [ ] Manual GUI sign-off — six smoke artifacts to produce + open in Bambu Studio (12-plate workflow, dry-run, schema-typo error envelope, config-batch failing_key error envelope, exit-7 semantic split, object.add via batch). Recipe TBD as steps 21+ of `docs/cli/manual-test.md`.

Spec: `docs/superpowers/specs/2026-05-31-project-apply-batch-design.md`
Plan: `docs/superpowers/plans/2026-05-31-project-apply-batch.md`
Range: `029c51e85..7777688ab` (28 commits — 27 plan tasks + 1 polish fix `7777688ab` resolving final-review Issues 1 + 2 on optional-field type-checks and missing happy-path tests). Merge commit: `31529bf6e`.
