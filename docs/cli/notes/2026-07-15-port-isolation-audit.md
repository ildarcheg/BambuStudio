# Port isolation & quality audit — master vs upstream v02.07.01.62

Date: 2026-07-15
Scope: full audit of the CLI additions relative to the upstream GA tag
`v02.07.01.62` (the base of the current `master`). Three questions:
(1) how independent are our changes, (2) how good is the CLI code,
(3) how easy is the next upstream rebase.

Method: the diff `v02.07.01.62..master` was inventoried and every
modified upstream file read in full; three parallel review passes
covered (a) core io/ops code quality, (b) command layer + test suite,
(c) the libslic3r include/symbol/build coupling surface. Both HIGH
findings were independently re-verified against libslic3r sources
before inclusion. Doc claims (CLAUDE.md, docs/cli) were treated as
hypotheses and checked against the tree.

## Verdict

- **Independence: excellent.** Of everything the CLI needs, exactly
  **2 lines** of upstream files are modified: the `add_subdirectory(cli)`
  hooks at `src/CMakeLists.txt:28` and `tests/CMakeLists.txt:38`. All
  other upstream modifications are toolchain portability (CMake 4.x /
  Clang 21) that upstream would need even with no CLI present.
- **Upgradability: good.** High-churn upstream APIs are called from
  single chokepoints; most breakage modes are loud (compile/link
  errors). The 2.08→2.07 retarget already proved a rebase needs zero
  CLI source changes. Residual risks are the few silent failure modes
  ranked below.
- **Code quality: good, with two HIGH findings** — a behavioral bug in
  `plate arrange` rotation handling, and a save-path robustness gap
  where disk errors crash the process instead of producing an error
  envelope.

## 1. Independence (verified against the diff, not the docs)

Delta vs upstream: 134 added files + 24 modified files, totaling
39 insertions / 25 deletions in upstream-owned files.

| Category | Files | Needed by CLI? |
|---|---|---|
| `add_subdirectory(cli)` hooks (`src/CMakeLists.txt:28`, `tests/CMakeLists.txt:38`) | 2 (1 line each) | **Yes — the entire required footprint** |
| `cmake_minimum_required` bumps to 3.5 | 14 | No — CMake 4.x compat (`6243afa37`) |
| Freetype target fix, Clang flag guard, `CMAKE_POLICY_VERSION_MINIMUM`, `FindOpenVDB` bump | 4 | No — toolchain compat |
| deps switches (`ASSIMP_BUILD_ZLIB=OFF`, `wxBUILD_PRECOMP=OFF`), `wxMediaState→int` in 2 GUI headers | 4 | No — macOS/Clang 21 compat (`161ed17f8`) |
| `.gitignore` | +6 lines | Cosmetic |

Verified clean: no `#ifdef`s in upstream source, no libslic3r source
edits, no monkey-patching. Grep of the whole tree: no upstream file
references the CLI beyond the two hooks; no file outside
`src/cli`/`tests/cli` includes anything from them. The commit
structure (tree port → hooks → portability fixes) is mechanically
replayable.

**Guardrail gap:** `src/cli/CMakeLists.txt` exposes
`${CMAKE_SOURCE_DIR}/src` as a PUBLIC include dir, so a CLI file *can*
include GUI headers and it compiles. Today exactly one does,
deliberately (`stubs_for_libslic3r.cpp` → `slic3r/Utils/Http.hpp`).
Nothing prevents accidental GUI coupling from creeping in — see
recommendation R6.

## 2. Code-quality findings

### HIGH

- **H1. `plate arrange` applies rotation as an absolute reset —
  `src/cli/project_ops.cpp:1389`.** The code does
  `inst->set_rotation(Vec3d(0, 0, items[k].rotation))`, but
  libslic3r's contract is an *incremental* Z-delta composed onto the
  existing transform (`ModelInstance::apply_arrange_result`,
  `src/libslic3r/Model.cpp:4276`; `get_arrange_polygon` bakes the
  current transform into the hull and reports `rotation = 0`).
  Running `plate arrange` after `auto-orient` (or on any pre-rotated
  instance) silently zeroes X/Y/Z rotation — objects can flip back
  upright and overlap. Fix: use `apply_arrange_result` / compose the
  delta.
- **H2. Disk errors during save/load crash the process —
  `src/cli/io.cpp:223` (also `:208/:214/:252/:262/:271/:306`),
  `src/cli/commands/mutation_runner.hpp:69/:85`,
  `src/cli/main.cpp:50`, `src/cli/project_tab_ops.cpp:370`.**
  `save_project`/`load_project` run outside every try block, `main`
  has no top-level catch, and `io.cpp` uses throwing
  `boost::filesystem` overloads. A stale `.tmp.3mf` held open, a
  read-only target, a locked `aux export` destination — any of these
  throws `filesystem_error` straight to `std::terminate`: no JSON
  envelope, an OS abort code instead of a documented one.

### MEDIUM

- **M1.** Invariant-guard thumbnail check off-by-one
  (`src/cli/invariant_guard.cpp:99`): treats 0-based `plate_index` as
  the 1-based thumbnail number; on an n-plate project it checks
  `plate_1.png` twice and never checks the last plate's.
- **M2.** Contradictory `plate_index` conventions
  (`src/cli/project_ops.cpp:44-47` vs `:76-83`, `:249`): `add_plate`
  produces 1-based indices on an empty list while loader and
  `remove_plate` compaction are 0-based. Scenario: remove the only
  plate, `plate add`, `object add` — object is placed at plate-2's
  world origin while `center`/`arrange` treat the plate as at origin.
- **M3.** CLI11-level usage errors bypass the exit-code/JSON contract
  (`src/cli/main.cpp:29-50`): missing required flag exits 106 with
  plain-text usage instead of documented exit 1 + JSON envelope. No
  test covers this.
- **M4.** `aux add` idempotency check compares only file size
  (`src/cli/project_tab_ops.cpp:330-338`): re-adding a same-size,
  different-content file reports "added", exits 0, silently keeps
  stale bytes.
- **M5.** `rewrite_thumbnails` is the one genuine breach of the
  "all writes through libslic3r" architecture
  (`src/cli/io.cpp:125-216`): rebuilds the whole zip entry-by-entry
  with raw miniz to swap thumbnails. Post-hoc guarded, but the code
  most likely to silently diverge if upstream renames
  `Metadata/plate_N` entries. `mz_zip_reader_get_filename` returns
  unchecked at `:151/:181`.
- **M6.** Hand-rolled numeric parsing maps typos to wrong exit
  classes (`src/cli/apply_helpers.cpp:57-59`,
  `src/cli/commands/object.cpp:20-34`): a string-typed manifest axis
  exits 3 `parse_failure` instead of the guaranteed exit 1;
  `--translate 1e999,0` exits 6 `unknown_reference`; `10x,20`
  silently parses as 10.
- **M7.** No fsync before the rename swap, though the doc comment
  claims it (`src/cli/io.cpp:250-337`, `src/cli/io.hpp:30`): on power
  loss right after a "successful" save the original is already
  demoted to `.bak` while new data may not have hit disk.

### LOW

- Exit-code inconsistencies: non-empty `plate remove` → exit 6
  instead of 7 (`project_ops.cpp:71`); filament-slot-out-of-range is
  exit 1 in some verbs, 6 in others (`project_ops.cpp:399,571,1011`).
  (Resolution 2026-07-15: `plate remove` fixed to exit 7 via a new
  typed `InvalidStateError`. merge-parts' exit 6 for filament range is
  left as-is — it is deliberately pinned by unit + e2e tests
  ("step e", `test_object_merge.cpp:86`) as part of the M12 contract,
  and its `invalid_argument → 7` override means no existing exception
  type maps it to exit 1; changing it would break the published CLI
  contract for a LOW-severity nit.)
- Exception-override maps match exact `typeid`
  (`exception_dispatch.cpp:20`), so `std::runtime_error` subclasses
  (e.g. `Slic3r::RuntimeError`) escape the auto-orient exit-7 remap
  and surface as exit 3.
- The "destination never absent during swap" comment at `io.cpp:277`
  is false (two-rename window); `atomic_copy` (`io.cpp:316-337`)
  still uses remove-then-rename with no `.bak`.
- `obj_inst_map` holds mixed key/value domains after mutations
  (`project_ops.cpp:529-543`, `:329`) — latent; the store path
  consumes only `objects_and_instances`.
- `save_project(const ProjectState&)` mutates through raw pointers
  (`io.cpp:237`); `ProjectState`'s defaulted move-assign would leak
  `PlateData*` (`project_state.hpp:26`).
- Config-roundtrip guard skips keys the writer drops entirely
  (`invariant_guard.cpp:145-152`).
- Test hygiene: non-RAII temp cleanup leaks files on assertion
  failure; latent stdout/stderr pipe-drain deadlock in `spawn_cli`
  (`tests/cli/test_helpers.cpp:20-31`).
- `class ProjectState` forward-decl vs `struct` definition → MSVC
  C4099 (`commands/project_apply_internal.hpp:32`).
- `.gitignore` comment contains a mojibake character.
- CLAUDE.md says the tests hook is at `tests/CMakeLists.txt:39`;
  actual is `:38`.

### Verified sound (checked, no defect)

- M12 `ManifestFieldError`-before-overrides ordering is implemented
  (`exception_dispatch.cpp:16-17` precedes override lookup at `:20`)
  and pinned by dedicated e2e tests; the null-entry (unknown-op) path
  is also safe.
- Exe and test binary share one static lib (`bambu_cli_core`) with
  zero source-list drift.
- Stubs are safe on all current CLI paths: all 8 `LogSink.obj`
  call sites traced; no-ops cannot corrupt behavior; encryption is
  gated on a key the CLI never sets.
- `png_placeholder` is a correct minimal PNG encoder.
- `check_rels` leading-slash normalization matches the exporter.
- Test suite: ~447 cases, three tiers, randomized order, asserting
  exit code + output content + archive-level invariants.

### Test-coverage gaps

No direct unit tests of `exception_dispatch`; no coverage of
CLI11-level usage errors, aux-export failure paths, or `object list`
as a first-class verb.

## 3. Upgradability — coupling map and risk ranking

Chokepoints: `load_bbs_3mf` has **1** call site (`io.cpp:87`),
`store_bbs_3mf`/`StoreParams` **1** (`io.cpp:242-250`), the arrange
cluster **1 function** (`project_ops.cpp:1314-1370`), orient **2**
adjacent calls. Widest surface: Model-API usage in `project_ops.cpp`
(~28 touchpoints). The `commands/` layer is insulated — only
`inspect.cpp` includes a libslic3r header directly. GUI-layer
includes: exactly one (the stubs file). Symbol contract:
`Http` stubs are defined against the real header (drift → compile
error, loud); `BBL_Encrypt` is redeclared locally (drift → link
error, loud); `nanosvg_impl.cpp` duplicates symbols normally in
`libslic3r_gui` (upstream moving them → duplicate-symbol link error,
loud).

| # | Risk | Manifests as | Cost |
|---|---|---|---|
| 1 | `load_bbs_3mf` signature growth (14 positional args, BBS-private) | Compile error (loud) | Cheap: single site |
| 2 | `StoreParams` gaining fields the GUI sets but CLI leaves defaulted | **Silent** — output diverges from GUI-produced 3MFs | Highest severity |
| 3 | Stub contract drift | Loud (compile/link) in all current forms | Cheap; residual silent risk if libslic3r ever depends on an Http result |
| 4 | Thumbnail entry naming hardcoded in `rewrite_thumbnails` | **Silent** (and weakened by M1) | Medium |
| 5 | Model/Preset API breadth in `project_ops.cpp` | Compile errors (loud) | Most person-hours |

### Rebase playbook (validated in shape by the 2.08 port)

1. Cherry-pick in the proven order: tree port → the two hooks →
   portability fixes (drop any CMake bumps upstream has absorbed).
2. Build. Link errors → extend stubs; compile errors in `io.cpp` →
   `load_bbs_3mf`/`StoreParams` drift (single sites).
3. **Manually diff the GUI's `store_bbs_3mf` call site against
   `io.cpp:242-250`** — the only defense against risk #2 (silent).
4. Re-grep `src/cli` for `slic3r/` (GUI) includes — expected: exactly
   one, in the stubs file.
5. Run the full `cli_tests` suite + `project init`→`inspect` smoke;
   compare a CLI-saved 3MF against a GUI-saved one for entry naming
   (risk #4).

## 4. Recommendations (priority order)

- **R1.** Fix H1: use `apply_arrange_result` in `plate_arrange`.
- **R2.** Fix H2: top-level catch in `main` + `error_code`
  filesystem overloads in `io.cpp`.
- **R3.** Fix M1 (guard thumbnail off-by-one) and M2 (`plate_index`
  convention) together — the convention bug creates outputs the guard
  was supposed to catch.
- **R4.** Fix M3 (remap CLI11 parse errors to exit 1 + JSON envelope)
  and M6 (route `parse_triple`/`read_axis_object` through
  `ManifestFieldError`-style validation).
- **R5.** M7: add the fsync the save-path comment promises, or
  correct the comment.
- **R6.** Add a guardrail against GUI-include creep (narrow the
  PUBLIC include dir, or a test that greps).
- **R7.** Small fry: content-hash in `aux add` idempotency (M4),
  `.gitignore` encoding, CLAUDE.md `:38` correction, low-severity
  exit-code inconsistencies.

None of the findings undermine the port's core story: the isolation
design is genuinely as clean as the docs claim, the update path is
well-rehearsed and mostly loud-failing, and the two HIGH items are
ordinary implementation bugs in CLI-owned code — not coupling
problems.
