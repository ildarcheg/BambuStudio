# BambuStudio fork — changes on top of upstream baseline

Generated: 2026-05-22 — for cross-comparison with the parallel OrcaSlicer fork.

## Baseline

- **Upstream remote:** `upstream` → `https://github.com/bambulab/BambuStudio.git`
- **Baseline commit SHA:** `22638157e2680dfffcf65062f0caf1152ca40252` ("FIX: AMSMaterialsSetting calls non-existent get_extruder_id_by_ams_id")
- **How identified:** `git merge-base HEAD upstream/master`. This is the divergence point between `master` (HEAD = `5c2700028`) and `upstream/master`.
- **Scope:** 91 non-merge commits ahead of upstream; 25,815 line additions across 87 new files; **only 2 upstream files modified** — both single-line `add_subdirectory(cli)` additions.

## Summary of Added Functionality

All work is one project: **bambu-cli**, a standalone executable (`bambu-cli.exe`) that composes `.3mf` project files by driving libslic3r's BBS 3MF reader/writer with no GUI dependency. Distinct capabilities added on top of upstream:

- New `bambu-cli` executable + `bambu_cli_core` static library, separate from `BambuStudio.exe` (`src/cli/`).
- `project init` — atomic clone-and-validate of a template `.3mf` with pre-save and post-save invariant guards.
- `inspect` — read-only plate/object/filament-slot counts.
- `plate add` / `plate list` / `plate remove` / `plate rename` — full plate-level CRUD.
- `object add` — load an STL onto a named plate with `--filament`, `--translate`, `--rotate`, `--scale`, `--count`, including a sqrt-grid auto-arrange layout and an off-bed AABB check.
- `object list` / `object remove` / `object set-filament` — group-by-name object operations, including per-volume filament stamping (`--part NAME`).
- `object split-to-parts` / `object merge-parts` — first-match volume operations (mesh-component split, named-volume merge).
- `config set` / `config unset` / `config list` — project-level and per-object config edits with `different_settings_to_system` routing for GUI compatibility.
- `project info show/set/clear` and `project profile show/set/clear` — model_info and profile_info field edits with PNG-validated cover images.
- `project aux list/add/remove/export` — auxiliary files (Pictures / Bom / AssemblyGuide / Others) ingest and extract, with name sanitization and `--force` collision handling.
- Runtime invariant guard (`run_guard`) that re-opens every saved `.3mf` and verifies relationship targets, per-plate thumbnails, and vector-config round-trip.
- Thumbnail passthrough: copy original plate thumbnails from the source archive into the rewritten archive; synthesize a valid 128×128 placeholder PNG for new plates.
- `--json` Shape A envelope across every subcommand, plus a documented 0–9 `ExitCode` enum.
- Bug B retrofit: every volume's `source.input_file` is stamped on add and re-stamped on `set-filament` so AMS color carries through GUI reload.
- Multi-plate world-coordinate stride (`plate_world_origin`) matching BBS `PartPlateList`.
- Atomic `.bak`-swap save pattern (ported from OrcaSlicer M11) eliminating the rename window where destination is absent.
- Comprehensive Catch2 test suite: 216 `TEST_CASE`s across 27 files, split into `e2e/` (load → CLI run → reload) and `unit/` (project_ops in-process).
- Vendored CLI11 v2.4.2 single-header parser under `src/cli/extern/CLI11/`.
- Vendored canonical test fixtures (reference 3MF, STL set) under `tests/cli/fixtures/local/`.
- Plan + spec + status docs under `docs/cli/` and `docs/superpowers/` (4,835-line implementation plan, 550-line milestone status, 383-line manual-test recipe, BBS profile storage note).

## Changed Files

### Upstream files touched (only 2, both 1-line additions)
- `src/CMakeLists.txt:28` — `add_subdirectory(cli)` for the new exe target
- `tests/CMakeLists.txt:38` — `add_subdirectory(cli)` for the new test target

### New CLI source library (`src/cli/`)
- `src/cli/CMakeLists.txt` — defines `bambu_cli_core` STATIC + `bambu-cli` exe; vendors DLL copy POST_BUILD step on Windows
- `src/cli/main.cpp` — exe entry point; wires `--json`, `--verbose`, top-level subcommand registration
- `src/cli/exit_codes.hpp` — `ExitCode` enum (0..9) per spec §3
- `src/cli/exceptions.hpp` — typed exception hierarchy mapped to exit codes (`FileNotFoundError`, `BadConfigError`, `DuplicateNameError`, `InvariantViolation`, `PlacementFailure`, plus Phase-C derivations `BadCoverImage`, `InvalidField`, `BadAuxFile`, `AuxNameError`, `AuxCollisionError`)
- `src/cli/project_state.hpp` — `ProjectState` holder (Model / DynamicPrintConfig / PlateDataPtrs / source_path) with raw-ptr ownership in dtor
- `src/cli/io.hpp` / `io.cpp` — `load_project`, `save_project`, `atomic_copy`, `rebuild_objects_and_instances` (G2), placeholder thumbnail fill (G3), `rewrite_thumbnails` passthrough, `.bak`-swap rename
- `src/cli/invariant_guard.hpp` / `invariant_guard.cpp` — three-check post-save guard (rels target resolution, per-plate thumbnails, vector config round-trip); `check_thumbnails_in_archive` for pre-save validation in `project init`
- `src/cli/project_ops.hpp` / `project_ops.cpp` — pure mutations: `add_plate`, `remove_plate`, `rename_plate`, `list_plate_names`, `add_object_to_plate`, `list_objects`, `remove_object`, `set_object_filament`, `config_set/unset/list`, `split_object_to_parts`, `merge_object_parts`, `plate_world_origin`
- `src/cli/project_tab_ops.hpp` / `project_tab_ops.cpp` — info/profile/aux read+mutate ops (Phase C)
- `src/cli/json_output.hpp` / `json_output.cpp` — Shape A envelope (`emit_ok`, `emit_error`, templated `emit_list_response`)
- `src/cli/png_placeholder.hpp` / `png_placeholder.cpp` — hand-rolled 128×128 RGBA-0xC0 PNG generator (IHDR + IDAT/MZ_NO_COMPRESSION + IEND) for newly-added plates
- `src/cli/stubs_for_libslic3r.cpp` — link-time stubs for `Slic3r::Http` and `BBL_Encrypt` so bambu-cli does not drag in curl / OpenSSL-SSL / crypt32
- `src/cli/nanosvg_impl.cpp` — TU providing `NANOSVG_IMPLEMENTATION`
- `src/cli/commands/inspect.cpp` — `inspect` subcommand
- `src/cli/commands/project.cpp` — `project init` subcommand
- `src/cli/commands/project_tab.cpp` / `project_tab.hpp` — `project info / profile / aux` subcommands
- `src/cli/commands/plate.cpp` — `plate add / list / remove / rename`
- `src/cli/commands/object.cpp` — `object add / list / remove / set-filament / split-to-parts / merge-parts`
- `src/cli/commands/config.cpp` — `config set / unset / list`
- `src/cli/commands/mutation_runner.hpp` — `run_mutation<Mutator>` envelope (load → mutate → save → emit) with `MutationExceptionMap` per-callsite overrides
- `src/cli/extern/CLI11/CLI11.hpp` — vendored CLI11 v2.4.2 single-header (10,998 lines)

### New tests (`tests/cli/`)
- `tests/cli/CMakeLists.txt` — `cli_tests` Catch2 target linking `bambu_cli_core`
- `tests/cli/cli_tests_main.cpp` — Catch2 main
- `tests/cli/test_helpers.hpp` / `test_helpers.cpp` — common fixture helpers
- `tests/cli/archive_invariants.hpp` / `archive_invariants.cpp` — archive-level invariant assertions reused across e2e tests
- `tests/cli/e2e/` — 18 e2e suites: `test_inspect`, `test_project_init`, `test_project_info/profile/aux`, `test_plate_add/list/remove_rename`, `test_object_add/transforms/filament/remove_set_filament/merge/split`, `test_config`, `test_json_and_exit_codes`, `test_thumbnail_passthrough`
- `tests/cli/unit/` — 10 unit suites: `test_plate_stride`, `test_png_placeholder`, `test_project_ops_{plates,objects,split,merge,config}`, `test_project_{info,profile,aux}_ops`, `unit_helpers`
- `tests/cli/fixtures/gen_fixtures.cpp` — programmatic STL fixture generator
- `tests/cli/fixtures/local/` + `tests/cli/fixtures/stls/` — committed reference `.3mf` + STL set (cube / cylinder / cone / two_cubes / 000_01_test_*)
- `tests/cli/fixtures/.gitattributes`, `assembly_smoke.txt`, `cover_smoke.jpg/png` — aux fixtures
- `tests/cli/roundtrip/.gitkeep` — placeholder for the roundtrip subtree

### New docs
- `docs/cli/status.md` — milestone-by-milestone status with manual-gate checkboxes (M0..M10 + Phases A..F)
- `docs/cli/manual-test.md` — 383-line manual GUI smoke recipe
- `docs/cli/notes/2026-05-21-bbs-profile-storage.md` — investigation note on BBS profile storage (resolves Phase C profile_set divergence from Orca)
- `docs/superpowers/plans/2026-05-19-bambu-cli.md` — 4,835-line implementation plan (M0..M10 + post-M10 sibling-parity follow-ups + Phases A..F)
- `docs/superpowers/plans/phaseD-progress.log` — Phase D dispatch progress log
- `docs/superpowers/specs/2026-05-19-bambu-cli-restart-design.md` — design spec (load-mutate-save flow, guard checks, exit codes)

## Per-Feature Detail

### 1. CLI executable + core static library
- **Purpose:** Single-binary, GUI-free composer for BBS `.3mf` projects.
- **Entry point:** `src/cli/main.cpp:17` (`int main`); `src/cli/CMakeLists.txt:43,72` (library + exe targets).
- **Key files & functions:** `src/cli/main.cpp:24` (top-level `--json` flag callback), `src/cli/main.cpp:31` (no-op `--verbose`), `src/cli/main.cpp:33-37` (subcommand registration fan-out), `src/cli/CMakeLists.txt:26-41` (BAMBU_CLI_CORE_SOURCES list), `src/cli/CMakeLists.txt:80-98` (Windows DLL copy POST_BUILD).
- **Data flow:** argv → CLI11 parse → leaf subcommand callback → `run_mutation` (load → mutator → save) → `emit_ok`/`emit_error` → `std::exit(ExitCode)`.
- **CLI surface:** `bambu-cli [--json] [--verbose] <subcommand> ...`; subcommands `project | inspect | plate | object | config` (each with leaf verbs below).
- **Notable design choices:**
  - One static lib (`bambu_cli_core`) so the exe and `cli_tests` share the same TU set — no double-compilation, no drift.
  - Sibling pattern explicitly noted (`src/cli/CMakeLists.txt:25`): matches OrcaSlicer's `orca_cli_core`.
  - Link-time stubs (`stubs_for_libslic3r.cpp`) keep curl/SSL out of the CLI surface.
  - `--verbose` parsed but intentionally a no-op so scripts don't break (`main.cpp:27-31`).

### 2. project init (atomic clone-and-verify)
- **Purpose:** Produce a working `.3mf` from a template with both pre-save and post-save invariant guards run against it.
- **Entry point:** `src/cli/commands/project.cpp:36` (CLI callback).
- **Key files & functions:** `atomic_copy` (`src/cli/io.cpp:316`), `load_project` (`io.cpp:76`), `check_thumbnails_in_archive` (`invariant_guard.hpp:33`), `save_project` (`io.cpp:218`).
- **Data flow:** template path → atomic_copy → load_project → pre-save thumbnail check → save_project (which itself runs `run_guard` post-write) → atomic `.bak` swap.
- **CLI surface:** `bambu-cli project init <out> --template <ref>`.
- **Notable design choices:** Pre-save guard runs explicitly **before** the save because `save_project` regenerates placeholder thumbnails and would otherwise silently "fix" a corrupted template (`project.cpp:55-62`). Failures roll back the partial clone.

### 3. inspect (read-only)
- **Purpose:** Print plate / object / filament-slot counts for a project.
- **Entry point:** `src/cli/commands/inspect.cpp:30`.
- **Key files & functions:** `inspect.cpp:14` (`total_object_count`), `inspect.cpp:18` (`filament_slot_count` via `filament_settings_id`).
- **Data flow:** path → `load_project` → count plates/objects/filaments → `emit_ok` with `{plate_count, object_count, filament_count}` in JSON mode.
- **CLI surface:** `bambu-cli inspect <in> [--json]`.
- **Notable design choices:** Reads `filament_settings_id` length (matches what the GUI shows in the AMS panel).

### 4. plate add / list / remove / rename
- **Purpose:** Plate-level CRUD with name validation.
- **Entry point:** `src/cli/commands/plate.cpp:36` (`register_plate_subcommands`).
- **Key files & functions:** `add_plate` / `remove_plate` / `rename_plate` / `list_plate_names` declared in `project_ops.hpp:21-35`, implemented in `project_ops.cpp:32`, `:61`, `:85`, `:22`.
- **Data flow:** in.3mf → load → `add_plate(state, name)` (new `Slic3r::PlateData*` with monotonic `plate_index`) → `run_mutation` saves → exit. `remove_plate` rejects non-empty plates and compacts `plate_index` so reload doesn't fail.
- **CLI surface:** `plate add IN --name N [--output O]`, `plate list IN`, `plate remove IN --name N [--output O]`, `plate rename IN --from F --to T [--output O]`.
- **Notable design choices:** Duplicate-name → `DuplicateNameError` → exit 5; empty name → `std::invalid_argument` → exit 1. The compaction hotfix (commit `63794625f`) keeps `plate_index` dense after remove.

### 5. object add (STL ingest with placement)
- **Purpose:** Load an STL as one or more new `ModelObject`s on a named plate, with optional manual transform and filament assignment.
- **Entry point:** `src/cli/commands/object.cpp:70` (CLI callback).
- **Key files & functions:** `parse_triple` (`object.cpp:20`), `add_object_to_plate` (`project_ops.hpp:80`, `project_ops.cpp:166`), `plate_world_origin` (`project_ops.hpp:189`).
- **Data flow:** STL path → libslic3r `load_stl` → for k in 0..N-1 deep-clone via `add_object(const ModelObject&)` (distinct ObjectID) → `clear_instances` on the clone → stamp `vol->source.input_file` (Bug B fix) → either sqrt-grid auto-arrange or `T·R·S` from `ManualTransform` → off-bed AABB check (scale-only) → optional `extruder=N` stamp.
- **CLI surface:** `object add IN --plate P --stl PATH [--name N] [--filament K] [--count N] [--translate x,y[,z]] [--rotate rx,ry[,rz]] [--scale s | x,y[,z]] [--output O]`.
- **Notable design choices:**
  - Deep-clone-per-copy required because the BBS loader keys `obj_inst_map` on 3MF object_id and collapses duplicates (`project_ops.hpp:60-63`).
  - sqrt-grid layout, cell ≥ 20mm + margin (`project_ops.hpp:71-73`).
  - Off-bed check excludes rotation to match the GUI's own approximation.
  - Multi-plate stride applied via `plate_world_origin` (BBS `PartPlateList` formula: stride = bed × 1.2).
  - Filament range validation: 1..len(`filament_settings_id`), out of range → exit 1.
  - Bug B day-one stamp: every volume of every clone gets `source.input_file` so AMS color carries through GUI reload.

### 6. object list / object remove / object set-filament
- **Purpose:** Group-by-name object operations (multiple `ModelObject`s sharing a name are operated on together).
- **Entry point:** `src/cli/commands/object.cpp:106-172`.
- **Key files & functions:** `list_objects` (`project_ops.hpp:91`), `remove_object` (`project_ops.hpp:103`, `.cpp:466`), `set_object_filament` (`project_ops.hpp:123`, `.cpp:551`).
- **Data flow:** load → enumerate all `ModelObject`s with matching name → detach from every plate's `obj_inst_map` + `objects_and_instances` (using `inst->loaded_id`, not array index — Bug retrofit fix in commit `a8a321aff`) → delete objects → renumber obj-idx references → save. `set_object_filament` additionally walks all matched volumes; with `--part NAME`, only volumes whose `vol->name == part_name` get the stamp.
- **CLI surface:** `object list IN [--plate P]`, `object remove IN --name N [--output O]`, `object set-filament IN --name N --filament K [--part PART] [--output O]`.
- **Notable design choices:**
  - Group-by-name documented as deliberate (`project_ops.hpp:99-102`); `object add --count N` creates N share-name siblings that all five list/remove/set-filament treat as one logical unit.
  - `--part NAME` rename was a Phase E breaking change (`HEAD`, commit `5c2700028`).
  - Bug B retrofit guard re-runs on `set-filament` so legacy archives without `source.input_file` get fixed up before the extruder stamp.

### 7. object split-to-parts / merge-parts
- **Purpose:** Mesh-component split of a single-volume object, and named-volume merge into one volume.
- **Entry point:** `src/cli/commands/object.cpp:179` (split), `:206` (merge).
- **Key files & functions:** `split_object_to_parts` (`project_ops.hpp:183`, `.cpp:918`), `merge_object_parts` (`project_ops.hpp:222`, `.cpp:975`), `MergePartsParams` (`project_ops.hpp:196`).
- **Data flow:** **split**: first-match by name → require 1 volume of `MODEL_PART` type with > 1 connected component → produce one volume per component, return count. **merge**: 8-step deterministic validation (b: name exists, c: each part exists, d: --into not in use, e: filament range, f: all sources are MODEL_PART, g: meshes non-empty, h: filament agreement when --filament absent, i: per-volume config keys whitelisted to "extruder") → merge meshes into a single volume placed at the lowest-indexed source slot.
- **CLI surface:** `object split-to-parts IN --name N [--output O]`, `object merge-parts IN --name N --parts CSV --into NAME [--filament K] [--output O]`.
- **Notable design choices:**
  - First-match (not group-by-name) per `project_ops.hpp:171-174` — group-splitting is ambiguous (which clone?).
  - Both verbs override `std::invalid_argument` → exit 7 (`invalid_state`) instead of the default exit 1 (`object.cpp:191-194`, `:235-237`) because in this context it's invalid mesh state, not usage.
  - Merge step (a) "empty --parts" validated in the CLI callback **before** `run_mutation` so exit 1 is reported without a needless reload (`object.cpp:229-232`).

### 8. config set / unset / list
- **Purpose:** Edit project-level and per-object config keys; list either all or only-changed keys.
- **Entry point:** `src/cli/commands/config.cpp:36`.
- **Key files & functions:** `config_set` / `config_unset` / `config_list` in `project_ops.hpp:141-163`, implementations `project_ops.cpp:737`, `:795`. Phase A.6 expansion (commit `474478f9b`) makes `--object` operate on **all** name matches.
- **Data flow:** load → either project-level (`state.project_config`) or per-object (`obj->config`). Unknown key → `BadConfigError` (exit 4). On `set`, project-level keys are also routed into `different_settings_to_system[slot]` via classifier so the GUI's project-overrides panel shows them (M1 sibling-parity, commit `944f151ca`).
- **CLI surface:** `config set IN --key K --value V [--object O] [--output O]`, `config unset IN --key K [--object O] [--output O]`, `config list IN [--object O] [--changed-only]`.
- **Notable design choices:**
  - `--changed-only` uses `DynamicPrintConfig::diff` against `new_from_defaults_keys` — no SEH (Windows structured exception) so the test binary doesn't go through the crash handler.
  - Classifier-aware `different_settings_to_system` routing splits keys across filament-vs-printer-vs-process tabs to match the GUI's per-tab dirty-overlay (M1 hotfix, commit `7b376b4c5`).

### 9. project info / profile / aux
- **Purpose:** Edit `Model::model_info` and `Model::profile_info` fields; ingest/extract auxiliary archive files.
- **Entry point:** `src/cli/commands/project_tab.cpp:361` (`register_project_tab_subcommands`).
- **Key files & functions:** `register_info` (`project_tab.cpp:98`), `register_profile` (`:176`), `register_aux` (`:257`); ops `info_show`/`info_set`/`info_clear` (`project_tab_ops.hpp:38-47`, `project_tab_ops.cpp`), `profile_show`/`set`/`clear` (`:71-80`), `aux_list`/`add`/`remove`/`export` (`:118-129`), `sanitize_aux_name` (`:134`).
- **Data flow:** info: 6 fields (title/desc/license/copyright/cover/origin). profile: 5 fields (title/desc/cover writable; user_id/user_name read-only). aux: 4 folders (Pictures / Bom / AssemblyGuide / Others), `--name` overrides basename, `--force` overrides collision, sanitization strips path separators and rejects reserved names (CON/PRN/AUX/NUL/COM1-9/LPT1-9).
- **CLI surface:** `project info show|set|clear`, `project profile show|set|clear`, `project aux list|add|remove|export` (full flag matrix in `project_tab.cpp`).
- **Notable design choices:**
  - Cover image is PNG-signature-validated before embedding (`BadCoverImage` → exit 4).
  - Profile storage differs from Orca — Bambu's `store_bbs_3mf` reads `model.profile_info` directly, no mirroring into `metadata_items["ProfileTitle"]` (documented in `docs/cli/notes/2026-05-21-bbs-profile-storage.md` §4 and `project_tab_ops.hpp:73-76`).
  - `--field` accepts a CSV list parsed by the shared `split_fields` helper (`project_tab.cpp:78`).
  - `aux export` to an existing directory routes file to `<dir>/<name>`; missing parent → exit 7.

### 10. Runtime invariant guard + thumbnail subsystem
- **Purpose:** Catch corrupt saves at the source rather than letting them get re-opened in the GUI later.
- **Entry point:** `save_project` (`src/cli/io.cpp:269`) calls `run_guard` after `store_bbs_3mf` and rejects on failure.
- **Key files & functions:** `run_guard` (`invariant_guard.hpp:26`, `.cpp:1-200`), `check_thumbnails_in_archive` (`invariant_guard.hpp:33` — used by `project init` only), `make_placeholder_png_128` (`png_placeholder.hpp:10`), `rewrite_thumbnails` (`io.cpp:125`), `fill_placeholder_thumbnail` (`io.cpp:107`).
- **Data flow:** post-save → re-open the just-written `.3mf` as a zip → check (a) every `<Relationship>` Target in `_rels/.rels` and `*.rels` resolves to an archive entry; (b) `Metadata/plate_N.png` and `Metadata/plate_N_small.png` exist for every plate; (c) vector-typed config (coPoint*, coBools, coStrings) round-trips byte-for-byte. Thumbnail passthrough (`rewrite_thumbnails`): zip-copy original plate thumbnails from the source archive into the rewritten archive when present, otherwise inject a hand-rolled valid 128×128 RGBA-0xC0 PNG.
- **CLI surface:** Internal — no flag. Triggered for every save.
- **Notable design choices:**
  - `check_thumbnails_in_archive` opens via `mz_zip_reader_init_file` directly to tolerate Windows 8.3 short-name paths (e.g. `ILDARC~1`) — comment at `invariant_guard.hpp:31`.
  - The placeholder PNG is hand-rolled (IHDR + IDAT with `MZ_NO_COMPRESSION` + IEND) — no libpng dependency.
  - `.bak`-swap save pattern (`io.cpp:284-310`) ported from OrcaSlicer M11 (`src/cli/io.cpp:467-499`) to remove the rename-window where destination is absent. Restores from `.bak` if the middle rename fails.

### 11. JSON Shape A envelope + ExitCode enum
- **Purpose:** Stable machine-readable output and documented exit codes for scripting.
- **Entry point:** Every callback resolves `OutputMode` from the parse-time `--json` callback set in `main.cpp:24`; emits via `emit_ok` / `emit_error`.
- **Key files & functions:** `OutputMode` + `emit_ok` / `emit_error` / `emit_list_response` (`src/cli/json_output.hpp:13-65`, `.cpp`). `ExitCode` (`src/cli/exit_codes.hpp:7`, 0..9). Phase M4 migrated all hand-rolled JSON to `nlohmann::json` (commit `c692c0ff5`); `src/` was added to `bambu_cli_core` include path so `<nlohmann/json.hpp>` resolves (commit `e7188d901`).
- **Data flow:** Text mode: write line(s) to stdout, errors `"<code>: <message>"` to stderr. JSON mode: `{"status":"ok|error","code":...,"message":...,"data":...}` to stdout (or stderr for errors). `emit_list_response<Row, ToJson, ToLine>` factors the list-style envelope used by every list verb.
- **CLI surface:** `--json` global flag. Exit codes: 0 ok, 1 usage_error, 2 file_not_found, 3 parse_failure, 4 bad_config, 5 duplicate_name, 6 unknown_reference, 7 invalid_state, 8 invariant_violation, 9 placement_failure.
- **Notable design choices:** `emit_list_response` deliberately matches OrcaSlicer's template at `src/cli/output.hpp:38-59` (sibling-parity, `json_output.hpp:41-42`). `--json` uses `add_flag_callback` so the mode is set during parse — before subcommand callbacks fire (`main.cpp:23`).

### 12. Mutation runner envelope
- **Purpose:** One place where load → mutate → save → emit happens, with typed-exception dispatch.
- **Entry point:** `run_mutation<Mutator>` template (`src/cli/commands/mutation_runner.hpp:60`).
- **Key files & functions:** `MutationExceptionMap` (`mutation_runner.hpp:57`), built-in `dynamic_cast` chain (`mutation_runner.hpp:88-123`), used by every mutating verb's callback.
- **Data flow:** caller passes `in_path`, `out_path`, mutator lambda, optional override map → envelope loads, invokes mutator, catches `std::exception`, dispatches via per-callsite override map first, then built-in defaults (PlacementFailure→9, BadConfigError→4, DuplicateNameError→5, FileNotFoundError→2, InvariantViolation→8, `std::invalid_argument`→1, `std::out_of_range`→6, catch-all→3), then saves and emits success.
- **CLI surface:** Internal.
- **Notable design choices:** Override map keyed on `std::type_index` so `object split-to-parts` and `merge-parts` can remap `std::invalid_argument` from exit 1 to exit 7 without changing the global default (`object.cpp:191-194`, `:235-237`). Sibling-parity note: matches OrcaSlicer's `orca_cli::commands::run_mutation` (`mutation_runner.hpp:6-8`).

### 13. Test harness (e2e + unit + archive invariants)
- **Purpose:** 216 Catch2 test cases across 27 files protecting every mutation and JSON shape.
- **Entry point:** `cli_tests` target (`tests/cli/CMakeLists.txt`); `cli_tests_main.cpp`.
- **Key files & functions:** `test_helpers.hpp:1` (shared fixture loader), `archive_invariants.hpp:1` (archive-level assertions reused across e2e), `unit/unit_helpers.hpp:1` (project_ops in-process scaffolding), Phase A.1 restructure (commit `244f76d1b`) split into `e2e/` and `unit/`; Phase A.2 (commit `49dcebae9`) extracted `bambu_cli_core` so tests link the same objects the exe uses.
- **Data flow:** unit tests call `project_ops` directly against an in-memory `ProjectState`; e2e tests run the `bambu-cli` binary as a subprocess against committed fixture 3MFs and reload the result.
- **CLI surface:** N/A (test binary).
- **Notable design choices:** Same source files compiled once, into a static lib, then linked into both `bambu-cli` and `cli_tests` — eliminates the drift Phase A.2 noted in its commit message.

## Integration Points with Upstream Code

### Modified vs wrapped vs untouched

- **Modified (only 2 files, 1 line each):**
  - `src/CMakeLists.txt:28` — `add_subdirectory(cli)`.
  - `tests/CMakeLists.txt:38` — `add_subdirectory(cli)`.
- **Wrapped (called via header includes, not modified):**
  - `Slic3r::load_bbs_3mf` (`src/cli/io.cpp:87`) — full call with `LoadModel | LoadConfig | LoadAuxiliary`.
  - `Slic3r::store_bbs_3mf` (`io.cpp:250`) with `SaveStrategy::SplitModel`.
  - `Slic3r::Model`, `Slic3r::ModelObject`, `Slic3r::ModelInstance`, `Slic3r::ModelVolume`, `Slic3r::PlateData`, `Slic3r::DynamicPrintConfig`, `Slic3r::PrintConfigDef` — used by `project_ops.cpp` and `project_tab_ops.cpp`.
  - `Slic3r::TriangleMesh`, `Slic3r::load_stl` — used by `add_object_to_plate`.
  - `Slic3r::miniz_extension` + raw miniz — used by `io.cpp`, `invariant_guard.cpp`, `png_placeholder.cpp`.
  - `nlohmann::json` (vendored in upstream at `src/nlohmann/json.hpp`) — picked up via `target_include_directories(bambu_cli_core PUBLIC ${CMAKE_SOURCE_DIR}/src)` (`src/cli/CMakeLists.txt:50`).
- **Stubbed (link-time, not modified):**
  - `Slic3r::Http` (all member functions) and `BBL_Encrypt` — replaced by no-op bodies in `src/cli/stubs_for_libslic3r.cpp:19`, with a header comment forbidding compiling the originals into `bambu-cli`.
- **Untouched:** Everything else. No GUI code (`src/slic3r/`) is referenced; no upstream source file has any other edit.

### Shim layers / conditional compilation

- The PG2 fixup in `src/cli/io.cpp:47` (`rebuild_objects_and_instances`) is a **post-load** rebuild of `PlateData::objects_and_instances` from `obj_inst_map`. It works around a state the bbs_3mf reader doesn't populate but the writer needs. Not a monkey-patch — runs at load time on the in-memory state only.
- The G3 placeholder thumbnail + `rewrite_thumbnails` passthrough (`io.cpp:107-216`) re-opens the just-written archive to swap thumbnail entries — also a post-process, not a libslic3r patch.
- `MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE` properties on both `bambu_cli_core` and `bambu-cli` (`src/cli/CMakeLists.txt:67-68, :75-76`) work around an IMPORTED-target Debug/Release mapping issue noted in the user's build_environment memory.
- No `#ifdef`-gated upstream patches and no monkey-patching of upstream classes.

## Open Questions / Rough Edges

- `bambu-cli --verbose` is parsed but a no-op — wiring it through every register function was estimated at >30 LOC, deferred. Hidden from `--help` (`main.cpp:27-31`).
- `install(TARGETS bambu-cli)` is out of scope for v1 — the binary ships from `build/` only (`src/cli/CMakeLists.txt:100`).
- Thumbnail passthrough for plates whose `plate_index` was **compacted** after a remove may fall back to synthesis instead of zero-copy (`io.cpp:121-124`). Acceptable for Phase B scope but noted.
- Phase F status entry in `docs/cli/status.md` exists for the most recent cleanups; `--part NAME` was a Phase E breaking rename of the prior flag spelling (commit `5c2700028`).
- Two un-checked-in plan files visible in `git status`: `docs/superpowers/plans/2026-05-20-cli-audit-followups.md` and `docs/superpowers/plans/2026-05-21-cli-sibling-parity.md`.
- Per-milestone smoke-gate checkboxes in `docs/cli/status.md` are still `[ ]` for M1 through M10 — meaning none of the produced 3MFs have been signed off by opening in Bambu Studio yet (or the file has not been updated to reflect that).
- `tests/cli/roundtrip/.gitkeep` is a placeholder for the unimplemented round-trip suite.
- Two `system-reminder` SessionStart hooks observed during report generation suggest a TaskCreate workflow may be expected by the user's harness configuration — see `MEMORY.md` `feedback_subagent_progress_visibility.md` for milestone-dispatch visibility requirements (not relevant here, but flagged).

---

**Originally generated at:** `C:\Users\ILDARC~1\AppData\Local\Temp\bambu_studio_changes_report.md`
**Archived to repo:** 2026-05-23
