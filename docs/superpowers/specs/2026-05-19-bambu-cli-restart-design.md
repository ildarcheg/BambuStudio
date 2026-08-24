# bambu-cli restart — design spec

**Date:** 2026-05-19
**Status:** Design approved; ready for writing-plans.
**Predecessor:** `C:\Users\ildarcheg\Documents\GitHub\bambu-cli-spec.md` (prior v1.2 attempt, branch discarded). This doc supersedes it for scope and milestone strategy; that doc remains authoritative for libslic3r gotchas (G1–G9), Bug B root-cause analysis, and build environment specifics.

---

## 1. Goal & scope

**Goal:** Build `bambu-cli.exe` under `src/cli/` that composes `.3mf` project files for the Bambu Studio GUI to slice. CLI is a thin orchestrator over `libslic3r`. Acceptance bar: a `.3mf` produced by any CLI command opens cleanly in installed Bambu Studio with every change visible and no errors.

**In scope (v1):**
- Loads a pre-configured `.3mf` the user provides (printer/process/filament slots already set up in Bambu Studio).
- `project init <out.3mf> --template <ref.3mf>` — clone-and-verify a reference template. No preset overrides; the template's printer / process / filament palette is authoritative. Purpose: catch a bad reference template via the runtime invariant guard (§2) before the user's first real mutation command.
- Mutations:
  - `plate add` / `plate remove` / `plate rename` / `plate list`
  - `object add` (with `--filament`, `--translate`/`--rotate`/`--scale`, `--count`) / `object remove` / `object list` / `object set-filament`
  - `config set` / `config unset` / `config list` (per-object via `--object` or project-level)
  - `inspect`
- Output: a new `.3mf` (atomic write — temp file + runtime guard + rename) at either the input path (in-place) or `--output <out.3mf>`.

**Out of scope — explicit:**
- No `--printer` / `--process` / `--filament` preset overrides on `project init` (or anywhere). Sidesteps Bug A from prior spec §5.3 entirely. No `PresetBundle`. No direct-JSON preset loader.
- No slicing. Slicing belongs in BS.
- No hand-written 3MF XML. All writes go through `Slic3r::store_bbs_3mf`.
- No GUI / wx / OpenGL dependencies.

---

## 2. Architecture

**Clone-and-mutate, single flow:** every command does `load_project(in.3mf) → pure mutation → save_project(out.3mf) → runtime invariant guard → atomic rename`. No synthesize-from-scratch path. `project init` is the degenerate case (mutation is a no-op).

**`ProjectState`:**
```cpp
struct ProjectState {
    Slic3r::Model model;
    Slic3r::DynamicPrintConfig project_config;
    std::vector<PlateData*> plate_data;
};
```

**File layout:**
- `src/cli/main.cpp` — CLI11 wiring + subcommand dispatch.
- `src/cli/io.{hpp,cpp}` — load/save (`Slic3r::load_bbs_3mf` / `Slic3r::store_bbs_3mf`), atomic temp-and-rename helper, 128×128 gray RGBA `0xC0` thumbnail placeholder.
- `src/cli/invariant_guard.{hpp,cpp}` — post-save runtime guard (zip-reopens the temp file, runs the three checks below; fails with exit 8 before rename).
- `src/cli/project_ops.{hpp,cpp}` — pure mutations on `ProjectState`. No I/O.
- `src/cli/commands/{plate,object,config,inspect,project}.cpp` — one file per subcommand group. Parses args → calls project_ops → emits output via json_output.
- `src/cli/json_output.{hpp,cpp}` — JSON Shape A formatter.
- `src/cli/CMakeLists.txt` — bakes in `nanosvg_impl.cpp` + `stubs_for_libslic3r.cpp` + `MAP_IMPORTED_CONFIG_RELWITHDEBINFO=RELEASE` + `MAP_IMPORTED_CONFIG_MINSIZEREL=RELEASE` + POST_BUILD DLL copies (per `build_environment` memory).
- `tests/cli/` — Catch2 tests + archive-level XML invariants.
- `tests/cli/fixtures/stls/` — committed minimal binary STLs (non-zero unit normals per G7).
- `tests/cli/fixtures/reference.3mf` — committed reference 3MF, generated once in BS.

**Load/save invariants (locked):**
- `LoadStrategy::LoadModel | LoadConfig` always (G1).
- `SaveStrategy::SplitModel` explicit.
- Rebuild `PlateData::objects_and_instances` from `obj_inst_map ↔ ModelInstance::loaded_id` after load (G2).
- Per-plate 128×128 gray RGBA `0xC0` placeholder thumbnail before save (G3 — never 1×1 transparent).
- Reference 3MF never modified in place; atomic write to `<out>.tmp.3mf`, then runtime guard, then rename on success.

**Runtime invariant guard (every save, including `project init`):**
After every `Slic3r::store_bbs_3mf` completes on `<out>.tmp.3mf` — but **before** the rename — re-open the produced file as a zip and run three checks:

- **(a) Relationship Target resolution.** Every `<Relationship>` Target declared in `_rels/.rels` and any `*.rels` file resolves to an entry that exists in the archive. (Catches the G3-class dangling-thumbnail-relationship crash from a different angle.)
- **(b) Per-plate thumbnail existence.** For every plate `N` referenced by the project, both `Metadata/plate_N.png` and `Metadata/plate_N_small.png` exist in the archive.
- **(c) Vector-typed config round-trip.** Each vector-typed config value in `project_config` and per-object configs (`coPoint*`, `coPoints*`, vector-bool, vector-string-group) re-deserializes from the saved string to a value equal to the in-memory value. This is the type-aware joiner regression catch — if a future preset-application path is ever re-added with a wrong delimiter (Bug A), this check fires before the file ever lands on disk.

On any failure: delete `<out>.tmp.3mf`, do NOT rename over `<in.3mf>` or `<out.3mf>`, exit 8 (`invariant_violation`) with the specific failure (which check, which entry/key) in the error message. The guard runs in the CLI binary itself — not just in tests — so a bad save never lands on disk for the user.

The guard is a third defense in addition to Layer 1 (Catch2 archive-level invariants per milestone, §5) and Layer 2 (manual GUI verification per milestone, §5).

---

## 3. Command surface (v1)

```
bambu-cli [--json] [--verbose]

  project init  <out.3mf> --template <ref.3mf>

  plate  add    <in.3mf> --name <plate> [--output <out.3mf>]
  plate  remove <in.3mf> --name <plate> [--output <out.3mf>]
  plate  rename <in.3mf> --from <old> --to <new> [--output <out.3mf>]
  plate  list   <in.3mf>

  object add    <in.3mf> --plate <plate> --stl <stl>
                         [--count N] [--name NAME]
                         [--filament N]
                         [--translate x,y[,z]]
                         [--rotate x,y[,z]]
                         [--scale s | x,y[,z]]
                         [--output <out.3mf>]
  object remove <in.3mf> --name <obj> [--output <out.3mf>]
  object list   <in.3mf> [--plate <plate>]
  object set-filament <in.3mf> --name <obj> --filament N [--output <out.3mf>]

  config set    <in.3mf> [--object <obj>] --key K --value V [--output <out.3mf>]
  config unset  <in.3mf> [--object <obj>] --key K [--output <out.3mf>]
  config list   <in.3mf> [--object <obj>] [--changed-only]

  inspect       <in.3mf>
```

**`project init` semantics:**
- Atomic-copies `<ref.3mf>` to `<out>.tmp.3mf` (filesystem copy, not a zip rebuild).
- Runs the full pipeline: load (`LoadModel | LoadConfig`) → no mutation → save (`SplitModel`, per-plate thumbnail placeholder) → runtime invariant guard (§2) → rename on success.
- No `--printer` / `--process` / `--filament` flags. The template's printer / process / filament palette remains authoritative.
- Failure modes: missing template → exit 2; load failure → exit 3; guard failure → exit 8; rename failure → exit 7.

**Hard rules (carried from prior spec):**
- `--stl` for STL paths, **never `--file`** (G9 — CLI11 v2.4.2 + MSVC `/GS` crash with `--file` + positional `file`).
- Positional `<in.3mf>` first, then long flags.
- `--filament N` is 1-based, validated against the loaded project's filament slot count. Out-of-range → exit 1.

**Output model (departure from prior spec):**
- `--output <out.3mf>` omitted → mutate `<in.3mf>` in place via atomic temp-write + guard + rename.
- `--output <out.3mf>` supplied → write to `<out.3mf>` and leave `<in.3mf>` untouched.
- Atomic via `<out>.tmp.3mf` → runtime guard → rename on success. On any failure (write or guard), the temp file is removed and the original is untouched.

**Arrangement semantics on `object add`:**
- No transform flags → auto-arrange on the target plate, with deterministic grid fallback (G5 — keep the fallback even when the libslic3r arranger appears to work).
- Any of `--translate` / `--rotate` / `--scale` → auto-arrange skipped for that call (per-call, not per-object). `--count N --translate` stacks N copies at the same point (explicit opt-in).
- After applying T·R·S, world-space AABB checked against plate's printable area. Off-bed → exit 9 (`placement_failure`) with object name, post-transform bbox, plate name, bed dimensions.

**Exit codes:**

| Code | Name |
|---|---|
| 0 | `ok` |
| 1 | `usage_error` |
| 2 | `file_not_found` |
| 3 | `parse_failure` |
| 4 | `bad_config` |
| 5 | `duplicate_name` |
| 6 | `unknown_reference` |
| 7 | `invalid_state` |
| 8 | `invariant_violation` |
| 9 | `placement_failure` |

Notes on the rework vs prior spec:
- `7` was `io_error`; renamed to `invalid_state` and broadened to cover post-load schema problems, rename failures, and any other "we got into a state we can't recover from" path.
- `8` was reserved (`preset_incompatible`) and unused; now consumed by the runtime invariant guard (§2).
- `9` was `arrange_failure`; renamed to `placement_failure` for symmetry with `invariant_violation`.
- `4` and `5` swapped: `bad_config` now precedes `duplicate_name`, ordering them roughly by "how early the check fires" (config validation typically before name-collision checks).

**JSON output (`--json`) — Shape A:**
```
// success
{"status": "ok", "code": "<machine_code>", "message": "<human>", "data": <optional>}
// error
{"status": "error", "code": "<machine_code>", "message": "<msg>"}
```

`data` is omitted on success when empty. `status` is the enum (`ok` | `error`); leaves room for future `warning` / `partial` without breaking the contract.

---

## 4. Critical fixes baked in from day one

**Bug B fix — source attribution on every new `ModelVolume`.** After every `obj->add_volume(...)` in `project_ops::add_object_to_plate`:
```cpp
ModelVolume* vol = obj->add_volume(Slic3r::TriangleMesh(mesh));
if (vol) {
    vol->source.input_file = stl_path;
    vol->source.object_idx = 0;
    vol->source.volume_idx = 0;
}
```
Without this stamp, `<part>` blocks in `Metadata/model_settings.config` lack `source_file`. The combination of `extruder = N` + missing source attribution makes BS silently drop the new object from its renderer (root cause in prior spec §5.4). The stamp makes the first save's output equivalent to a load → save round-trip.

**Bug A — not applicable in v1.** No preset application surface. If preset overrides are ever re-added, the type-aware `separator_for_type(const ConfigOptionDef*)` helper from prior spec §5.3 is the required day-one fix. The runtime guard's check (c) in §2 would also catch a future Bug-A-class regression before the file lands on disk.

**Selected libslic3r gotchas applied at design time:**
- G1: every load uses `LoadModel | LoadConfig`.
- G2: rebuild `PlateData::objects_and_instances` after load.
- G3: every plate gets 128×128 gray RGBA `0xC0` thumbnail placeholder before save; the runtime guard's check (b) double-verifies this.
- G4: any "patch a default value" save-time logic gated on `was_loaded_from_disk == false`. With load-only flow that is never true in v1; the gate is documented in `io.cpp` so a future preset path doesn't reintroduce the regression.
- G5: deterministic grid fallback in `project_ops::auto_arrange` even when the libslic3r arranger appears to work.
- G6: `DynamicPrintConfig::diff` against `new_from_defaults_keys` for `config list --changed-only`. No SEH, no `__try` / `__except`.
- G7: committed STL fixtures have non-zero unit normals.
- G8: ASCII-only `TEST_CASE` names.
- G9: `--stl` not `--file`.

---

## 5. Test strategy

Two automated/manual layers (Layer 1, Layer 2) plus one runtime layer (the guard from §2, which lives in the CLI binary, not in tests).

**Layer 1 — Catch2 + archive-level XML invariants.** Each command's tests run the CLI binary against a reference 3MF (committed-minimal or local-realistic, see fixtures below), then **unzip the produced 3MF** and assert structural invariants directly:
- `printable_area` has 4 points (Bug-A-class corruption catch).
- Every `<part>` block in `Metadata/model_settings.config` carries `source_file` (Bug-B-class drop catch).
- Plate count, object count, per-object `extruder` value, per-object config overrides match expectations.
- Thumbnail PNGs exist and are 128×128.

Invocation: `cli_tests.exe --order rand --warn NoAssertions`. CMake injects binary path via `$<TARGET_FILE:bambu-cli>` generator expression. `boost::process::child` spawns the CLI and captures stdout / stderr / exit code.

**Layer 2 — Manual GUI verification.** At the end of every milestone, the user opens the produced 3MF in installed Bambu Studio and confirms:
- No error dialogs, no crash.
- Expected change is visible (new plate present; new object renders on its plate; per-object config visible in Object Manipulation panel).

**Milestone gating rule (hard):** A milestone is NOT complete until **both** Layer 1 (green Catch2) AND Layer 2 (user signed off after opening the 3MF) pass. Layer-1-green but Layer-2-fail is exactly how Bug B shipped in the prior attempt. The runtime guard (§2) is a third defense; passing the guard is necessary but not sufficient — the milestone still gates on Layer 1 + Layer 2.

### Test fixtures (authoritative)

Two fixture layers — committed minimal for CI portability, local realistic for full dev + manual smoke.

**CI / committed minimal layer** (in-tree):
- `tests/cli/fixtures/stls/` — 2-3 committed minimal binary STLs (cube, torus, etc.) with non-zero unit normals (G7).
- `tests/cli/fixtures/reference.3mf` — small reference 3MF (1 plate, 4 filament slots with distinct colors, no objects) generated once in BS and committed. 4 slots gives M5 (`object add --filament`) room to test values 1..4 plus out-of-range (5+) for the negative test.

**Local realistic layer** (canonical for dev + manual smoke; do NOT commit):
- **Canonical reference 3MF** — used by every `project init --template <ref>` invocation in `docs/cli/manual-test.md` and by the full-coverage Catch2 cases:
  - Path: `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\temp_project_for_bambu_studio.3mf`
  - Authored in Bambu Studio; printer / process / filament slots already configured.
  - **Never modify in place** — every test copies it to `$TEMP` first.
- **Canonical STL fixtures** for `object add` tests and manual smoke recipes, all in `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\`:
  - `000_01_test_cylinder.stl`
  - `000_01_test_cone.stl`
  - `000_01_test_bambu_cube.stl`
  - `000_01_test_cube.stl`
- **Do not reference** the sister file `temp_project_for_orca_slicer.3mf` in the same directory — that's for a different project and must not be touched by bambu-cli.

**CMake cache vars** (wired in M0):
- `BAMBU_CLI_REFERENCE_3MF` — default `C:/Users/ildarcheg/Documents/GitHub/slicer_tamplates/temp_project_for_bambu_studio.3mf`.
- `BAMBU_CLI_STL_DIR` — default `C:/Users/ildarcheg/Documents/GitHub/slicer_tamplates`.
- If either path is missing on the host, tests that depend on them use `Catch2::SUCCEED("skipped: <var> not set or path missing")` so CI runners without local fixtures still pass green. CI coverage in that case falls back to the committed in-tree minimal layer.

**Manual-test recipe (`docs/cli/manual-test.md`, produced in M10):**
Must invoke `bambu-cli` with these exact local paths (the canonical reference 3MF and the four canonical STL fixtures above).

---

## 6. Milestone breakdown (subagent-driven execution)

Each milestone is one subagent invocation. The subagent receives a self-contained brief; when it returns, the main thread prepares the manual-verification recipe and asks the user to open the produced 3MF in BS. **No next subagent spawns until the user signs off on the previous milestone.**

| # | Subagent deliverable | Layer-1 invariants (archive-level) | Manual verification (Layer 2) |
|---|---|---|---|
| **M0** | Build setup + fixtures. Lands `src/cli/CMakeLists.txt` with workarounds from `build_environment` memory (`nanosvg_impl.cpp`, `stubs_for_libslic3r.cpp`, `MAP_IMPORTED_CONFIG_*`, POST_BUILD DLL copies). Commits CI minimal fixtures: 2-3 minimal binary STLs (G7 non-zero unit normals) + reference 3MF (1 plate, 4 filament slots with distinct colors, no objects) generated in BS. **Wires CMake cache vars `BAMBU_CLI_REFERENCE_3MF` and `BAMBU_CLI_STL_DIR` with defaults pointing at the local-realistic fixtures (§5).** No CLI code yet; just `bambu-cli` target compiles and runs `--help`. | `bambu-cli.exe` exists and prints help. | N/A (no 3MF produced). |
| **M1** | `project init <out.3mf> --template <ref.3mf>` + runtime invariant guard. Atomic-copies `<ref.3mf>` to `<out>.tmp.3mf`, then runs the full pipeline: load (`LoadModel | LoadConfig`, G1+G2) → no mutation → save (`SplitModel`, per-plate 128×128 gray placeholder thumbnail, G3) → **runtime invariant guard** checks (a) `.rels` Target resolution, (b) per-plate `plate_N.png` + `plate_N_small.png` existence, (c) vector-typed config round-trip. Guard failure → delete temp, exit 8. Success → rename to `<out.3mf>`. Establishes `io.{hpp,cpp}`, `invariant_guard.{hpp,cpp}`, `ProjectState`, `json_output.{hpp,cpp}`, `commands/project.cpp`. | E2E: `bambu-cli project init out.3mf --template <committed reference.3mf>` exits 0; output passes all three guard checks; output 3MF parses with a second `load_bbs_3mf` round-trip producing structurally-equal state. Negative test: a hand-corrupted template (e.g. `plate_1_small.png` removed) → exit 8 with the specific failure named in the message. | BS: open the produced clone of the canonical reference 3MF — opens cleanly, no error dialogs, looks indistinguishable from the original. |
| **M2** | Skeleton: `bambu-cli inspect <in.3mf>` loads a 3MF, prints plate count + object count + filament slot count, exits 0. Builds on M1's foundational layers; no new infrastructure beyond `commands/inspect.cpp`. | `bambu-cli inspect reference.3mf` prints expected counts. JSON Shape A. | Output matches what BS shows for the same file. |
| **M3** | `plate add` + `plate list`. Atomic-write + temp-rename. Per-plate 128×128 gray RGBA `0xC0` thumbnail placeholder. Runtime guard runs on the saved output (regression net). | After `plate add`: archive has N+1 plate dirs; new plate's thumbnail PNG exists and is 128×128; `printable_area` has 4 points. `plate list` lists all plates including new one. | BS: new plate appears in plate selector; switching to it shows correct bed dimensions; no error dialogs. |
| **M4** | `object add` (no `--filament`, no transforms) + `object list`. Auto-arrange with deterministic grid fallback (G5). Source attribution stamp on every `add_volume` (day-one Bug B fix). | Archive contains new `<part>` on target plate; `<part>` carries `source_file` referencing the STL path; instance positioned within plate AABB. | BS: new object visible on correct plate, positioned on bed, renders normally. |
| **M5** | `object add --filament N`. Validates N ∈ [1, slot_count]; out-of-range → exit 1. **The Bug B regression milestone** — explicit test for `<part>` having both `extruder = N` AND `source_file` in the same write. | `<part>` has both `extruder = N` and `source_file`. Negative test: invalid N → exit 1 (`usage_error`). | BS: new object visible; AMS slot N's color visible on the part; no silent drop. |
| **M6** | `object add --translate / --rotate / --scale / --count`. Per-call auto-arrange skip. Off-bed → exit 9 (`placement_failure`). | Per-axis transforms applied (instance matrix in archive matches expected T·R·S); off-bed → exit 9 without writing output; `--count N --translate` produces N instances at same point. | BS: object at expected position/orientation/scale; stacked copies visible. |
| **M7** | `config set` / `config unset` / `config list` for both per-object (`--object`) and project-level. `--changed-only` uses `DynamicPrintConfig::diff` against `new_from_defaults_keys` (G6, no SEH). | `Metadata/model_settings.config` carries per-object `<config>` block with set keys; `config list --changed-only` matches `diff` output. | BS: per-object override visible in Object Manipulation panel; project-level config matches in Process panel. |
| **M8** | `plate remove` + `plate rename`. | After remove: plate count decreases; orphan check (no objects reference removed plate). After rename: `name` updated; cross-references intact. | BS: removed plate gone; renamed plate's new name appears in selector. |
| **M9** | `object remove` + `object set-filament`. | Object gone from archive after remove. After set-filament: existing `<part>`'s `extruder` updated; `source_file` preserved (Bug B regression test on retrofit path). | BS: removed object gone; retrofit-filament object visible with new AMS slot color. |
| **M10** | Polish: `--json` output uniformity across all subcommands, error-code coverage matrix, `inspect` enriched output. Final manual-test recipe in `docs/cli/manual-test.md` covering the full v1 workflow end-to-end (using the canonical local-realistic fixtures from §5). | All commands return Shape A JSON under `--json`; all documented exit codes covered by at least one test. | Full end-to-end workflow per `docs/cli/manual-test.md`: `project init` from the canonical template, run M3..M9 commands in sequence, open final 3MF in BS — every change visible, no errors, slicing in BS succeeds. |

**Subagent brief contract (what each milestone's subagent receives):**
- Working directory and current branch.
- Files to read first: this design doc + `C:\Users\ildarcheg\Documents\GitHub\bambu-cli-spec.md` + `build_environment` memory.
- The exact tests to write (archive-level invariants for that milestone).
- The exact build/test commands (per `build_environment` memory).
- Exit condition: "Layer 1 green; return a path to the produced 3MF for manual verification."

**Subagent is NOT asked to:**
- Decide scope or skip tests.
- Spawn further subagents.
- Mark the milestone complete without Layer 1 green.

---

## 7. Build environment

Locked from `build_environment` memory and prior spec §6. **No design changes; subagent briefs reference the memory file directly.** Key items:

- PATH priming: VS 2019 CMake 3.20 ahead of system PATH.
- Env vars: `CMAKE_POLICY_VERSION_MINIMUM=3.5`, `CMAKE_GENERATOR=Visual Studio 16 2019`, `CMAKE_GENERATOR_PLATFORM=x64`.
- Deps at `C:\Users\ildarcheg\Documents\GitHub\BambuStudio_dep` (FFMPEG `.pc` patch already applied; do NOT rebuild deps).
- Build in **Release** (not RelWithDebInfo), `--parallel 2`.
- Source patch on `src/slic3r/GUI/AMSMaterialsSetting.cpp` is irrelevant — `bambu-cli` only links `libslic3r`, not `libslic3r_gui`. Patch is still in working tree from prior attempt; subagents shouldn't touch it.
- `--stl` not `--file` (G9).

Standard configure + build:
```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Users/ildarcheg/Documents/GitHub/BambuStudio_dep/usr/local" -DSLIC3R_BUILD_TESTS=ON
cmake --build build --target bambu-cli cli_tests --config Release --parallel 2
build\tests\cli\Release\cli_tests.exe --order rand --warn NoAssertions
```

---

## 8. Decisions log (this spec vs prior spec)

| Decision | Prior spec | This spec | Reason |
|---|---|---|---|
| `project init` (template clone, no preset overrides) | Part of the preset-override flow | **In scope as standalone command** | Lets the user catch a bad reference template via the runtime invariant guard before the first real mutation. No preset surface, so Bug A still cannot occur. |
| Preset overrides (`--printer` / `--process` / `--filament`) on `project init` | In scope (v1.2) | **Out of scope** | Sidesteps Bug A entirely. User confirmed workflow always starts from a pre-configured BS-authored 3MF. |
| Runtime invariant guard (post-save, in CLI binary) | Absent | **Added** | Third defense beyond archive-level Catch2 (Layer 1) and manual GUI (Layer 2). Catches dangling relationships, missing thumbnails, and joiner regressions before output lands on disk. |
| Exit code mapping | `4 duplicate_name`, `5 unknown_reference`, `6 bad_config`, `7 io_error`, `8 preset_incompatible` (reserved), `9 arrange_failure` | **`4 bad_config`, `5 duplicate_name`, `6 unknown_reference`, `7 invalid_state`, `8 invariant_violation`, `9 placement_failure`** | Frees code 8 for the runtime guard; broadens `io_error` → `invalid_state` to cover post-load schema problems; renames `arrange_failure` → `placement_failure` for symmetry; swaps 4/5 to order by check-fires-earliest. |
| `--output <out.3mf>` flag | Absent (in-place only) | **Added** | Atomic-rename safety + leaves input untouched for debugging / scripting. |
| `project new` synthesize path | Deprecated, kept with stderr warning | **Removed entirely** | Nothing to deprecate without preset surface. |
| Multi-filament `project init` (`--filament A --filament B ...`) | In scope | **Out of scope** | Filament slots come from the pre-configured 3MF. |
| Direct-JSON preset loader | In scope | **Removed** | No preset application path. |
| Test strategy | 4 layers (unit, round-trip, e2e, installed-GUI smoke) | **2 layers (archive-level invariants, manual GUI) + runtime guard in the binary** | Archive-level invariants catch Bug-A/B-class regressions programmatically; manual GUI is the gate; the runtime guard adds a third defense that fires before bad files ever reach disk. Unit + round-trip + e2e value largely subsumed by archive-level + subagent-internal correctness. |
| Source attribution stamp on `add_volume` | Day-one requirement | **Day-one requirement (same)** | Bug B fix; non-negotiable. |
| Type-aware preset delimiter (Bug A fix) | Day-one requirement | **N/A** (no preset surface) | Bug A surface removed. If ever re-added, the runtime guard's check (c) catches a regression of the same shape. |
| Execution model | Implicit linear | **Subagent-per-milestone with manual verification gate** | User requested step-by-step subagent execution to preserve main-thread context; per-milestone manual gate catches Layer-2 regressions early. |
| Canonical fixtures | Unspecified | **`C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\` `temp_project_for_bambu_studio.3mf` + four `000_01_test_*.stl` files** | Local realistic layer for dev + manual smoke; CMake cache vars `BAMBU_CLI_REFERENCE_3MF` / `BAMBU_CLI_STL_DIR` with `SUCCEED+skip` fallback for CI. Sister file `temp_project_for_orca_slicer.3mf` explicitly out of scope. |

---

## 9. One-line summary

Build a `.3mf`-composing CLI under `src/cli/`, scoped to `project init` (template clone) + load → mutate → save → runtime invariant guard, no preset application. Apply Bug B's day-one source-attribution fix. Execute as 11 subagent-driven milestones (M0–M10), each gated on archive-level Catch2 invariants and user manual verification in Bambu Studio.
