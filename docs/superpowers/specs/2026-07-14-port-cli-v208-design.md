# Port bambu-cli onto upstream v02.08.00.50 — design

**Date:** 2026-07-14
**Status:** approved by user (this session)

## Goal

Bring the fork up to the latest upstream BambuStudio release
(`v02.08.00.50`) and carry the bambu-cli project (M0–M12 + convergence)
onto it. Only the CLI work carries over; the fork's other
customizations are dropped (see "What carries over").

## Context

- Current `master` = upstream tag `v02.07.00.55` + 197 fork commits
  (the entire bambu-cli effort, plus GUI/print/build customizations).
  Clean tree; pushed to `origin/master` except this spec commit.
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
`docs/superpowers/`, `CLAUDE.md`, our `.gitignore` additions, and the
two one-line `add_subdirectory(cli)` hooks in `src/CMakeLists.txt` /
`tests/CMakeLists.txt`. (`openspec/` is NOT carried — it was added
and later deleted within the fork's history and does not exist at
master HEAD; including it in the checkout pathspec would fail the
whole command.)

**Carried as build fixes** (verified still unfixed at `v02.08.00.50`;
re-applied upfront as one explicit build-fix commit — they enable
modern-CMake / macOS builds, which the fork's documented macOS
`cli_tests` path depends on):
- `src/Shiny/CMakeLists.txt` — `cmake_minimum_required` 2.8.12 → 3.5
- `src/admesh/CMakeLists.txt` — `cmake_minimum_required` 2.8.12 → 3.5
- `cmake/modules/FindOpenVDB.cmake` — `cmake_minimum_required` 3.3 →
  3.5...4.3
- `src/libslic3r/CMakeLists.txt` — non-Win32 freetype link via
  `find_package(Freetype REQUIRED)` + `Freetype::Freetype`

**Dropped:**
- Camera-fullscreen GUI feature (`StatusPanel.cpp` +448,
  `CameraFullscreenMac.{hpp,mm}`, `MainFrame`/`MediaPlayCtrl`/SVGs,
  and the `AppConfig.cpp` `camera_fullscreen_active_monitor_only`
  default, plus its `src/slic3r/CMakeLists.txt` source additions).
- **The `alternate_extra_wall` print feature** — a coherent
  user-facing setting (extra wall on alternating layers) spanning
  `PerimeterGenerator.cpp`, `PrintConfig.{cpp,hpp}`, `Print.cpp`,
  `Preset.cpp`, `ConfigManipulation.cpp`, `Tab.cpp`. This is a
  knowing retirement, not cleanup; the code stays reachable via the
  `archive/v2.7-cli` tag. Nothing in `src/cli`/`tests/cli` references
  it, so the port is unaffected.
- `src/clipper2/CMakeLists.txt` Clang `-Wno-unknown-warning-option`
  guard (re-apply only if a Clang build actually hits it).

## Approach (chosen: fresh port branch)

Rejected alternatives: rebasing 197 commits onto the tag (dozens of
conflict passes, manual dropping of unwanted commits) and merging the
tag into master (keeps the dropped customizations, needs revert
commits).

Acknowledged cost of the chosen approach: `git log` / `git blame` /
`--follow` on `src/cli` stop at the single port commit — the
per-commit history of M0–M12 is only reachable via the
`archive/v2.7-cli` tag. Rebasing would have preserved it; we trade
that for a clean base and one conflict-free pass.

### Branching & end state

1. Tag current master `archive/v2.7-cli` (findable archive of dropped
   customizations).
2. Branch `port-cli-v2.08` from tag `v02.08.00.50`.
3. After verification is green, reset `master` to the port branch.
   Pushing the new master to origin is a **force-push** — deferred to
   the user as an explicit final decision; not required for the work.
   **Hard precondition of any force-push:** `git push origin
   archive/v2.7-cli` first, so origin retains the 197-commit fork
   history before `origin/master` is rewritten.

### Port mechanics

1. One clean commit: `git checkout master -- src/cli tests/cli
   docs/cli docs/superpowers CLAUDE.md` + `.gitignore` additions →
   "port bambu-cli tree onto v02.08.00.50". (No `openspec` — see
   "What carries over".)
2. Hand-re-apply the two `add_subdirectory(cli)` hook lines (upstream
   modified both CMakeLists in 2.08 — no blind copy). Then one
   build-fix commit re-applying the four CMake portability fixes
   listed under "Carried as build fixes".
3. Rebuild deps into a **separate** dir
   (`C:\Users\ildarcheg\Documents\GitHub\BambuStudio_dep_v208`),
   keeping the working 2.7 deps for rollback. Re-apply the FFMPEG
   `.pc` absolute-prefix patch afterwards. Use the documented PATH
   priming + env vars (VS-bundled CMake 3.20, `CMAKE_GENERATOR=Visual
   Studio 16 2019`, `CMAKE_POLICY_VERSION_MINIMUM=3.5`).
4. Configure with `-DSLIC3R_BUILD_TESTS=ON`, build `libslic3r` →
   `bambu-cli` → `cli_tests` (Release config, `--parallel 2`), fixing
   compile breaks against the new libslic3r as small, explained
   commits. Expected breaks are **narrow** (verified against the
   tags): `load_bbs_3mf`/`StoreParams` and the `Arrange.hpp` /
   `Orient.hpp` APIs are unchanged between 2.7 and 2.08, and
   `bbs_3mf.hpp` only gains a `#define`. Remaining candidates:
   `Model`/`PlateData` field additions and
   `stubs_for_libslic3r.cpp` if the `LogSink`/`Http` link surface
   grew (LogSink is still present at 2.08, so the stubs stay).
5. Update `CLAUDE.md` + `docs/cli/status.md` with the new base
   version and port outcome; fix CLAUDE.md's stale "33 commits ahead
   of origin/master; not pushed yet" branch-state note in the same
   pass.

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
  workarounds. NOTE: Defender exclusions are per-directory (repo and
  old deps dir only) — the new `BambuStudio_dep_v208` dir must be
  excluded *before* the deps build (admin `Add-MpPreference`), and
  2.08's `build_win.bat` must be pinned with `-v 16` (it now
  auto-selects the newest installed VS, which is VS 2026 on this
  machine, and that path demands CMake ≥ 4.2).
- **Model assembly-data serialization (the specific 3mf-drift
  risk):** 2.08 adds per-volume `m_assemble_transformation` (+ an
  `m_assemble_initialized` flag) to `ModelVolume`'s cereal `ar(...)`
  lists, plus assembly-tree / assembly-steps JSON data on `Model`.
  New content inside the 3mf is exactly what the invariant guard and
  roundtrip tests assert over — if anything diverges, look here
  first. Each divergence investigated individually; no blind
  re-baselining.
- **Arrange behavior changes**: `Arrange.cpp` at 2.08 adds
  skirt-distance spacing, a seq-print bin pre-screen fix, and a
  post-arrange edge clamp. The API is unchanged, but M11
  arrange/layout test *expectations* (placements, spacing) may shift
  legitimately.
- **Stub surface growth**: 2.08 libslic3r may reference new symbols
  that `stubs_for_libslic3r.cpp` must no-op; verified at link time.
  (LogSink is confirmed still present at 2.08 — the stubs remain
  necessary, per CLAUDE.md's divergence note.)

Struck after verification: the `AMSMaterialsSetting.cpp` source-patch
risk — upstream 2.08 already contains the fix
(`GetFilaSystem()->GetExtruderIdByAmsId(...)`); nothing to re-apply.
