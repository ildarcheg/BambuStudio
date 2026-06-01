# bambu-cli — session context

## What this project is
A standalone GUI-free CLI (`bambu-cli.exe`) that composes BBS `.3mf`
projects by driving libslic3r's load/store directly. Lives under
`src/cli/` + `tests/cli/`. Parallel to the sibling `orca-cli` initiative
in the OrcaSlicer fork at `C:\Users\ildarcheg\Documents\GitHub\OrcaSlicer`
(read-only reference; feature-parity is the explicit goal).

## Architecture in one paragraph
`bambu_cli_core` static lib holds all logic; both the `bambu-cli` exe and
the `cli_tests` Catch2 binary link it (no drift). All mutations go
through libslic3r's own `load_bbs_3mf` / `store_bbs_3mf` / `Model` /
`DynamicPrintConfig` / `PlateData` / `print_config_def` — no upstream
monkey-patching, no `#ifdef`s, only an `add_subdirectory(cli)` hook in
`src/CMakeLists.txt:28` and `tests/CMakeLists.txt:38`. Save path is a
`.bak`-swap atomic pattern (ported from OrcaSlicer M11) in
`src/cli/io.cpp:284-310` with a three-check post-write invariant guard
(rels target resolution / per-plate thumbnails / vector-config
roundtrip). Link surface is deliberately narrowed via
`src/cli/stubs_for_libslic3r.cpp` no-opping `Slic3r::Http`,
`BBL_Encrypt`, and `LogSink` (see "divergences" below). Layout ops
(`plate center` / `drop-to-bed` / `arrange` / `auto-orient` and
`object auto-orient`, M11) reuse libslic3r's own
`arrangement::arrange` + `update_arrange_params` +
`update_selected_items_inflation` + `get_shrink_bedpts`
(`src/libslic3r/Arrange.hpp`) and `orientation::orient`
(`src/libslic3r/Orient.hpp`); no new `libslic3r_gui` link surface.
The `project apply` batch-manifest verb (M12) dispatches a JSON
manifest of mutations against the same `project_ops` functions in a
single load/save cycle via a `HandlerRegistry` of
`HandlerEntry{ fn, overrides }` (one entry per op). Schema-shape
errors throw `ManifestFieldError` (a `std::invalid_argument`
subclass) which `exception_dispatch::dispatch` short-circuits to
exit 1 *before* the per-op override lookup, so manifest typos can't
be misclassified as `invalid_state` on verbs (split-to-parts,
merge-parts) whose overrides remap `invalid_argument` to exit 7.

## Sibling-fork divergences — LEGITIMATE, do not try to "fix"
- **Profile storage:** Bambu reads `model.profile_info` directly via
  `store_bbs_3mf` (no metadata-item mirror). Orca mirrors into
  `metadata_items["ProfileTile"]` (upstream typo). See
  `docs/cli/notes/2026-05-21-bbs-profile-storage.md` for the format-level
  reason.
- **Aux folder names:** Bambu's canonical layout is `Model Pictures` /
  `Profile Pictures` / `Bill of Materials` / `Assembly Guide` / `Others`
  (TitleCase + spaces, per `src/slic3r/GUI/Auxiliary.hpp:75` and
  `src/slic3r/GUI/Project.cpp:214-226`, verified against
  `tests/cli/fixtures/test_reference.3mf`). The CLI emits exactly these
  names. (Prior versions of this note incorrectly stated
  `Pictures` / `Bom` / `AssemblyGuide` — that was a CLI bug, since
  fixed; see `docs/cli/notes/2026-05-26-aux-folder-canonical-layout.md`.)
  Orca uses lowercase-hyphenated; we explicitly do not match Orca here.
- **CLI link surface:** `bambu_cli_core` requires
  `stubs_for_libslic3r.cpp` because Bambu's `libslic3r` itself references
  `LogSink` (verified 2026-05-22: removing stubs → `LNK1120`, 8
  unresolved externals from `libslic3r.lib(LogSink.obj)`) and pulls
  `Http` via `libslic3r_gui`. Orca's `libslic3r` has no `LogSink`
  reference, so Orca needs no stubs. Do NOT propose removing them here.

## Misattribution to watch for
`.bak`-swap atomic save originated in OrcaSlicer M11 — Bambu ported it
FROM Orca. The comment in `src/cli/io.cpp:284-310` is correct.

## Branch state (as of 2026-06-01)
- `master` HEAD: `31529bf6e` (merge of `worktree-cli-project-apply-m12`
  — M12 batch-manifest verb). **33 commits ahead of `origin/master`**;
  not pushed yet.
- M12 range: `029c51e85..7777688ab` (28 commits — 27 plan tasks + 1
  polish fix). M11 merge is `562fdd5ab` on the same line of history.
- `cross-project-convergence` branch retained at `90fbbbf7e` (the prior
  convergence HEAD).
- Convergence range: `65ecc50d9..90fbbbf7e` (Round 1: roundtrip tests,
  cover-image refcount, identify_id pin, project-init staging-copy
  TOCTOU. Round 2: `plate_world_origin` total-count fix, stubs
  investigation kept-with-rationale).
- Last `cli_tests` run: 447 cases / 4271 assertions, all green
  (post-M12, +116 cases / +239 assertions vs M11). To build/run the
  tests on macOS, reconfigure with
  `-DSLIC3R_BUILD_TESTS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5` (the
  default `BuildMac.sh` configures tests OFF); see
  `tests/cli/cli_tests_main.cpp` for the macOS temp-dir harness fix.
- Working tree is clean as of this CLAUDE.md write.

## File layout
- `src/cli/` — entry (`main.cpp`), `io.{hpp,cpp}`,
  `project_ops.{hpp,cpp}`, `project_tab_ops.{hpp,cpp}`,
  `invariant_guard.{hpp,cpp}`, `png_placeholder.{hpp,cpp}`,
  `json_output.{hpp,cpp}`, `exception_dispatch.{hpp,cpp}` (M12 — shared
  exception → exit-code dispatch with `ManifestFieldError` short-
  circuit), `apply_helpers.{hpp,cpp}` (M12 — `require_only` +
  `parse_filament` + `parse_transform` + `ConfigBatchError`),
  `exit_codes.hpp`, `exceptions.hpp` (includes `ManifestFieldError`),
  `stubs_for_libslic3r.cpp`, `nanosvg_impl.cpp`, `commands/` (one TU per
  top-level verb; `commands/project_apply.cpp` +
  `commands/project_apply_internal.hpp` host the M12
  `HandlerRegistry`), `extern/CLI11/CLI11.hpp` vendored.
- `tests/cli/` — `unit/`, `e2e/`, `roundtrip/` (populated by Round 1 —
  7 ported test files, `.gitkeep` removed); fixtures in
  `tests/cli/fixtures/local/` + `tests/cli/fixtures/stls/`.
- `docs/cli/` — `status.md`, `manual-test.md`, `notes/`.
- `docs/superpowers/specs/` and `plans/` — design docs + the
  4,835-line mega-plan (`2026-05-19-bambu-cli.md`). The two stray
  plan files mentioned in the audit (`2026-05-20-cli-audit-followups.md`,
  `2026-05-21-cli-sibling-parity.md`) are NOT currently in the tree;
  their work landed via the Phase A–F and convergence commits.

## Open items (carryover, not regressions)
- `docs/cli/status.md` manual GUI smoke gates still `[ ]` for M1–M10,
  Phases B/C/D, and **M12** — none of the produced 3MFs have been
  signed off by opening in Bambu Studio. M11 is the one signed-off
  milestone (2026-05-30).
- Master is **33 commits ahead of `origin/master`** — push when ready.
- `bambu-cli --verbose` is parsed but a no-op (intentional deferral —
  see Phase F.1 entry in `status.md`).
- No `install(TARGETS bambu-cli)` — ships from `build/` only
  (`src/cli/CMakeLists.txt:100`).
- Thumbnail passthrough for plates whose `plate_index` was compacted
  after a remove may fall back to synthesis instead of zero-copy
  (`src/cli/io.cpp:121-124`).

## Detailed references
- `docs/cli/notes/2026-05-22-bambu-cli-audit.md` — full Round-0 audit
  (every file, every feature, every integration point with file:line).
- `docs/cli/notes/2026-05-22-cross-project-convergence-log.md` — what
  each Round-1 and Round-2 convergence item did and why (including the
  stubs-removal investigation outcome).
- `docs/cli/notes/2026-05-21-bbs-profile-storage.md` — why Bambu's
  profile storage differs from Orca's.
- `docs/cli/status.md` — milestone-by-milestone status (M0..M12 +
  Phases A..F + convergence).
- `docs/cli/manual-test.md` — 383-line manual GUI smoke recipe.
- `docs/superpowers/specs/2026-05-31-project-apply-batch-design.md` —
  M12 spec (1001 lines).
- `docs/superpowers/plans/2026-05-31-project-apply-batch.md` —
  M12 27-task TDD plan (2699 lines).
