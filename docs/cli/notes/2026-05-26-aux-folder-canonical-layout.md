# Canonical Aux Folder Layout (post-2026-05-26)

## What the canonical layout is

Bambu Studio writes auxiliary content under `Auxiliaries/` using these
exact subdir names, with spaces and TitleCase:

| Subdir              | Holds                                              | Metadata reference         |
|---------------------|----------------------------------------------------|----------------------------|
| `Model Pictures`    | Model preview images (PNG/JPEG)                    | `DesignerCover` (basename) |
| `Profile Pictures`  | Profile preview images (PNG/JPEG)                  | `ProfileCover`  (basename) |
| `Bill of Materials` | BOM documents                                      | n/a                        |
| `Assembly Guide`    | Assembly/print guide documents (PDF, txt, etc.)    | n/a                        |
| `Others`            | Anything else                                      | n/a                        |
| `.thumbnails`       | Cover-thumbnail PNGs at 3MF/middle/small sizes     | from `_rels/.rels`         |

Sources of truth:
- `src/slic3r/GUI/Auxiliary.hpp:75` — `s_default_folders` array
- `src/slic3r/GUI/Project.cpp:214-226` — JSON dispatch
- `tests/cli/fixtures/test_reference.3mf` — real Bambu output

## DesignerCover vs ProfileCover (decoupled 2026-05-26)

Before this change, the CLI shared a single `Model Pictures/cover.png`
between `model_info.cover_file` and `profile_info.ProfileCover` via a
refcount in `project_tab_ops.cpp`. That was wrong — Bambu treats them
as fully separate: each lives in its own folder under its own basename.

After this change:
- `project info set --cover X.png|X.jpg` embeds the image at
  `Model Pictures/<basename>` and sets `model_info.cover_file = "<basename>"`.
- `project profile set --cover X.png|X.jpg` does the same for
  `Profile Pictures/` and `profile_info.ProfileCover`.
- `info clear cover` / `profile clear cover` blank the metadata pointer
  only — the on-disk image is left in place as a normal aux entry.
  Users can delete it explicitly via `project aux remove --folder
  model-pictures --name X.jpg`.
- `--cover-name NAME` (new) picks an existing image in the folder as
  the cover without re-embedding. Validation (mutual exclusion with
  `--cover`, `sanitize_aux_name`) happens at the CLI layer; the ops
  layer trusts its inputs.

## Why this matters

Round-tripping any real Bambu-produced 3MF (e.g. the new
`test_reference.3mf` fixture) requires the canonical folder names — if
the CLI re-saves under `Pictures/`, Bambu Studio no longer recognizes
the contents in its Project tab. The `check_auxiliary_passthrough`
invariant guard catches any future regression by comparing the
in-memory aux temp dir against the saved archive on every
`save_project`. (Originally designed as a pre-archive vs post-archive
diff; that false-positived on legitimate `aux remove` / `aux add
--force` mutations and was redesigned to compare against the aux temp
dir during execution — see commit `413187b6a`.)

## Why CLAUDE.md previously got this wrong

The early audit (pre-M1) mis-read which folder names were canonical
and recorded the CLI's then-incorrect layout as a "deliberate
divergence from Orca." The 2026-05-26 audit against
`test_reference.3mf` corrected this. The current `CLAUDE.md` divergence
note points here.
