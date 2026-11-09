# Bench notes

`preview/` is the browser bench. `src/make.c` is the Amiga window. The bench is ahead. These notes are the state after 9 November 2026, on top of tag `v0.2.0-rc1`.

The Amiga editor opens, saves and undoes. Move, rotate and scale drag the part. Drop, Centre and Lay flat put it on a 60 mm plate. Cube and Cylinder add a part. Join, cut and intersect combine two. The picture is an `OGPU_OP_TRIANGLES` batch with a Z32 depth on a retained surface. The pen drawer is the fallback. We already have OpenGPU.

The bench bed is 220 mm, Z up. Twenty-four parts. Booleans, drill, and split stop at 2,500 triangles. The NASA wrench and the spacecraft are public domain and are too heavy to cut. The Utah teapot is CC0.

What the bench can do, and what was checked:

- Add, On and Cut. Add sits on the bed. On joins a boss onto the selected part. Cut takes the height off the top. A 20 × 14 × 8 mm boss on a 24 mm box made it 24 mm tall.
- Place uses W, D and H. A round uses W as the diameter. Dragging still draws the footprint.
- Keyway is a slot. 28 × 8 × 8 mm sat on the bed. An 18 × 6 mm slot cut a pocket in a box.
- Pull and Chamfer move the face you clicked.
- Drill, bolt circle, row, counterbore and a 90° countersink. Four Ø6 mm holes on a Ø16 mm circle. Three Ø4 mm holes, 8 mm apart. Counterbore was Ø6 × Ø12, 4 mm deep. Countersink was Ø6 × Ø12.
- Split keeps both halves, at the section or at mid-height. A 16 mm box became two 8 mm slabs.
- Washer, slot, line, ring, mirror, copy, stack, solo, section, measure. Drop, Centre, Lay flat.

Not in the Amiga window yet: extrude, keyway, pull, chamfer, the hole tools, and split. The slicer is still the 0.1.0-rc1 slice. One wall, 45° infill, a rectangular skirt.

Commits on this work are AmiCradle. SacredTrees is the other name that belongs on this repo.
