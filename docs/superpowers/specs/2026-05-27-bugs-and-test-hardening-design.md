# Bugs and Test Hardening — Design (2026-05-27)

## Goal

Land four targeted improvements to the bambu-cli effort: two user-visible
bug fixes surfaced during recent work (non-ASCII metadata XML escaping,
thumbnail passthrough after plate compaction), and two test-coverage
gaps (e2e/unit coverage of the new invariant guards, negative-path
coverage of cover image embedding). All four flow through the existing
CLI/libslic3r boundary without expanding the link surface or
introducing new abstractions.

## Architecture

- Each phase is independent and produces working, shippable software on
  its own.
- Phases A and B touch production code (`src/libslic3r/Format/bbs_3mf.cpp`
  and/or `src/cli/io.cpp`). Phases C and D add tests only.
- TDD discipline throughout: every fix starts with a failing test;
  every test-only phase still uses red→green where applicable (a test
  that initially fails to compile or fails an unreached assertion
  counts as red).
- Per-phase commit policy: at minimum one commit per phase. Phases
  with investigation may split into "repro test (red)" plus "fix
  (green)" commits.
- No new CLI surface area, no new ops-layer abstractions, no changes
  to existing invariant guard signatures. Phase C reuses the existing
  `check_auxiliary_passthrough` and `check_cover_references_resolve`
  entry points.

## Scope

In scope (four phases):
- Phase A — Non-ASCII metadata XML escaping bug (item #1)
- Phase B — Thumbnail passthrough after plate compaction (item #2)
- Phase C — Invariant guard unit tests with corrupted fixtures (item #8)
- Phase D — Negative-path cover image tests (item #9)

Out of scope (explicit):
- Group B housekeeping: refreshing `CLAUDE.md` branch state, push
  decision for the current 15-commit-ahead `master` branch.
- Group C hygiene: `--verbose` no-op, `const_cast` in
  `invariant_guard.cpp`, `install(TARGETS bambu-cli)`.
- Any change to the CLI's user-facing flag surface.

These can be addressed separately when convenient.

---

## Phase A — Non-ASCII metadata XML escaping (#1)

### Symptom recap

Em-dash (`—`, U+2014) in `model_info.description` survived the call
into `store_bbs_3mf` but the saved archive failed the
`config_roundtrip` invariant guard on reload with `"not well-formed
(invalid token) at line 7"`. Surfaced during the Phase G manual GUI
smoke; worked around by using ASCII-only description text. Other
likely-affected fields by symmetry: `title`, `copyright`, `license`,
`profile_title`, `profile_description`, and plate names.

### What is already known

- `bbs_3mf.cpp` declares `<?xml version="1.0" encoding="UTF-8"?>` at
  every metadata-emitting site (`:6696`, `:6822`, `:6944`, `:7951`,
  `:8359`).
- `xml_escape` in `src/libslic3r/utils.cpp:1229` only escapes
  `" ' & < >`; non-ASCII bytes pass through unchanged. That is the
  correct behavior for valid UTF-8.
- All affected metadata fields are wrapped in `xml_escape` at the
  write site (`bbs_3mf.cpp:7004-7009`).
- Therefore the bug is **not** a missing escape call. Root cause is
  one of: (a) the writer mangles bytes on output (stream codepage
  conversion on Windows), (b) the input string was already malformed
  before reaching the writer (CLI argv came through in
  windows-1252 because of console codepage), or (c) the reader's
  expat configuration rejects valid UTF-8 input.

### Tasks

**A.1 — Repro pin (red test).** Add a roundtrip test under
`tests/cli/roundtrip/test_non_ascii_metadata.cpp`. Load a clean
fixture, set `model_info.description` to a string containing
`—` (em-dash, U+2014), `测试` (CJK), `"x"` (smart quotes, U+201C/D),
and `café` (accented Latin), save via `save_project`, reload via
`load_project`, assert the description round-trips byte-identically.
Expected outcome: this test fails today against current master.

**A.2 — Root cause investigation.** With A.1 failing, instrument the
write path:
1. Hex-dump `description` immediately before the `stream <<` call in
   `bbs_3mf.cpp` (add temporary `BOOST_LOG_TRIVIAL(trace)`).
2. After save, hex-dump the corresponding bytes in the saved archive's
   metadata XML.
3. Compare to determine whether bytes change in transit (writer-side
   issue) or arrive at the writer already malformed (CLI-input-side).
4. Remove instrumentation before committing.
5. Capture findings in `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md`.

**A.3 — Fix.** Scope follows A.2's findings:
- If writer-side (stream codepage): swap to a UTF-8-clean output
  stream at the metadata write site (likely `boost::nowide::ofstream`
  or explicit `std::ofstream` with binary mode). Smallest possible
  code change.
- If CLI-input-side (argv encoded as windows-1252): convert at the
  CLI boundary — switch `main` to `wmain` on Windows, convert UTF-16
  argv to UTF-8 once before strings enter the ops layer.
- If reader-side (expat config): fix the expat init at the load site.
- If none of the above: pause and reassess scope before continuing.

**A.4 — Test green and commit.** The A.1 fixture now passes. Commit
ordering: A.1 red commit (test alone), then A.3 green commit (fix
plus A.2 note included).

### File touch budget

- New: `tests/cli/roundtrip/test_non_ascii_metadata.cpp` (~80 lines).
- Modify: one production source file. Which file is determined by
  A.2's investigation outcome (writer-side →
  `src/libslic3r/Format/bbs_3mf.cpp`; CLI-input-side →
  `src/cli/main.cpp`; reader-side → the expat init site in
  `bbs_3mf.cpp` loader). Scoped to <50 lines in all branches.
- New: `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md` (~50
  lines).
- Modify: `tests/cli/CMakeLists.txt` to register the new test.

### Risk

If A.2 finds the root cause is in deep libslic3r territory (e.g.,
expat config that affects many BBS code paths beyond the CLI), stop
and reassess scope before A.3. That stop becomes a separate user
check-in, not silent scope creep.

---

## Phase B — Thumbnail passthrough after plate compaction (#2)

### Symptom recap

CLAUDE.md "Open items" lists: "Thumbnail passthrough for plates
whose `plate_index` was compacted after a remove may fall back to
synthesis instead of zero-copy (`src/cli/io.cpp:121-124`)."

### What the code actually does

`src/cli/io.cpp:130-137`:

```cpp
for (size_t i = 0; i < plate_data.size(); ++i) {
    const auto* pd = plate_data[i];
    if (!pd) continue;
    std::string out_key = "Metadata/plate_" + std::to_string(i + 1);
    std::string src_key = "Metadata/plate_" + std::to_string(pd->plate_index + 1);
    passthrough[out_key + ".png"]       = src_key + ".png";
    passthrough[out_key + "_small.png"] = src_key + "_small.png";
}
```

The source key uses `pd->plate_index` (the original loader-assigned
index). If libslic3r preserves `plate_index` across a `plate remove`,
the current code is already correct and the comment is stale. If
libslic3r renumbers `plate_index` to match the new position, the
passthrough silently breaks.

### Tasks

**B.1 — Repro test.** Add
`tests/cli/roundtrip/test_thumbnail_compaction.cpp`:
1. Load `tests/cli/fixtures/test_reference.3mf` (or, if it has fewer
   than three plates, build a multi-plate fixture in-test via
   `plate_add`).
2. Remove a middle plate (e.g., index 1 of [0,1,2]).
3. Save to a temp output via `save_project`.
4. Open the saved archive with miniz; for each surviving plate's
   `Metadata/plate_N.png` and `_small.png`, compare bytes to the
   corresponding entry in the source archive (looked up via the
   source's `plate_index`).
5. Assert byte-identical passthrough (not the placeholder grey
   thumbnail).

**B.2 — Outcome branch.**
- If B.1 passes against current master: the comment in
  `io.cpp:121-124` is stale. Update it to reflect actual behavior.
  Commit test + comment edit in one commit.
- If B.1 fails: investigate whether `plate_index` is renumbered
  post-remove (grep `Slic3r::Model::delete_plate` and related). Most
  likely fix: capture each plate's source-key at load time into a
  parallel data structure on `ProjectState`, then look up by position
  at save time instead of relying on `plate_index` being stable.

### File touch budget

- New: `tests/cli/roundtrip/test_thumbnail_compaction.cpp` (~60 lines).
- Possibly modify: `src/cli/io.cpp` (10–30 lines depending on outcome).
- Possibly modify: `src/cli/project_ops.hpp` if source-keys need
  persistence across mutations.
- Modify: `tests/cli/CMakeLists.txt` to register the new test.

### Risk

The `test_reference.3mf` fixture may have only one plate. If so, B.1
constructs a multi-plate fixture in-test via existing `plate_add`
ops rather than baking yet another binary into the repo.

---

## Phase C — Invariant guard unit tests with corrupted fixtures (#8)

### Goal

Pin the behavior of `check_auxiliary_passthrough` and
`check_cover_references_resolve` against realistic corruption
patterns, so future regressions in `store_bbs_3mf` or in the guards
themselves are caught.

### Strategy

Unit-style, in-process. Build a known-good archive via `save_project`,
then mutate the archive on disk to violate one invariant, then re-run
the guard against the mutated archive and assert it fails with the
expected `failed_check` string. (Subprocess e2e was considered and
rejected: save→guard is atomic, so the realistic failure modes come
from external corruption or future regressions in `store_bbs_3mf`,
which unit-level tests catch just as well.)

### Tasks

**C.1 — Corrupted-archive helpers.** Add to
`tests/cli/unit/unit_helpers.{hpp,cpp}`:

- `mutate_archive_remove_entry(archive_path, entry_name)`: opens the
  archive via miniz, copies all entries except the named one into a
  new archive, atomically swaps. Used for "metadata points at file
  that no longer exists" cases.
- `mutate_archive_add_extra(archive_path, entry_name, bytes)`: opens
  via miniz, appends one extra entry. Used for "saved archive has an
  aux file not present in temp dir" cases.

**C.2 — `check_cover_references_resolve` corruption cases.** Append
to `tests/cli/unit/test_invariant_cover_references.cpp`:

1. Save with both covers populated → remove
   `Model Pictures/cover_sample.jpg` from the archive → guard fails
   with `failed_check == "cover_references_resolve"`, error message
   names the missing file.
2. Same as 1 but remove `Profile Pictures/cover_original.jpg`.
3. Save with only designer cover → mutate the archive's model XML to
   inject a dangling `ProfileCover` metadata pointer → guard fails.

**C.3 — `check_auxiliary_passthrough` corruption cases.** Append to
`tests/cli/unit/test_invariant_aux_passthrough.cpp`:

1. Save with a known aux file set → externally add an extra file to
   the archive after save → guard fails with `failed_check ==
   "auxiliary_passthrough"`, error message names the extra file.
2. Save with known aux files → externally remove one aux file → guard
   fails with `auxiliary_passthrough`, error names the missing file.
3. Save with no aux files → externally add one → guard fails.

### File touch budget

- Modify: `tests/cli/unit/unit_helpers.{hpp,cpp}` (~80 lines added).
- Modify: `tests/cli/unit/test_invariant_cover_references.cpp` (~70
  lines added).
- Modify: `tests/cli/unit/test_invariant_aux_passthrough.cpp` (~70
  lines added).

### Risk

If the archive-mutation helpers exceed ~120 lines combined, factor
them into `tests/cli/unit/archive_mutators.{hpp,cpp}` for clarity.
Decision made at coding time, not pre-committed.

---

## Phase D — Negative-path cover image tests (#9)

### Goal

Pin current behavior of `is_png_or_jpeg` and the
`embed_image_into_folder` path against malformed inputs, so future
regressions in either are caught. No new validation logic — this
phase is purely test additions documenting what is accepted today.

### Tasks

**D.1 — Signature-level negative cases.** Append to
`tests/cli/unit/test_image_signature.cpp`:

1. Zero-byte file → rejected.
2. File too short to hold a signature (1, 2, 7 bytes of zeros) →
   rejected.
3. PNG signature (`89 50 4E 47 0D 0A 1A 0A`) followed by random
   garbage → **accepted** (signature check only — document this).
4. JPEG SOI (`FF D8 FF`) followed by random garbage → **accepted**
   (same).
5. File starting with valid PNG signature but containing embedded NUL
   bytes mid-stream → **accepted** (same).
6. File starting with `MZ` (DOS exe header) → rejected.
7. File starting with `%PDF` → rejected.
8. File with PNG signature minus the last byte → rejected (signature
   must be complete).

**D.2 — Embed-path negative cases.** Append to
`tests/cli/unit/test_cover_decoupling.cpp`:

1. `info_set` with `cover_path` pointing at a non-existent file →
   throws.
2. `info_set` with `cover_path` pointing at a zero-byte file → throws
   (signature check fails).
3. `info_set` with `cover_path` pointing at a directory (not a file)
   → throws.
4. `info_set` with `cover_path` pointing at a valid PNG, file
   unreadable due to permissions (Windows-conditional via
   `#ifdef _WIN32`, skip if can't set up) → throws.
5. `info_set` succeeding with a tiny known-good PNG (the
   `make_placeholder_png_128` output is a convenient choice — already
   used elsewhere in tests) → `cover_file` populated, on-disk file
   byte-identical to source. Sanity baseline for the happy path.

Each test asserts the *thrown exception type* (not just "throws") so
future changes that swap exception types get caught.

### File touch budget

- Modify: `tests/cli/unit/test_image_signature.cpp` (~80 lines
  added).
- Modify: `tests/cli/unit/test_cover_decoupling.cpp` (~60 lines
  added).
- No new fixtures (all malformed inputs generated inline as byte
  vectors written to temp files).

---

## Cross-cutting

### Testing strategy

- Every phase ends with `cli_tests` fully green (no skipped, no
  known-fail).
- Final assertion count strictly greater than the current
  baseline of **268 cases / 1324 assertions**.
- TDD red→green observable for every fix: red commit (test added,
  fails) precedes green commit (fix applied, passes). For pure
  test-addition phases (C, D), one commit per logical group is fine
  since there is no "fix" to separate.

### Commit policy

- No `--amend` on prior commits — every change is a new commit.
- No `--no-verify`, no skipped hooks.
- No push to remote during execution.
- No fixture deletion or regeneration without a confirmation
  checkpoint.
- One commit per task, except where TDD splits a task into red+green
  (Phases A and B may produce two commits each).

### Stop conditions

Any of the following halts execution and triggers a user check-in:

1. Phase A.2 investigation finds the root cause is in libslic3r
   territory affecting non-CLI code paths (broader blast radius than
   expected).
2. Phase B.1 repro test passes — confirm whether to just update the
   stale comment or dig deeper before closing the phase.
3. Phase C archive-mutation helpers exceed ~120 lines — decide
   whether to factor into a new file before continuing.
4. Any phase produces a test that fails for a reason different from
   what was predicted in the spec.

### Exit criteria

- All four phases committed.
- `cli_tests` green with assertion count greater than baseline.
- No new documentation files except
  `docs/cli/notes/2026-05-27-non-ascii-metadata-bug.md` (Phase A
  only).

### Manual GUI smoke

Not required for this work. Phase A may produce a Bambu-Studio-worthy
fixture if the writer-side fix changes how metadata serializes, but
that is a nice-to-have sign-off, not a gating step. Decision deferred
to phase end.
