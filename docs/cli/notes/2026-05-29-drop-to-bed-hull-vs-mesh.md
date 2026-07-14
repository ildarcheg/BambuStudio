# drop-to-bed: convex hull vs full mesh (2026-05-29)

`plate_drop_to_bed` and `object_auto_orient` iterate
`ModelVolume::get_convex_hull().its.vertices` to compute world-space
min-Z, not the full mesh's vertex set. This note exists to forestall
the obvious "fix" of switching to full mesh.

## Why hull, not mesh

The lowest-Z vertex of a transformed mesh is always an extreme point and
therefore on the convex hull. So min-Z over hull verts == min-Z over
mesh verts. **Mathematically identical.**

## Why it matters for the CLI

Hulls are typically 10–100 vertices. Full STL meshes can be ~100K
vertices. Headless batch composition is bambu-cli's primary use case;
dropping a plate of N instances iterates N × verts. The 1000× constant
factor is the whole reason.

## Why the GUI does it the same way

`src/slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp:36-50` uses
`ModelVolume::get_convex_hull()` for the same calculation in the
gizmo's "drop to bed" button. The CLI matches.

## Why per-volume world matrix, not per-instance

The GUI uses `GLVolume::world_matrix()` which composes
`instance.transformation × volume.transformation`. The CLI does the
same (`inst_m * mv->get_transformation().get_matrix()`). Missing the
volume transform mis-drops multi-volume objects loaded from a
multi-part 3MF — the volumes have their own per-part offsets, and
treating them all as if they had the instance's offset puts non-primary
volumes at the wrong Z.

## Verifying

The unit test
`plate_drop_to_bed: rotated instance — uses world matrix not naive bbox`
(in `tests/cli/unit/test_plate_layout.cpp`) pins the rotation case.
For multi-volume coverage, see the `object_auto_orient: N instances
across two plates` test which exercises the same code path.
