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
