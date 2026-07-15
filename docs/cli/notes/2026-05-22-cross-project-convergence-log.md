# Bambu cross-project convergence log

Branch: `cross-project-convergence` (already existed at start)
Started: 2026-05-22

Reference reports:
- `C:\Users\ildarcheg\AppData\Local\Temp\bambu_studio_changes_report.md`
- `C:\Users\ildarcheg\AppData\Local\Temp\orca_slicer_changes_report.md`

---

## Item 1 — Roundtrip test subtree

**What I found:**
- Orca ships 7 roundtrip test files at `OrcaSlicer/tests/cli/roundtrip/`:
  `test_project_init.cpp`, `test_plate.cpp`, `test_merge.cpp`,
  `test_output_flag.cpp`, `test_project_tab.cpp`, `test_save_atomic.cpp`,
  `test_split.cpp`. Bambu had only a `.gitkeep` placeholder.
- Helper / API divergences that needed adapting (no Bambu source changes):
  - `ref_3mf()/make_temp_dir()/copy_ref_to_temp()` → Bambu's
    `canonical_committed_3mf()` + `fresh_temp_path()` + inline `fs::copy_file`.
  - `run_cli().stdout_` → `spawn_cli().stdout_text`.
  - `unzip_to_memory()` + `verify_relationships/verify_plate_thumbnails`
    helpers → Bambu's `list_zip_entries(path)` + `assert_relationships_resolve`
    + `assert_plate_thumbnails_128`.
  - `find_object(s, name)` → inlined as a 4-line static helper inside the two
    test files that need it (Bambu doesn't export an equivalent).
  - `AddObjectParams` struct → Bambu's positional
    `add_object_to_plate(state, plate, stl, name, -1, nullptr, 1, nullptr)`.
  - `set_object_filament(s, name, slot, optional<part>)` → Bambu's
    `set_object_filament(s, name, slot, part_name)` with `""` for object-level.
  - `merge_object_parts(s, name, parts, into, optional)` → Bambu's
    `merge_object_parts(s, name, MergePartsParams{parts, into, -1})`.
  - `s.plates / s.model->objects` → `s.plate_data / s.model.objects`.
  - All `<catch2/catch_all.hpp>` includes → Bambu's `<catch2/catch.hpp>`
    (Catch2 v2 here vs v3 there).

**What I changed:**
- Deleted `tests/cli/roundtrip/.gitkeep`.
- Added 7 new files under `tests/cli/roundtrip/` (one per Orca source).
- Wired all 7 into `tests/cli/CMakeLists.txt::BAMBU_CLI_TEST_SOURCES`.
- Two Bambu-side divergences from Orca that I encoded in the port:
  1. **Empty reference 3mf**: Bambu's `tests/cli/fixtures/reference.3mf` is an
     empty project (no objects). The `test_project_init.cpp` port asserts
     `plates` and `project_config.keys()` non-empty (instead of Orca's
     `objects.size() > 0`), and the G2 rebuild test bootstraps an object
     in-memory before save+reload to exercise `obj_inst_map`.
  2. **Strict merge step (i)**: Bambu's `merge_object_parts` allows ONLY the
     `extruder` key in per-volume config (the report calls this out as a
     deliberate design choice). The merge roundtrip drops the
     `wall_loops` per-volume agreement subtest and now only asserts name +
     extruder survive — documented inline in `test_merge.cpp`.
  3. **Cover-image path**: Bambu embeds covers at `Auxiliaries/cover.png`
     (not Orca's `Auxiliaries/.thumbnails/thumbnail_3mf.png`). The
     project-tab cover roundtrip asserts Bambu's path.
- Skipped: nothing whole-test — every Orca file landed in some form.

**What I tested:**
- Initial build: `cmake --build build --target cli_tests --config Release
  --parallel 2` → 0 errors.
- First pass `cli_tests "[roundtrip]"` → 3 failures (project_init x2 from
  empty-reference, merge x1 from strict step i).
- Adapted those 3 tests; rebuilt; re-ran.
- Final: **`[roundtrip]` 13 cases, 115 assertions, all pass.**
- Full suite: **229 cases, 1124 assertions, all pass** (216 prior + 13 new).

## Item 2 — Cover-image refcount

**What I found (VERIFY step):**
- Read `src/cli/project_tab_ops.cpp`. Bambu does NOT enforce the refcount.
  - `info_clear` (around line 115 of the original file) only sets
    `mi.cover_file = ""` and never touches the on-disk file.
  - `profile_clear` (around line 161) only sets `pi.ProfileCover = ""`.
  - `embed_cover` writes to a SINGLE on-disk path `<aux>/cover.png` that is
    shared by both info and profile (called with the same `archive_entry =
    "Auxiliaries/cover.png"` from `info_set` and `profile_set`).
- Result: clearing one surface leaves a dangling on-disk `cover.png` even
  when the second surface still references it; clearing both never deletes
  the on-disk file at all (it just lingers in the aux temp dir).
- Path is `Auxiliaries/cover.png` (Bambu) vs Orca's
  `Auxiliaries/.thumbnails/thumbnail_3mf.png`; spec said use Bambu's path.

**What I changed:**
- Ported Orca's refcount semantics (Orca
  `src/cli/project_tab_ops.cpp::clear_cover_image`,
  `project_tab_ops.hpp:223`) into `src/cli/project_tab_ops.cpp`:
  - Added 3 internal helpers: `info_cover_empty(model)`,
    `profile_cover_empty(model)`, and `delete_cover_file_if_unreferenced(model)`
    which removes `<aux>/cover.png` only when both surface pointers are empty.
  - `info_clear` and `profile_clear` now track whether the cover field was
    cleared in this call and invoke the refcount-delete helper once at the
    end of the loop.
- No changes to `embed_cover`, header, or aux-list ops — the on-disk path
  and the archive-relative pointer string remain `Auxiliaries/cover.png`.
- No GUI / store_bbs_3mf changes (Bambu's writer reads from
  `model.model_info` / `model.profile_info` directly, so the pointer change
  on its own is enough for the serialized output to reflect the clear).

**What I tested:**
- Added 4 new `[cover_refcount]` unit tests in
  `tests/cli/unit/test_project_info_ops.cpp`:
  1. Profile clear leaves file in place when info still references it; a
     subsequent info clear drops it.
  2. Symmetric: info clear first, then profile clear deletes.
  3. Single-surface case: clearing the only surface deletes immediately.
  4. Idempotent: clearing an already-empty cover does not throw or fail.
- `cli_tests "[cover_refcount]"` → **4 cases, 30 assertions, all pass**.
- Full suite: **233 cases, 1154 assertions, all pass** (was 229).

## Item 3 — Cross-plate identify_id uniqueness regression test

**What I found (VERIFY step):**
- Orca test located at `OrcaSlicer/tests/cli/e2e/test_object.cpp:736`,
  tagged `[orca-cli][P3][e2e][cross_plate]`. The test:
  1. Copies the reference 3mf to a temp working file
  2. Runs `plate add A`, `plate add B`, `object add ... --plate A`,
     `object add ... --plate B` via the CLI
  3. Re-opens the resulting `.3mf` as a zip, extracts
     `Metadata/model_settings.config`, scrapes every
     `key="identify_id" value="<digits>"` occurrence with regex
  4. REQUIREs total > 0 AND `ids.size() == size_t(total)` (every value
     distinct).
- Bambu coverage check:
  - `grep identify_id tests/cli` → 0 matches.
  - `grep loaded_id tests/cli` → `tests/cli/unit/test_project_ops_objects.cpp:225`
    has a regression test for the **same root cause** but at the
    **in-memory** level (uses `list_objects` and asserts cross-plate
    filtering returns the right thing). No archive-level
    `identify_id` uniqueness check exists.
  - Bambu fix history (`git log --grep`): commit `faa5f4dd6`
    ("fix(cli): global loaded_id assignment in add_object_to_plate") is
    the fix referenced by the Orca test comment.
- Bambu's coverage is weaker than Orca's: a purely-in-memory pass would
  miss a serialization regression that drops/changes `identify_id` during
  store_bbs_3mf. Port the Orca archive-scrape test as a complement.

**What I changed:**
- Appended one new `TEST_CASE` to
  `tests/cli/e2e/test_object_add.cpp` tagged
  `[e2e][object_add][cross_plate]`, mirroring the Orca version verbatim
  (adapted to Bambu's `spawn_cli` / `canonical_committed_3mf` / fixture
  paths and to the Bambu CLI verbs which already match Orca's).
- Added `<miniz.h>`, `libslic3r/miniz_extension.hpp`, `<regex>`, `<set>`,
  `<cstring>` to the file's includes.
- No production-side changes — this is a regression pin against current
  HEAD, not a fix.

**What I tested:**
- `cli_tests "[cross_plate]"` → **2 cases, 22 assertions, both pass**
  (the new e2e + the prior in-memory test_project_ops_objects unit case).
- Full suite: **234 cases, 1164 assertions, all pass** (was 233).
- Bambu's faa5f4dd6 fix holds: the per-plate add produces 2 distinct
  identify_id values in the saved archive. No fix needed.

## Item 4 — Staging-copy validation in `project init`

**What I found:**
- Bambu's old `project init` flow (`src/cli/commands/project.cpp`):
  1. `atomic_copy(template, out)` — copies template directly to `out`.
  2. `load_project(out)`
  3. `check_thumbnails_in_archive(out, state)`
  4. `save_project(state, out)` (overwrites `out` via store + .bak swap)
- The unvalidated template bytes therefore live at the user-visible `out`
  path between step 1 and step 4. On validation failure the code did
  `boost::filesystem::remove(out)` to roll back. A crash between 1 and 4
  would leave a half-baked clone at the user's destination.
- Orca's pattern (`OrcaSlicer commands/project_init.cpp:31-74`) uses a
  side-staging file `<out>.init-tmp` so the destination is never written
  until `save_project` succeeds.
- `check_thumbnails_in_archive` itself opens the file via
  `mz_zip_reader_init_file` (not `Slic3r::open_zip_reader`) so it tolerates
  Windows 8.3 short paths under TEMP — Orca lacks that tolerance. Per
  the spec, keep it.

**What I changed:**
- Reworked `src/cli/commands/project.cpp::register_project_subcommands`
  init callback to the staging-copy pattern:
  1. `fs::exists(template)` check up front → exit 2 on miss (preserves
     the existing `missing template -> exit 2 file_not_found` test).
  2. `fs::copy_file(template, <out>.init-tmp,
     overwrite_existing)` → side staging file. Copy failures map to
     `invalid_state` (exit 7).
  3. `load_project(staging)` — loads from staging copy.
  4. `check_thumbnails_in_archive(staging, state)` — validates the bytes
     we actually loaded; staging cleanup runs on failure. The function
     itself is untouched, so `mz_zip_reader_init_file` short-path
     tolerance is preserved.
  5. `save_project(state, <out>)` — first time `out` is written. The
     existing post-save guard + .bak swap in `save_project` handles
     atomicity at the destination.
  6. Staging file removed regardless of save outcome.
- No production cleanup of the now-unused `atomic_copy` helper in
  `io.hpp` / `io.cpp` — leaving it in this commit's scope to avoid mixing
  cleanup with the substantive change; a follow-up removal is trivial.
- Existing exit-code mapping preserved: missing template = 2, validation
  failure = 8, store/guard failure = 8 (via `save_project`), rename
  failure = 7 (via `save_project`).

**What I tested:**
- Added 2 new staging-cleanup assertions to existing
  `tests/cli/e2e/test_project_init.cpp` cases:
  - `happy path` now asserts `<out>.init-tmp` is gone after success.
  - `corrupted template -> exit 8` now asserts `<out>.init-tmp` is gone
    after the validation failure (and the existing
    `REQUIRE_FALSE(fs::exists(out))` still holds — destination was
    never written).
- `cli_tests "[project_init],[m2_baksave]"` → **7 cases, 44 assertions,
  all pass**.
- Full suite: **234 cases, 1166 assertions, all pass** (was 234 / 1164;
  +2 for the new staging-cleanup assertions, no new test cases).

---

## Round 2

Round 2 picks up two items the Orca side surfaced from the other direction
(Bambu-fixes-itself, not Orca-→-Bambu ports).

## Item B5 — Fix plate_world_origin column-count bug

**What I found:**
- Bambu's `plate_world_origin(plate_index_1based, bed_width, bed_height)` at
  `src/cli/project_ops.cpp:141` derives `cols` from `plate_index_1based`,
  which is a comment-acknowledged shortcut ("Since we don't know
  total_plate_count we use a fixed large-enough column count").
- The shortcut diverges from the GUI's `PartPlate.cpp:4776`
  `compute_colum_count(m_plate_count)` for any plate past the first row in
  a layout with more plates than its index. Worked example: 5-plate layout,
  plate 3. GUI / Orca: `cols=ceil(sqrt(5))=3` → col 2 row 0 → origin
  `(2*stride, 0)`. Bambu old: `cols=ceil(sqrt(3))=2` → col 0 row 1 →
  origin `(0, -stride)`. Different position by a full stride.
- Stride math (`bed * 1.2`) and the row Y-negation were already correct
  on both sides; only the column-count derivation was wrong.
- Orca's `src/cli/placement.cpp::plate_origin_offset` already takes
  `total_plates` and uses `ceil(sqrt(total_plates))`.
- Only one in-tree caller: `add_object_to_plate` (1 call site).

**What I changed:**
- Added a `total_plates` parameter to `plate_world_origin` in
  `src/cli/project_ops.hpp` and `.cpp`. Cols now uses
  `ceil(sqrt(total_plates))`, matching the GUI helper and Orca.
- Updated `add_object_to_plate` to pass `state.plate_data.size()` as the
  total. The plate the object is being added to is already counted in
  `plate_data` (CLI flow is load → mutate → save; we're not pre-adding
  the plate).
- Replaced the old float-precision dance (sqrt+round+conditional) with a
  plain `std::ceil(std::sqrt(...))`.
- Updated doc comment on the header to cite `PartPlate.cpp:4776` and
  `OrcaSlicer src/cli/placement.cpp::plate_origin_offset` for sibling
  parity.

**What I tested:**
- Rewrote `tests/cli/unit/test_plate_stride.cpp` to pass `total_plates`
  alongside the index in each call. The first 5 prior cases keep their
  exact previous expected values (since `total == index` in each, the
  cols derivation is unchanged for those inputs).
- Added a new B5 regression case at the bottom of the file
  (`[m1_audit][stride][cross_plate]`) pinning the corrected behavior for
  plate 3 of 5 and plate 4 of 5 — the inputs the old formula got wrong.
- `cli_tests "[stride]"` → **6 cases, 15 assertions, all pass**.
- Full suite: **235 cases, 1170 assertions, all pass** (was 234 / 1166;
  +1 new test case, +4 new assertions).

## Item B6 — stubs_for_libslic3r.cpp investigation

**What I found (static survey first):**
- Orca's `orca_cli_core` links libslic3r only and ships no stubs file.
- Bambu's `src/cli/CMakeLists.txt` registers `stubs_for_libslic3r.cpp` and
  links libslic3r (not libslic3r_gui) — same structural shape as Orca.
- `grep "Slic3r::Http|BBL_Encrypt|slic3r/Utils/Http.hpp" src/libslic3r`:
  - OrcaSlicer: **0 matches**.
  - Bambu: 1 match → `src/libslic3r/LogSink.cpp`. Calls
    `Slic3r::Http::get(url)` (line 280) and
    `Slic3r::BBL_Encrypt::AES256CBC_Encrypt/Decrypt` (lines 120, 162, 380).
- `grep LogSink src/libslic3r`:
  - OrcaSlicer: **0 matches** (no LogSink file).
  - Bambu: `src/libslic3r/CMakeLists.txt:43-44` registers `LogSink.hpp` +
    `LogSink.cpp` into libslic3r itself.
- Prediction from the static survey: dropping the stubs will produce
  unresolved externals on `Slic3r::Http::*` and `Slic3r::BBL_Encrypt::*`
  from `libslic3r.lib(LogSink.obj)`.

**Experimental confirmation:**
- Temporarily commented `stubs_for_libslic3r.cpp` out of
  `BAMBU_CLI_CORE_SOURCES`, ran `cmake --build build --target bambu-cli
  --config Release`.
- Link failed with `LNK1120: 8 unresolved externals`, all
  `LNK2001` and all from `libslic3r.lib(LogSink.obj)`:
  `Slic3r::Http::get(string)`, `Slic3r::Http::~Http`,
  `Slic3r::Http::timeout_max(long)`, `Slic3r::Http::on_complete`,
  `Slic3r::Http::on_error`, `Slic3r::Http::perform_sync`,
  `Slic3r::BBL_Encrypt::AES256CBC_Encrypt`,
  `Slic3r::BBL_Encrypt::AES256CBC_Decrypt`.
- That is exactly the set the stubs file defines (the other Http stubs are
  there to cover the symbols Http.hpp's class layout might pull in via
  vtable, but only 8 actually surface as unresolved from LogSink.obj).
- Reverted the CMakeLists change. Rebuild succeeds; full suite green.

**Decision: keep the stubs, document the why.**

**What I changed:**
- Added a 6-line comment in `src/cli/CMakeLists.txt` next to the
  `stubs_for_libslic3r.cpp` source-list entry pointing readers at the
  detailed explanation in the stubs file.
- Added a paragraph to `src/cli/stubs_for_libslic3r.cpp`'s leading comment
  block covering:
  - The exact source of the references (`src/libslic3r/LogSink.cpp:6, :120,
    :162, :280, :380`) and the CMakeLists lines (43-44) that include it.
  - The Orca structural divergence (no LogSink in Orca's libslic3r).
  - The verified 8-symbol unresolved-externals list from the experiment.
  - The three alternatives that would remove the need for stubs and why
    each is a larger surface change than keeping them:
    (a) compile real Http.cpp + BBLUtil.cpp → drags in
        curl/libssl/crypt32 (defeats the point);
    (b) move LogSink to libslic3r_gui → touches upstream code outside
        `src/cli/`;
    (c) refactor LogSink to not call Http/BBL_Encrypt → same.

**What I tested:**
- Reverted-state build of `cli_tests` succeeds.
- Full suite: **235 cases, 1170 assertions, all pass** (unchanged from
  B5 — this commit only updates comments).
