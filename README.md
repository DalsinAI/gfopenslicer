# GFOpenSlicer

GFOpenSlicer 0.1.0-rc1 slices an STL to G-code on AmigaOS 3.2. v0.2.0-rc1 adds the solid editor on OpenGPU, and the browser bench in `preview/`. The window is
GFOpenLayout. The same engine runs from the shell, and the host tests run it
with no Amiga.

![The window, before a file is sliced](docs/window-empty.png)

![A 10 mm cube, three faces](docs/window-sliced.png)

Open asks for a file. Slice reads the fields and writes G-code next to the
STL, with `.gcode` in place of `.stl`. The list is one row a layer. The pane
is the solid, the way Cura and OctoPrint show a model: three faces of it,
shaded. Drag turns it. Shift-drag pans. `gs_cam_zoom` zooms.

The triangles are one scene. The slicer window still paints that scene with
the CPU drawer. The editor submits the same triangles to
`gfopengpu.library` as `OGPU_OP_TRIANGLES`, with a Z32 depth, on a retained
surface, when the library is open. The pen drawer is only the fallback.
We already have OpenGPU. This tree does not grow a second GPU.

From the shell:

```
gfopenslicer in.stl out.gcode LAYER=0.2 INFILL=20 LINE=0.4 FILAMENT=1.75 HOTEND=200 BED=60 SKIRT=1
```

## Where the view comes from

Cura, PrusaSlicer, OrcaSlicer and OctoPrint are AGPL-3.0. Their viewports
are the picture this is aiming at, and none of that code is in here.

The camera we did take is [orbit-camera](https://github.com/mikolalysenko/orbit-camera)
(Mikola Lysenko, MIT, 2013): a point, a turn and a distance. A drag turns.
Shift-drag moves the point in the plane of the screen, and the move is
larger when the camera is further away. `gs_cam_zoom` changes the distance.
Ours keeps Z up, a turntable, which is what a bed wants. A free tumble would
roll the printer.

Also MIT, and not copied: [meshview](https://github.com/fogleman/meshview) is
that camera as an arcball on a GPU, in Go.
[gcode-viewer](https://github.com/aligator/gcode-viewer) draws a toolpath as
a mesh. That is the layer lines, next, not this release.

## Libraries

Use a library we already have. Write a new one only when we cannot take one
under MIT, BSD, Apache or BSL.

`gfopenlayout.library` is the window. `gfopengpu.library` is the chip and
its CPU back end: one batch, one retained surface. ASL is the file
requester. Cura, PrusaSlicer and OrcaSlicer are the picture to aim at, and
their code stays out. The booleans are ours, not a translation of JSCAD.

## What a slice does

It reads a binary STL or an ASCII one. A binary file is little-endian, which
is how a desktop saves it, so the same file loads on the 68000. Vertices that
land on each other are welded, empty triangles are dropped, and the outside
is turned out.

Each layer is the triangles cut by a plane. The pieces are joined into loops.
The outline is one wall. Infill is even-odd lines at 45 degrees. The first
layer can have a rectangular skirt. Extrusion comes from the layer height,
the line width and the filament diameter. A hotend or a bed of 0 leaves that
temperature command out.

The solid editor is `src/make.c`. It starts from a 20 mm cube with a
cylinder cut out, on a 60 mm plate. Cube and Cylinder add a second part.
Join, Cut and Intersect combine the selected part with the other one.
Move, Rotate and Scale drag the selected part. Drop, Centre and Lay flat
place it. Open, Save and Undo are there. The picture is fitted to the
plate, so a move is visible.

## Preview bench

`preview/` is the browser bench. It is ahead of `src/make.c`. It is in this tree so that work is not only on one machine.

Add drops a block or a round on a 220 mm bed. On stands that shape on the selected part and joins it. Cut takes the height off the top. Place uses the W, D and H fields. A round uses W as the diameter. Dragging still draws the footprint. Pull and Chamfer move the face you clicked. A washer, a slot, a line and a ring are there. Join, cut, intersect and drill stop at 2,500 triangles.

The meshes in `preview/library/` are the shelf. The NASA ones are public domain. The Utah teapot is CC0. The wrench and the spacecraft are too heavy to cut. The Amiga window does not have these tools yet.

## Not in this release

One wall, not several. The skirt is a rectangle, not the outline. There is no
brim, no support, and no start or end G-code of your own. The view is the solid. It does not yet lay the toolpaths back onto the
model.

## Building

The host check:

```
make test
```

The Amiga program is `src/amiga.c`. It needs `gfopenlayout.library` and the
link library that goes with it:

```
m68k-amigaos-gcc -O2 -std=c99 -noixemul -Iinclude -o GFOpenSlicer \
    src/amiga.c src/job.c src/stl.c src/slice.c -lgfopenlayout -lm
```

The pictures above are the real GFOpenLayout draw list, painted on the host.
`make preview` rebuilds that tool when GFOpenLayout is checked out beside
this tree (`LAYOUT=` if it is not).
