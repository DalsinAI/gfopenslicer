# GFOpenSlicer

GFOpenSlicer 0.1.0-rc1 slices an STL to G-code on AmigaOS 3.2. The window is
GFOpenLayout. The same engine runs from the shell, and the host tests run it
with no Amiga.

![The window, before a file is sliced](docs/window-empty.png)

![A 10 mm cube, three faces](docs/window-sliced.png)

Open asks for a file. Slice reads the fields and writes G-code next to the
STL, with `.gcode` in place of `.stl`. The list is one row a layer. The pane
is the solid, the way Cura and OctoPrint show a model: three faces of it,
shaded. Drag turns it. Shift-drag pans. `gs_cam_zoom` zooms.

The triangles are one scene. A chip would draw them through
`gfopengpu.library` when `OGPU_ONGPU` is set. That hand-off is not wired.
This machine has no chip, so the CPU drawer paints the scene, and that is
the picture below.

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

## Not in this release

One wall, not several. The skirt is a rectangle, not the outline. There is no
brim, no support, and no start or end G-code of your own. The view is the solid. It does not yet lay the toolpaths back onto the
model, and it does not let you move a face to the bed the way Cura does.

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
