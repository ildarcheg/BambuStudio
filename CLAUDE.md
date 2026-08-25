# bambu-cli — session context

## What this project is
A standalone GUI-free CLI (`bambu-cli.exe`) that composes BBS `.3mf`
projects by driving libslic3r's load/store directly. Lives under
`src/cli/` + `tests/cli/`. Parallel to the sibling `orca-cli` initiative
in the OrcaSlicer fork formerly checked out at
`C:\Users\ildarcheg\Documents\GitHub\OrcaSlicer` (read-only reference;
feature-parity is the explicit goal). **That checkout no longer exists
on this machine** — re-clone it before relying on any sibling
comparison below.

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
exit 1 *before* the per-op override lookup. (Since 2026-07-15,
split-to-parts / merge-parts mesh-state errors throw a typed
`InvalidStateError` → exit 7 via the built-in ladder and those verbs'
`invalid_argument → 7` override maps are gone; merge's bad
`--filament` is exit 1 like every other verb. auto-orient's
`runtime_error → 7` override is the only one left.)

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

## Branch state (as of 2026-08-24)
- `master` is still the **`v02.07.01.62`** GA port (tip `570ffbe03`).
  The live work is on **`port-cli-v2.08.02`** = upstream tag
  `v02.08.02.61` (`926a71925`) + four port commits. Promotion of
  `master` to this branch has NOT happened yet and nothing has been
  pushed — `origin/master` still points at the 2.07 port.
- **Why the retarget:** the 2.08 line went GA (`v02.08.02.60`
  2026-08-14, `v02.08.02.61` 2026-08-21, both prerelease=false). The
  2026-07-14 retarget away from 2.08 happened *only* because
  `v02.08.00.50` / `v02.08.01.55` were prerelease. Policy is unchanged:
  `master` tracks the latest GA release, not the latest tag.
- **Zero changes were needed to `src/cli` or `tests/cli` source**, same
  as the 2.07 port. `load_bbs_3mf` / `store_bbs_3mf` / `StoreParams`
  signatures are unchanged and `Arrange.hpp` / `Orient.hpp` are
  byte-identical between `v02.07.01.62` and `v02.08.02.61`. `PlateData`
  gained fields (mixed filaments, AMS load/unload times,
  `pause_printing`) but only additively.
- **The carried non-CLI delta shrank.** Upstream has absorbed most of
  it: the `Freetype::Freetype` fix, and all six macOS/Clang-21 hunks
  (`check_cxx_compiler_flag` probe, `ASSIMP_BUILD_ZLIB`, the wxWidgets
  policy floor, `MediaPlayCtrl.h` / `wxMediaCtrl2.h`) now ship upstream,
  in several cases as strictly better versions. Do NOT re-apply them.
  What remains: 2 hooks, 10 `cmake_minimum_required` bumps to 3.5 (only
  where upstream is still *below* 3.5 — `src/admesh`, `src/boost` and
  `FindOpenVDB.cmake` are at 3.13 and must NOT be downgraded), the
  `deps/CMakeLists.txt` policy line, `.gitignore`, and the ffmpeg fix
  below.
- **`archive/v2.07.01-cli`** (`570ffbe03`) is the rollback tag for the
  pre-retarget `master`. `archive/v2.7-cli` (`6cb539d2f`) still
  preserves the entire pre-port 2.7-era lineage. `port-cli-v2.08` (tip
  `2e8ac75e6`) remains parked but is now **superseded and stale** — it
  is based on the 2.08.00.50 beta and its `src/cli` predates
  `161ed17f8`..`570ffbe03`. Retire it (tag + delete) only after this
  port is promoted.
- Last `cli_tests` run: **471 cases / 4406 assertions, all green** on
  the `v02.08.02.61`-ported tree (2 runs, one `--order rand`). The
  **447 / 4271** figure quoted in older notes is *stale*, not a
  regression — it was recorded at `486a702ad`, before the twelve
  quality commits; the `TEST_CASE` count in `tests/cli` went 448 → 472
  over that range. E2E smoke: `project init --template
  tests/cli/fixtures/test_reference.3mf`, then `inspect` → plates:1
  objects:1 filaments:4; re-init from the CLI's own output → identical
  counts; all exit 0.

## Build environment (Windows was REINSTALLED — 2026-08-24)
Every `C:\Users\ildarcheg\Documents\GitHub\...` path in older notes is
**gone**; that profile does not exist on this machine. Current setup:
- Deps: `C:\Users\ildar\Documents\BambuStudio_dep_v20802`, built fresh
  at the 2.08 revision (973 MB, Boost 1.84). Configure from
  `deps/build` with `-G "Visual Studio 17 2022" -A x64
  -DDESTDIR=<deps> -DCMAKE_BUILD_TYPE=Release -DDEP_DEBUG=OFF`.
- Build dir: `build_v20802`, configured with
  `-DCMAKE_PREFIX_PATH=<deps>/usr/local -DSLIC3R_BUILD_TESTS=ON
  -DPKG_CONFIG_EXECUTABLE=C:/Strawberry/perl/bin/pkg-config.bat`. That
  last `-D` is mandatory: Strawberry ships BOTH `pkg-config` and
  `pkg-config.bat`, and CMake picks the extensionless Perl script
  otherwise.
- Toolchain: VS 2022 Community 17.14.39, MSVC 14.44.35207
  (`MSVC_VERSION` 1944 → `DEP_VS_VER 17` / `msvc-14.3`), Windows SDK
  10.0.26100.0, Strawberry Perl 5.42.
- **Use the VS-bundled CMake 3.31.6**
  (`<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`), NOT
  `Kitware.CMake` — winget only offers 4.4.2, and CMake 4.x hard-errors
  on `cmake_minimum_required` below 3.5, which ten sub-projects still
  declare at this tag.
- Build only `cli_tests` / `bambu-cli` with `--parallel 2` (higher
  parallelism OOMs MSVC's PCH). Never build the `BambuStudio` GUI
  target (known LNK2038, out of scope).
- `cli_tests.exe` has no `POST_BUILD` DLL-copy step — run it with
  `build_v20802\src\cli\Release` and `<deps>\usr\local\bin\occt`
  prepended to `PATH`.
- **ffmpeg pkg-config fix (`deps/FFMPEG/fix_pc_prefix.cmake`):** on
  MSVC the ffmpeg dep is a prebuilt archive copied verbatim, and its
  `*.pc` files carry a relative `prefix=./dist`.
  `pkg_check_modules(LIBAV ...)` (`src/slic3r/CMakeLists.txt:807`) feeds
  that into `libslic3r_gui`, and CMake rejects a relative entry in
  `INTERFACE_INCLUDE_DIRECTORIES` as soon as another directory consumes
  the target — which only `-DSLIC3R_BUILD_TESTS=ON` causes
  (`tests/slic3rutils`). Upstream CI builds with tests off and never
  sees it. NOT a port regression: the same recipe and consumer exist at
  `v02.07.01.62`.

## Push record
The 2026-07-14 push (tag `archive/v2.7-cli` first, then
`--force-with-lease` `master`, then `port-cli-v2.08`) still stands —
`origin/master` is the `v02.07.01.62` port. **The 2026-08-24
`v02.08.02.61` port has NOT been pushed**; it exists only locally on
`port-cli-v2.08.02`.
## File layout
- `src/cli/` — entry (`main.cpp`), `io.{hpp,cpp}`,
  `project_ops.{hpp,cpp}`, `project_tab_ops.{hpp,cpp}`,
  `invariant_guard.{hpp,cpp}`,
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
  `v02.07.01.62` GA port never got one either, and neither has the
  `v02.08.02.61` port (open item as of 2026-08-24 — see "Branch state"
  above). This is the one gate the automated suite cannot close.
- 2.08 mixed (virtual) filaments — `PlateData::mixed_filaments_info`
  and `ams_list` — are neither read nor written by the CLI. They
  survive a load/store round-trip untouched. Teaching the CLI about
  them is a FEATURE, not port scope.
- `bambu-cli --verbose` is parsed but a no-op (intentional deferral —
  see Phase F.1 entry in `status.md`).
- No `install(TARGETS bambu-cli)` — ships from the build dir only
  (`src/cli/CMakeLists.txt:100`; currently
  `build_v20802\src\cli\Release`. The `build_v20701` and `build_v208`
  dirs named in older notes do not exist on this machine).
- ~~Thumbnail passthrough compaction caveat~~ — resolved 2026-07-15:
  `rewrite_thumbnails` was eliminated entirely; thumbnails now travel as
  decoded RGBA through `store_bbs_3mf`'s own thumbnail path (see
  `docs/superpowers/specs/2026-07-15-thumbnail-passthrough-refactor.md`).
  Content-identical (byte-identical for CLI-produced sources).

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
