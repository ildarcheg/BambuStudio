# Thumbnail passthrough refactor — eliminating rewrite_thumbnails

Date: 2026-07-15
Status: accepted (Option B), implemented same day.
Context: audit finding M5 in
`docs/cli/notes/2026-07-15-port-isolation-audit.md` — `rewrite_thumbnails`
(`src/cli/io.cpp`) is the single genuine breach of the "all format writes
go through libslic3r" architecture: after `store_bbs_3mf` it rebuilds the
entire output zip entry-by-entry with raw miniz to swap thumbnail entries,
making the CLI owner of entry ordering, compression flags, and zip64
behavior, and hardcoding the `Metadata/plate_<N>` naming convention
(rebase-risk #4 in the audit).

## Question

Can `store_bbs_3mf` take real thumbnail bytes directly, so the CLI never
touches the written archive?

## Findings (source-verified, v02.07.01.62)

1. **What the loader provides.** After `load_bbs_3mf`, each
   `PlateData::plate_thumbnail.pixels` holds the **raw PNG bytes** of the
   source thumbnail — the loader extracts the archive entry verbatim into
   the pixels vector (`bbs_3mf.cpp:1674`; `width`/`height` stay 0, so
   `is_valid()` is false). `PlateData::thumbnail_file` holds the
   *in-archive entry name* (e.g. `Metadata/plate_1.png`), not a
   filesystem path.

2. **The exporter's two thumbnail paths** (`bbs_3mf.cpp:6305-6360`):
   - **In-memory:** a valid `ThumbnailData` (RGBA) in
     `StoreParams::thumbnail_data[i]` is PNG-encoded and written as
     `Metadata/plate_<i+1>.png`, and — with `generate_small_thumbnail` —
     a downscaled `plate_<i+1>_small.png` is derived and written too
     (`_add_thumbnail_file_to_archive`, `bbs_3mf.cpp:6715`).
   - **File-based fallback:** if no valid in-memory data was passed for
     index i and `plate_data->thumbnail_file` exists **on disk**, the
     file's bytes are added verbatim as `plate_<i+1>.png`
     (`_add_file_to_archive`) — but **no `_small.png` is written** on
     this path. Since the loader stores an entry name, not a path, this
     branch never fires for CLI-loaded projects.

3. **Orientation:** the exporter encodes with the vertical-flip flag set
   (`tdefl_write_image_to_png_file_in_memory_ex(..., flip=1)`,
   `bbs_3mf.cpp:6720`) because GUI thumbnails come from an OpenGL
   framebuffer (bottom-up rows). RGBA handed to the exporter must
   therefore be stored **bottom-up** to come out upright.

4. **Decoding is available in libslic3r:** `png::decode_colored_png`
   (`src/libslic3r/PNGReadWrite.hpp`, libpng-backed, already linked into
   `libslic3r`) decodes RGB/RGBA PNGs to rows/cols/bytes-per-pixel.

5. **The loader's pixels are only populated for sliced projects**
   (discovered empirically during implementation): the extraction at
   `bbs_3mf.cpp:1674` runs while merging `slice_info.config` plate
   entries. An unsliced project (e.g. the committed `reference.3mf`
   fixture, whose slice_info has a header only) never sets
   `thumbnail_file`, so `plate_thumbnail.pixels` stays empty even though
   `Metadata/plate_1.png` exists in the archive. Option B therefore
   needs a **read-only source-archive fallback**: at save time, if the
   plate carries no PNG bytes, read `Metadata/plate_<plate_index+1>.png`
   from `state.source_path` (plain miniz *reader*, same as the invariant
   guard uses) and decode that. Reading the source archive is not an
   architecture breach — M5 was about owning the *write* path.

## Options

- **A. File-based passthrough** — write the source PNG bytes to a temp
  file, point `thumbnail_file` at it, pass invalid `ThumbnailData`.
  Byte-identical big thumbnail, zero re-encode. **Rejected:** the
  file-based path writes no `plate_N_small.png`, so every saved project
  would lose its small thumbnails (which the GUI and our own invariant
  guard require). Keeping a mini-rewrite just for `_small` defeats the
  purpose.
- **B. Decode → real ThumbnailData (chosen)** — obtain the source PNG
  bytes (from `plate_thumbnail.pixels` when the loader filled them, else
  by reading `Metadata/plate_<plate_index+1>.png` from the source
  archive — see finding 5), decode to RGBA (bottom-up), pass as
  `StoreParams::thumbnail_data[i]`. The exporter writes both entries
  through its own canonical path — the exact code the GUI uses. Plates
  with no source thumbnail (or undecodable bytes) keep today's gray
  placeholder RGBA. `rewrite_thumbnails` is deleted outright.
- **C. Status quo** — keep the rewrite. Rejected: it is the last
  architecture breach and the top silent-divergence risk for rebases.

## Consequences of B

- `plate_N.png` is **content-identical** (PNG is lossless; pixels match
  the source exactly) but not **byte-identical** (re-encoded by the
  exporter's deterministic tdefl encoder; stable from the first re-encode
  onward). The e2e passthrough pins change from byte-equality to
  decoded-pixel equality.
- `plate_N_small.png` is now *derived from the big thumbnail* by the
  exporter instead of passed through from the source archive — small and
  big can no longer disagree.
- ~90 lines of CLI-owned raw-miniz write logic disappear, along with the
  CLI's knowledge of `Metadata/plate_<N>` naming (audit rebase-risk #4)
  and the `.pass_tmp` scratch file.
- The audit's "thumbnail passthrough may fall back to synthesis for
  compacted plate_index" caveat (io.cpp:121-124, CLAUDE.md open item)
  disappears: content now travels *with the plate* in `plate_thumbnail`,
  not via index-keyed archive lookups.
