# Port bambu-cli onto upstream v02.08.00.50 — design

**Date:** 2026-07-14
**Status:** approved by user (this session)

## Goal

Bring the fork up to the latest upstream BambuStudio release
(`v02.08.00.50`) and carry the bambu-cli project (M0–M12 + convergence)
onto it. Only the CLI work carries over; the fork's other
customizations are dropped (see "What carries over").

## Context

- Current `master` = upstream tag `v02.07.00.55` + 196 fork commits
  (the entire bambu-cli effort, plus incidental GUI/build
  customizations). Clean tree, fully pushed to `origin/master`.
- Upstream released `v02.07.01.62` (patch) and `v02.08.00.50` (major,
  360 commits / 1,118 files / ~242k insertions ahead of our base).
  Target chosen: **v02.08.00.50**.
- Upstream 2.08 changed the deps tree (new vendored libharu, FREETYPE
  and Assimp changes, GMP patch, `deps-windows.cmake`) → a **full deps
  rebuild** is required on Windows.
- CLI dependency hotspots changed moderately in 2.08:
  `Format/bbs_3mf.cpp` (+157), `Model.{cpp,hpp}` (+159),
  `PrintConfig.cpp` (+251), `Arrange.cpp` (+58). No wholesale API
  rewrite observed at diff-stat level.

## What carries over (user decision)

**Carried:** `src/cli/`, `tests/cli/`, `docs/cli/`,
`docs/superpowers/`, `openspec/`, `CLAUDE.md`, our `.gitignore`
additions, and the two one-line `add_subdirectory(cli)` hooks in
`src/CMakeLists.txt` / `tests/CMakeLists.txt`.

**Dropped:** camera-fullscreen GUI feature (`StatusPanel.cpp` +448,
`CameraFullscreenMac.{hpp,mm}`, `MainFrame`/`MediaPlayCtrl`/SVGs),
libslic3r tweaks (`PerimeterGenerator`, `PrintConfig`, `Print`,
`Preset`, `AppConfig`, `ConfigManipulation`), and the scattered CMake
one-liners. Exception: any CMake tweak that turns out to be *required*
to build 2.08 on Windows gets re-applied as an explicit build-fix
commit (not as a carried feature).

## Approach (chosen: fresh port branch)

Rejected alternatives: rebasing 196 commits onto the tag (dozens of
conflict passes, manual dropping of unwanted commits) and merging the
tag into master (keeps the dropped customizations, needs revert
commits).

### Branching & end state

1. Tag current master `archive/v2.7-cli` (findable archive of dropped
   customizations).
2. Branch `port-cli-v2.08` from tag `v02.08.00.50`.
3. After verification is green, reset `master` to the port branch.
   Pushing the new master to origin is a **force-push** — deferred to
   the user as an explicit final decision; not required for the work.

### Port mechanics

1. One clean commit: `git checkout master -- src/cli tests/cli
   docs/cli docs/superpowers openspec CLAUDE.md` + `.gitignore`
   additions → "port bambu-cli tree onto v02.08.00.50".
2. Hand-re-apply the two `add_subdirectory(cli)` hook lines (upstream
   modified both CMakeLists in 2.08 — no blind copy).
3. Rebuild deps into a **separate** dir
   (`C:\Users\ildarcheg\Documents\GitHub\BambuStudio_dep_v208`),
   keeping the working 2.7 deps for rollback. Re-apply the FFMPEG
   `.pc` absolute-prefix patch afterwards. Use the documented PATH
   priming + env vars (VS-bundled CMake 3.20, `CMAKE_GENERATOR=Visual
   Studio 16 2019`, `CMAKE_POLICY_VERSION_MINIMUM=3.5`).
4. Configure with `-DSLIC3R_BUILD_TESTS=ON`, build `libslic3r` →
   `bambu-cli` → `cli_tests` (Release config, `--parallel 2`), fixing
   compile breaks against the new libslic3r as small, explained
   commits. Expected hotspots: `load_bbs_3mf`/`store_bbs_3mf`
   signatures, `Model`/`PlateData` fields, `stubs_for_libslic3r.cpp`
   if the `LogSink`/`Http` link surface grew.
5. Update `CLAUDE.md` + `docs/cli/status.md` with the new base
   version and port outcome.

## Verification bar

- Full `cli_tests` suite green (447 cases / 4,271 assertions on 2.7 —
  same bar on 2.08). Failures analyzed one by one: a 2.08 format
  change may legitimately shift expectations (fix the expectation,
  document why); a port bug gets fixed in code. No blind
  re-baselining.
- `bambu-cli.exe` builds; e2e smoke: compose a 3mf via the CLI and
  re-open it with the CLI.
- Manual GUI sign-off in Bambu Studio 2.08 remains a later,
  user-driven gate (as with M11/M12).

## Risks

- **Deps rebuild flakiness** (Windows): mitigated by documented
  workarounds; Defender exclusions already cover both dirs' parent.
- **3mf format drift in 2.08**: invariant guard / roundtrip tests may
  diverge; each divergence investigated individually.
- **`AMSMaterialsSetting.cpp` patch** from the 2.7 build notes: check
  whether upstream fixed it in 2.08 before re-applying anything (it is
  GUI-only, so it should not block the CLI build).
- **Stub surface growth**: 2.08 libslic3r may reference new symbols
  that `stubs_for_libslic3r.cpp` must no-op; verified at link time.
