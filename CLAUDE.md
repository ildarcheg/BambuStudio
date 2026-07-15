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
`src/CMakeLists.txt:28` and `tests/CMakeLists.txt:39`. Save path is a
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

## Branch state (as of 2026-07-14)
- `master` = tip of `port-cli-v2.07.01` = upstream tag `v02.07.01.62`
  (`42d319c66`) + the port commits (tree port `264000a80`, hooks
  `6b76fb2c1`, CMake portability fixes `6243afa37`) plus this docs
  commit — `git log v02.07.01.62..master` is authoritative (do not
  hardcode a HEAD SHA here; it will drift). **Zero changes were needed
  to `src/cli` or `tests/cli` source themselves** — `load_bbs_3mf` /
  `StoreParams` / `Arrange` / `Orient` APIs are unchanged between the
  old base and `v02.07.01.62`, and the stubs in
  `stubs_for_libslic3r.cpp` remain both necessary and sufficient.
- **Retarget story:** the port was first done against `v02.08.00.50`
  (2026-07-14, same day) and then re-targeted, because GitHub marks
  `v02.08.00.50` and `v02.08.01.55` as prerelease — **`v02.07.01.62`
  is the latest public GA release** (prerelease=false, published
  2026-06-16). Policy: `master` tracks the latest GA release, not the
  latest tag. The completed 2.08 port is parked, not discarded — see
  below.
- `port-cli-v2.08` branch (tip `2e8ac75e6`) retains the full 2.08-based
  port (built and test-verified before the retarget) for when the
  2.08 line goes GA. Its own build dir (`build_v208`) and deps
  (`BambuStudio_dep_v208`) are untouched by this retarget.
- `archive/v2.7-cli` tag (`6cb539d2f`) preserves the entire pre-port
  2.7-era lineage, including all M0–M12 + Phase A–G + convergence
  history and the port spec/plan docs. It is `201` commits past
  `v02.07.00.55` (`git rev-list --count v02.07.00.55..archive/v2.7-cli`).
  Two upstream features present in that lineage do not exist on either
  the 2.07 or 2.08 lines and were not reintroduced by any port: the
  `alternate_extra_wall` print feature and the camera-fullscreen GUI
  feature.
- Not pushed to `origin` yet — `origin/master` still points at the old
  (pre-port) history.
- Last `cli_tests` run: **447 cases / 4271 assertions, all green** on
  the `v02.07.01.62`-ported tree (3 runs, incl. one randomized test
  order) — identical counts to the pre-port 2.7-era baseline (post-M12)
  and to the parked 2.08 port. E2E smoke: `project init` from
  `tests/cli/fixtures/test_reference.3mf` then `inspect` → plates:1
  objects:1 filaments:4; re-init from the CLI's own output produced
  identical counts; all exit 0.
- Build environment for the GA-based tree (Windows):
  - Deps dir: the **existing** `C:\Users\ildarcheg\Documents\GitHub\BambuStudio_dep`
    (2.7-era deps) — works unchanged, no rebuild needed for
    `v02.07.01.62`.
  - Build dir: `build_v20701`, configured with
    `cmake -S . -B build_v20701
    -DCMAKE_PREFIX_PATH=".../BambuStudio_dep/usr/local"
    -DSLIC3R_BUILD_TESTS=ON
    -DPKG_CONFIG_EXECUTABLE=C:/Strawberry/perl/bin/pkg-config.bat`.
    `v02.07.01.62` has `find_package(PkgConfig REQUIRED)`, so the `-D`
    is mandatory (without it, CMake picks Strawberry's extensionless
    `perl` script and configure fails).
  - `build_win.bat` on this tag auto-selects the newest installed VS
    (VS 2026 on this box) — any *deps* rebuild would need `-v 16`, but
    none was needed this time since the 2.7-era deps are reused as-is.
  - `cli_tests.exe` has no `POST_BUILD` DLL-copy step — run it with
    `build_v20701\src\cli\Release` and its `occt` DLL dir prepended to
    `PATH`.
  - Machine note: Windows Smart App Control blocked freshly built
    exes (CodeIntegrity `0xC0E90002`) until the user disabled it on
    2026-07-14.
  - GUI manual sign-off in Bambu Studio (`v02.07.01.62`, GA) is still
    pending (user-driven gate, not yet performed) — see
    `docs/cli/status.md`.
- Working tree is clean as of this CLAUDE.md write.

## Push guidance (not yet executed — user's call)
`master` was reset locally to the `port-cli-v2.07.01` tip; `origin/master`
still has the old pre-port history. Publishing requires, **in this
order**:
1. `git push origin archive/v2.7-cli` — hard precondition, preserves
   the entire 2.7-era lineage on the remote before it becomes
   unreachable from `origin/master`.
2. `git push origin port-cli-v2.08` — preserves the parked 2.08 port
   on the remote too, since it is reachable only from a branch (not
   `master`) once this retarget lands.
3. `git push --force-with-lease origin master` — rewrites
   `origin/master` to the `v02.07.01.62`-based port tip.
Do not do step 3 without steps 1 and 2 first.

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
- `docs/cli/status.md` manual GUI smoke gates still `[ ]` for M1–M10
  and Phases B/C/D — none of those 3MFs have been signed off in
  Bambu Studio. M11 (2026-05-30) and M12 (2026-06-01) are the two
  signed-off milestones from the pre-port (2.7-era) lineage; the
  `v02.07.01.62` GA port itself has NOT had a GUI manual sign-off yet
  (open item as of 2026-07-14 — see "Branch state" above). The parked
  2.08 port also has no GUI sign-off, but that is out of scope until
  the 2.08 line goes GA.
- `bambu-cli --verbose` is parsed but a no-op (intentional deferral —
  see Phase F.1 entry in `status.md`).
- No `install(TARGETS bambu-cli)` — ships from the build dir only
  (`src/cli/CMakeLists.txt:100`; currently `build_v20701\src\cli\Release`
  for the GA-based tree, `build_v208\src\cli\Release` for the parked
  2.08 port, historically `build\src\cli\Release` for the pre-port
  lineage).
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
  Phases A..G + convergence).
- `docs/cli/manual-test.md` — 383-line manual GUI smoke recipe.
- `docs/superpowers/specs/2026-05-31-project-apply-batch-design.md` —
  M12 spec (1001 lines).
- `docs/superpowers/plans/2026-05-31-project-apply-batch.md` —
  M12 27-task TDD plan (2699 lines).
- `docs/superpowers/specs/2026-07-14-port-cli-v208-design.md` +
  `docs/superpowers/plans/2026-07-14-port-cli-v208.md` — the port
  spec/plan, originally written for the `v02.08.00.50` beta and
  re-executed unchanged (same playbook) against `v02.07.01.62` after
  the GA retarget; see `docs/cli/status.md` for the
  `v02.07.01.62` port entry.
