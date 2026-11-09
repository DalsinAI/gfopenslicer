import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { TransformControls } from "three/addons/controls/TransformControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
import { cylinder, cube, intersect, subtract, union, volume } from "./gs.js";
import { BED, BOOLEAN_LIMIT, METALS } from "./library";
import { makeSolid, SOLID_LABEL, stadium, type SolidKind } from "./solids";
import { encodeSTL, loadBuffer, normalizeUnits, parseSTL, thinTriangles } from "./stl";

export type Tool = "translate" | "rotate" | "scale";
export type Sketch = "rect" | "circle" | "slot";
export type SketchOp = "add" | "cut" | "top";
export type Shade = "metal" | "clay" | "wire";
export type BoolOp = "union" | "subtract" | "intersect";

export type PartInfo = {
  id: string;
  name: string;
  color: string;
  tris: number;
  visible: boolean;
  selected: boolean;
  size: [number, number, number];
  pos: [number, number, number];
  rot: [number, number, number];
  scale: [number, number, number];
  volume: number;
  offBed: boolean;
};

export type BenchSnapshot = {
  parts: PartInfo[];
  status: string;
  tool: Tool;
  shade: Shade;
  snap: boolean;
  measuring: boolean;
  stockName: string | null;
  cutterName: string | null;
  face: number | null;
  busy: boolean;
  section: boolean;
  sectionZ: number;
  sectionMax: number;
  sketch: Sketch | null;
  sketchHeight: number;
  sketchOp: SketchOp;
  pulling: boolean;
};

type Part = {
  id: string;
  name: string;
  color: string;
  mesh: THREE.Mesh;
  edges: THREE.LineSegments | null;
  volume: number;
};

type SnapPart = {
  id: string;
  name: string;
  color: string;
  visible: boolean;
  pos: [number, number, number];
  quat: [number, number, number, number];
  scale: [number, number, number];
  positions: Float32Array;
};

type Snap = { parts: SnapPart[]; selectedId: string | null };

const EMPTY: BenchSnapshot = {
  parts: [],
  status: "Opening the bench…",
  tool: "translate",
  shade: "metal",
  snap: false,
  measuring: false,
  stockName: null,
  cutterName: null,
  face: null,
  busy: false,
  section: false,
  sectionZ: 12,
  sectionMax: 40,
  sketch: null,
  sketchHeight: 8,
  sketchOp: "add",
  pulling: false,
};

function deg(r: number) {
  return Math.round(THREE.MathUtils.radToDeg(r) * 10) / 10;
}

function round1(n: number) {
  return Math.round(n * 10) / 10;
}

export class Bench {
  snapshot: BenchSnapshot = { ...EMPTY };
  private readonly listeners = new Set<(s: BenchSnapshot) => void>();
  private readonly renderer: THREE.WebGLRenderer;
  private readonly scene = new THREE.Scene();
  private readonly camera: THREE.PerspectiveCamera;
  private readonly orbit: OrbitControls;
  private readonly transform: TransformControls;
  private readonly raycaster = new THREE.Raycaster();
  private readonly parts: Part[] = [];
  private readonly undoStack: Snap[] = [];
  private readonly redoStack: Snap[] = [];
  private readonly bedPlane = new THREE.Plane(new THREE.Vector3(0, 0, 1), 0);
  private readonly measureHits: THREE.Vector3[] = [];
  private readonly measureGroup = new THREE.Group();
  private faceMark: THREE.Mesh;
  private selectedId: string | null = null;
  private face: number | null = null;
  private tool: Tool = "translate";
  private shade: Shade = "metal";
  private snap = false;
  private measuring = false;
  private status = "180 mm is a small plate. This bed is 220 mm. Z is up.";
  private busy = false;
  private dead = false;
  private serial = 1;
  private colorCursor = 0;
  private userMoved = false;
  private pointer: { x: number; y: number; moved: number } | null = null;
  private sketch: Sketch | null = null;
  private sketchHeight = 8;
  private sketchOp: SketchOp = "add";
  private sketchDrag: { x: number; y: number } | null = null;
  private pulling = false;
  private pullDrag: {
    localN: THREE.Vector3;
    worldN: THREE.Vector3;
    origin: THREE.Vector3;
    indices: number[];
    base: Float32Array;
    anchor: number;
    scale: number;
  } | null = null;
  private pickPoint: THREE.Vector3 | null = null;
  private readonly sketchPreview: THREE.Mesh;
  private sectionOn = false;
  private sectionZ = 12;
  private readonly clipPlane = new THREE.Plane(new THREE.Vector3(0, 0, -1), 12);
  private readonly sectionSheet: THREE.Mesh;

  constructor(canvas: HTMLCanvasElement, onChange: (s: BenchSnapshot) => void) {
    this.listeners.add(onChange);
    this.renderer = new THREE.WebGLRenderer({
      canvas,
      antialias: true,
      alpha: false,
      powerPreference: "high-performance",
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.08;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFShadowMap;

    this.scene.background = new THREE.Color("#14161c");
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    pmrem.dispose();

    this.camera = new THREE.PerspectiveCamera(38, 1, 0.15, 5000);
    this.camera.up.set(0, 0, 1);
    this.camera.position.set(160, -210, 130);

    this.buildRoom();

    this.sectionSheet = new THREE.Mesh(
      new THREE.PlaneGeometry(BED - 6, BED - 6),
      new THREE.MeshBasicMaterial({
        color: "#e8b84a",
        transparent: true,
        opacity: 0.16,
        side: THREE.DoubleSide,
        depthWrite: false,
      }),
    );
    this.sectionSheet.visible = false;
    this.sectionSheet.raycast = () => {};
    this.sectionSheet.renderOrder = 2;
    this.scene.add(this.sectionSheet);

    this.orbit = new OrbitControls(this.camera, canvas);
    this.orbit.target.set(0, 0, 16);
    this.orbit.enableDamping = true;
    this.orbit.dampingFactor = 0.08;
    this.orbit.screenSpacePanning = true;
    this.orbit.minPolarAngle = 0.04;
    this.orbit.maxPolarAngle = Math.PI - 0.04;
    this.orbit.mouseButtons.RIGHT = THREE.MOUSE.PAN;
    this.orbit.mouseButtons.MIDDLE = THREE.MOUSE.PAN;
    this.orbit.addEventListener("start", () => {
      this.userMoved = true;
    });

    this.transform = new TransformControls(this.camera, canvas);
    this.transform.setMode("translate");
    this.scene.add(this.transform.getHelper());
    this.transform.addEventListener("dragging-changed", (ev) => {
      const dragging = Boolean((ev as { value?: boolean }).value);
      this.orbit.enabled = !dragging;
      if (dragging) this.pushUndo();
      else this.emit();
    });
    this.transform.addEventListener("objectChange", () => this.emit());

    this.faceMark = new THREE.Mesh(
      new THREE.BufferGeometry(),
      new THREE.MeshBasicMaterial({
        color: "#f4e8c8",
        side: THREE.DoubleSide,
        polygonOffset: true,
        polygonOffsetFactor: -4,
        polygonOffsetUnits: -4,
      }),
    );
    this.faceMark.visible = false;
    this.faceMark.raycast = () => {};
    this.scene.add(this.measureGroup);
    this.sketchPreview = new THREE.Mesh(
      new THREE.BoxGeometry(1, 1, 1),
      new THREE.MeshStandardMaterial({
        color: "#e8b84a",
        transparent: true,
        opacity: 0.42,
        roughness: 0.35,
        metalness: 0.25,
        depthWrite: false,
      }),
    );
    this.sketchPreview.visible = false;
    this.sketchPreview.raycast = () => {};
    this.scene.add(this.sketchPreview);

    canvas.addEventListener("pointerdown", this.onPointerDown);
    canvas.addEventListener("pointermove", this.onPointerMove);
    canvas.addEventListener("pointerup", this.onPointerUp);
    window.addEventListener("resize", this.resize);
    this.resize();
    this.renderer.setAnimationLoop(() => {
      this.orbit.update();
      this.renderer.render(this.scene, this.camera);
    });
    this.emit();
  }

  subscribe(fn: (s: BenchSnapshot) => void) {
    this.listeners.add(fn);
    fn(this.snapshot);
    return () => this.listeners.delete(fn);
  }

  async boot() {
    this.setStatus("Reading the NASA wrench…");
    try {
      const buf = await loadBuffer("/library/nasa-wrench.stl");
      if (this.dead) return;
      this.addPositions(parseSTL(buf), "NASA wrench", true);
      if (!this.userMoved) this.frame(true);
      this.setStatus(
        this.describeSelection(
          "NASA wrench is on the bed. Block or Round, on the left, extrudes a solid you drag on the plate.",
        ),
      );
    } catch {
      if (!this.dead) this.addSolid("box");
    }
  }

  async openUrl(url: string, name: string) {
    if (this.busy) return;
    this.setStatus(`Reading ${name}…`);
    try {
      const buf = await loadBuffer(url);
      if (this.dead) return;
      const parsed = parseSTL(buf);
      if (this.parts.length >= 24) {
        this.setStatus("Twenty-four parts is the limit on this bench.");
        return;
      }
      this.pushUndo();
      this.addPositions(parsed, name, true);
      this.frame(false);
    } catch {
      this.setStatus(`Could not open ${name}.`);
    }
  }

  openBuffer(buf: ArrayBuffer, name: string) {
    let parsed: Float32Array;
    try {
      parsed = parseSTL(buf);
    } catch {
      this.setStatus("That file is not an STL this bench can read.");
      return;
    }
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    this.pushUndo();
    this.addPositions(parsed, name.replace(/\.stl$/i, "") || "STL", true);
    this.frame(false);
  }

  addSolid(kind: SolidKind) {
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    this.pushUndo();
    const tris = makeSolid(kind);
    const part = this.makePart(tris, SOLID_LABEL[kind]);
    this.seat(part, false);
    this.park(part);
    this.selectedId = part.id;
    this.face = null;
    this.attachGizmo();
    this.frame(false);
    this.setStatus(`${part.name} is on the bed.`);
  }

  select(id: string) {
    if (!this.parts.some((p) => p.id === id)) return;
    this.selectedId = id;
    this.face = null;
    this.hideFace();
    this.attachGizmo();
    this.emit();
  }

  toggleVisible(id: string) {
    const part = this.parts.find((p) => p.id === id);
    if (!part) return;
    part.mesh.visible = !part.mesh.visible;
    if (part.edges) part.edges.visible = part.mesh.visible && this.shade !== "wire";
    if (!part.mesh.visible && this.selectedId === id) this.transform.detach();
    this.emit();
  }

  rename(id: string, name: string) {
    const part = this.parts.find((p) => p.id === id);
    if (!part) return;
    const next = name.trim().slice(0, 48);
    if (!next || next === part.name) return;
    this.pushUndo();
    part.name = next;
    this.emit();
  }

  setColor(hex: string) {
    const part = this.current();
    if (!part) return;
    part.color = hex;
    (part.mesh.material as THREE.MeshPhysicalMaterial).color.set(hex);
    this.emit();
  }

  setTool(tool: Tool) {
    this.tool = tool;
    this.measuring = false;
    this.clearMeasure();
    this.leaveSketch(false);
    this.leavePull(false);
    this.transform.setMode(tool);
    this.orbit.enabled = true;
    this.attachGizmo();
    this.emit();
  }

  setShade(shade: Shade) {
    this.shade = shade;
    for (const part of this.parts) this.applyShade(part);
    this.emit();
  }

  setSnap(on: boolean) {
    this.snap = on;
    this.transform.setTranslationSnap(on ? 1 : null);
    this.transform.setRotationSnap(on ? Math.PI / 12 : null);
    this.transform.setScaleSnap(on ? 0.1 : null);
    this.setStatus(on ? "Snap is 1 mm, 15°, and 0.1× scale." : "Snap is off.");
  }

  setMeasuring(on: boolean) {
    this.measuring = on;
    this.clearMeasure();
    if (on) {
      this.leaveSketch(false);
      this.leavePull(false);
      this.transform.detach();
      this.setStatus("Click two points. The brass line is the distance in millimetres.");
    } else {
      this.attachGizmo();
      this.emit();
    }
  }

  setSketch(mode: Sketch | null) {
    if (mode && this.sketch === mode) mode = null;
    const was = this.sketch;
    this.sketch = mode;
    this.measuring = false;
    this.clearMeasure();
    this.sketchDrag = null;
    this.sketchPreview.visible = false;
    if (mode) {
      this.leavePull(false);
      this.transform.detach();
      this.orbit.enableRotate = false;
      const shape =
        mode === "rect" ? "a rectangle" : mode === "slot" ? "a slot, length by width" : "out from the centre of a round";
      const act =
        this.sketchOp === "cut"
          ? `It cuts ${round1(this.sketchHeight)} mm down from the top of the selected part.`
          : this.sketchOp === "top"
            ? `It stands ${round1(this.sketchHeight)} mm on the selected part and joins.`
            : `It extrudes ${round1(this.sketchHeight)} mm.`;
      this.setStatus(`Drag ${shape} on the bed. ${act} Right-drag still pans.`);
      return;
    }
    this.orbit.enableRotate = true;
    this.attachGizmo();
    if (was) this.setStatus("Sketch is off. Move, turn and scale are back.");
    else this.emit();
  }

  setSketchHeight(mm: number) {
    if (!Number.isFinite(mm)) return;
    this.sketchHeight = Math.min(120, Math.max(0.4, mm));
    this.emit();
  }

  setSketchOp(op: SketchOp) {
    this.sketchOp = op;
    const mat = this.sketchPreview.material as THREE.MeshStandardMaterial;
    mat.color.set(op === "cut" ? "#e85d4c" : op === "top" ? "#3e7a45" : "#e8b84a");
    const line =
      op === "cut"
        ? `Cut is on. ${round1(this.sketchHeight)} mm comes off the top of the selected part.`
        : op === "top"
          ? `On is on. ${round1(this.sketchHeight)} mm stands on the selected part and joins.`
          : `Add is on. The sketch extrudes ${round1(this.sketchHeight)} mm onto the bed.`;
    this.setStatus(line);
  }

  place(kind: Sketch, width: number, depth: number) {
    const w = Math.min(200, Math.max(1, Number.isFinite(width) ? width : 10));
    const d = Math.min(200, Math.max(1, Number.isFinite(depth) ? depth : w));
    let cx = 0;
    let cy = 0;
    const part = this.current();
    if (part && this.sketchOp !== "add") {
      part.mesh.updateMatrixWorld(true);
      const c = new THREE.Box3().setFromObject(part.mesh).getCenter(new THREE.Vector3());
      cx = c.x;
      cy = c.y;
    }
    if (kind === "circle") this.commitFoot({ kind, cx, cy, w: w / 2, d: w / 2 });
    else this.commitFoot({ kind, cx, cy, w, d });
  }

  pullBy(mm: number) {
    const part = this.current();
    if (!part || this.face == null) {
      this.setStatus("Click a face, then pull it by a distance.");
      return;
    }
    if (!Number.isFinite(mm) || Math.abs(mm) < 0.05) {
      this.setStatus("Pull needs a distance of at least 0.1 mm.");
      return;
    }
    const moved = this.offsetFace(part, this.face, mm);
    if (!moved) return;
    this.setStatus(`Pulled ${round1(mm)} mm. ${moved} points moved on that face.`);
  }

  chamfer(mm: number) {
    const part = this.current();
    if (!part || this.face == null) {
      this.setStatus("Click a face, then chamfer it.");
      return;
    }
    const d = Math.min(20, Math.max(0.2, Math.abs(mm) || 1));
    const pos = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const tris = Math.floor(pos.count / 3);
    const region = this.regionVerts(part, this.face);
    if (region.verts.length < 3) {
      this.setStatus("That face has nothing to chamfer.");
      return;
    }
    const n0 = region.normal.clone().normalize();
    const onFace = new Uint8Array(tris);
    for (let t = 0; t < tris; t++) {
      let on = true;
      for (let k = 0; k < 3; k++) {
        const i = t * 3 + k;
        if (!region.verts.includes(i)) {
          const dist =
            n0.x * pos.getX(i) + n0.y * pos.getY(i) + n0.z * pos.getZ(i) -
            (n0.x * pos.getX(region.verts[0]!) +
              n0.y * pos.getY(region.verts[0]!) +
              n0.z * pos.getZ(region.verts[0]!));
          if (Math.abs(dist) > 0.4) on = false;
        }
      }
      const n = this.triNormalLocal(pos, t);
      if (n.lengthSq() < 1e-12 || n.normalize().dot(n0) < 0.992) on = false;
      onFace[t] = on ? 1 : 0;
    }
    const near = (i: number, j: number) =>
      Math.hypot(pos.getX(i) - pos.getX(j), pos.getY(i) - pos.getY(j), pos.getZ(i) - pos.getZ(j)) < 0.45;
    const outside = new Map<number, THREE.Vector3>();
    for (let t = 0; t < tris; t++) {
      if (onFace[t]) continue;
      const n = this.triNormalLocal(pos, t);
      if (n.lengthSq() < 1e-8) continue;
      n.normalize();
      for (let k = 0; k < 3; k++) {
        const i = t * 3 + k;
        const hit = region.verts.find((v) => near(v, i));
        if (hit == null) continue;
        const acc = outside.get(hit) ?? new THREE.Vector3();
        acc.add(n);
        outside.set(hit, acc);
      }
    }
    if (!outside.size) {
      this.setStatus("That face has no edge to chamfer.");
      return;
    }
    this.pushUndo();
    const attr = pos;
    const a = attr.array as Float32Array;
    const seen = new Set<number>();
    for (const [i, raw] of outside) {
      const side = raw.clone();
      side.addScaledVector(n0, -side.dot(n0));
      if (side.lengthSq() < 1e-8) continue;
      side.normalize();
      for (let j = 0; j < pos.count; j++) {
        if (seen.has(j) || !near(i, j)) continue;
        seen.add(j);
        a[j * 3] = (a[j * 3] ?? 0) - n0.x * d - side.x * d;
        a[j * 3 + 1] = (a[j * 3 + 1] ?? 0) - n0.y * d - side.y * d;
        a[j * 3 + 2] = (a[j * 3 + 2] ?? 0) - n0.z * d - side.z * d;
      }
    }
    for (const i of region.verts) {
      if (seen.has(i)) continue;
      seen.add(i);
      a[i * 3] = (a[i * 3] ?? 0) - n0.x * d;
      a[i * 3 + 1] = (a[i * 3 + 1] ?? 0) - n0.y * d;
      a[i * 3 + 2] = (a[i * 3 + 2] ?? 0) - n0.z * d;
    }
    attr.needsUpdate = true;
    part.mesh.geometry.computeVertexNormals();
    part.mesh.geometry.computeBoundingBox();
    part.mesh.geometry.computeBoundingSphere();
    this.rebuildEdges(part);
    part.volume = this.localVolume(part);
    this.showFace(part, this.face);
    this.setStatus(`Chamfered that face by ${round1(d)} mm.`);
  }

  setPull(on: boolean) {
    if (on && (this.face == null || !this.current())) {
      this.setStatus("Click a face on the part, then Pull.");
      return;
    }
    if (!on && !this.pulling) return;
    this.pulling = on;
    this.pullDrag = null;
    if (on) {
      this.leaveSketch(false);
      this.measuring = false;
      this.clearMeasure();
      this.transform.detach();
      this.orbit.enableRotate = false;
      this.setStatus("Drag to pull that face along its normal. Escape leaves Pull.");
      return;
    }
    if (!this.sketch) this.orbit.enableRotate = true;
    this.attachGizmo();
    this.setStatus("Pull is off.");
  }

  addWasher() {
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    const outer = cylinder(0, 0, -4, 16, 8, 48);
    const inner = cylinder(0, 0, -7, 7, 16, 36);
    const tris = subtract(outer, inner);
    if (!tris || tris.length < 36) {
      this.setStatus("The washer did not build.");
      return;
    }
    this.pushUndo();
    const part = this.makePart(tris, "Washer");
    this.seat(part, false);
    this.park(part);
    this.selectedId = part.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    this.attachGizmo();
    this.frame(false);
    this.setStatus(`${part.name} is on the bed. A 32 mm washer, 14 mm hole, 8 mm thick.`);
  }

  line(axis: 0 | 1 | 2, count: number, gap: number) {
    const part = this.current();
    if (!part) return;
    const n = Math.max(2, Math.min(8, Math.round(count) || 2));
    const g = Math.max(0, Math.min(80, Number.isFinite(gap) ? gap : 6));
    if (this.parts.length + (n - 1) > 24) {
      this.setStatus("Not enough room in the list for that line.");
      return;
    }
    const size = this.sizeOf(part);
    const step = (size[axis] || 1) + g;
    const letter = "XYZ"[axis];
    this.pushUndo();
    let last = part;
    for (let i = 1; i < n; i++) {
      const copy = this.clonePart(part, 0);
      if (axis === 0) copy.mesh.position.x += step * i;
      else if (axis === 1) copy.mesh.position.y += step * i;
      else copy.mesh.position.z += step * i;
      last = copy;
    }
    this.selectedId = last.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    this.attachGizmo();
    this.setStatus(`${n} of ${part.name} along ${letter}, ${round1(g)} mm apart.`);
  }

  ring(count: number) {
    const part = this.current();
    if (!part) return;
    const n = Math.max(3, Math.min(12, Math.round(count) || 4));
    if (this.parts.length + (n - 1) > 24) {
      this.setStatus("Not enough room in the list for that ring.");
      return;
    }
    part.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(part.mesh);
    const c = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());
    let radius = Math.hypot(c.x, c.y);
    const lifted = radius < Math.max(8, size.x, size.y) * 0.55;
    if (lifted) radius = Math.max(size.x, size.y) * 0.65 + 10;
    const angle0 = Math.atan2(c.y, c.x);
    this.pushUndo();
    let last = part;
    for (let i = 1; i < n; i++) {
      const ang = angle0 + (i * Math.PI * 2) / n;
      const copy = this.clonePart(part, 0);
      copy.mesh.position.x = Math.cos(ang) * radius;
      copy.mesh.position.y = Math.sin(ang) * radius;
      const q = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), (i * Math.PI * 2) / n);
      copy.mesh.quaternion.premultiply(q);
      last = copy;
    }
    this.selectedId = last.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    this.attachGizmo();
    this.setStatus(
      lifted
        ? `Ring of ${n}. The original was on the centre, so the copies sit on a ${round1(radius)} mm circle.`
        : `Ring of ${n} around the bed centre.`,
    );
  }

  setSection(on: boolean) {
    this.sectionOn = on;
    if (on) {
      const box = new THREE.Box3();
      for (const part of this.parts) {
        if (part.mesh.visible) box.expandByObject(part.mesh);
      }
      if (!box.isEmpty()) this.sectionZ = (box.min.z + box.max.z) / 2;
      this.syncSection();
      this.setStatus(`Section at ${round1(this.sectionZ)} mm. Everything above the brass plane is hidden.`);
      return;
    }
    this.syncSection();
    this.setStatus("Section is off. The whole part is back.");
  }

  setSectionZ(z: number) {
    if (!Number.isFinite(z)) return;
    this.sectionOn = true;
    this.sectionZ = Math.max(0, Math.min(this.sectionLimit(), z));
    this.syncSection();
    this.emit();
  }

  nudgeSection(dz: number) {
    if (!this.sectionOn) {
      this.setSection(true);
      return;
    }
    this.setSectionZ(this.sectionZ + dz);
    this.setStatus(`Section at ${round1(this.sectionZ)} mm.`);
  }

  fitExtent(axis: 0 | 1 | 2, mm: number) {
    const part = this.current();
    if (!part || !Number.isFinite(mm) || mm <= 0) return;
    const current = this.sizeOf(part)[axis];
    if (current < 0.05) return;
    if (Math.abs(mm - current) < 0.15) return;
    this.pushUndo();
    part.mesh.scale.multiplyScalar(mm / current);
    part.volume = this.localVolume(part);
    const names = ["X", "Y", "Z"] as const;
    this.setStatus(`${part.name} scaled evenly so ${names[axis]} is ${round1(mm)} mm.`);
  }

  stack() {
    const part = this.current();
    const base = this.stock();
    if (!part || !base || part === base) {
      this.setStatus("Stack needs the part under the selection as a base.");
      return;
    }
    this.pushUndo();
    part.mesh.updateMatrixWorld(true);
    base.mesh.updateMatrixWorld(true);
    const a = new THREE.Box3().setFromObject(part.mesh);
    const b = new THREE.Box3().setFromObject(base.mesh);
    const ac = a.getCenter(new THREE.Vector3());
    const bc = b.getCenter(new THREE.Vector3());
    part.mesh.position.x += bc.x - ac.x;
    part.mesh.position.y += bc.y - ac.y;
    part.mesh.position.z += b.max.z - a.min.z;
    this.setStatus(`${part.name} is stacked on ${base.name}.`);
  }

  solo() {
    const part = this.current();
    if (!part) return;
    const others = this.parts.filter((p) => p !== part);
    if (!others.length) {
      this.setStatus(`${part.name} is the only part.`);
      return;
    }
    const hiding = others.some((p) => p.mesh.visible);
    this.pushUndo();
    if (hiding) {
      for (const other of others) other.mesh.visible = false;
      part.mesh.visible = true;
      this.setStatus(`Only ${part.name} is showing.`);
    } else {
      for (const other of this.parts) other.mesh.visible = true;
      this.setStatus("Every part is showing.");
    }
    this.attachGizmo();
  }

  async drill(diameter: number) {
    const stock = this.current();
    if (!stock || this.busy) return;
    const d = Math.min(80, Math.max(0.5, diameter || 6));
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    const box = new THREE.Box3().setFromObject(stock.mesh);
    const h = Math.max(6, box.max.z - box.min.z + 8);
    const aimed = this.face != null && this.pickPoint;
    const cx = aimed ? this.pickPoint!.x : (box.min.x + box.max.x) / 2;
    const cy = aimed ? this.pickPoint!.y : (box.min.y + box.max.y) / 2;
    const z0 = Math.min(box.min.z, 0) - 4;
    this.pushUndo();
    const tool = this.makePart(cylinder(0, 0, 0, d / 2, h, 36), `Drill Ø${round1(d)}`, "#c47a5a");
    tool.mesh.position.set(cx, cy, z0);
    this.selectedId = tool.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    this.attachGizmo();
    if (this.triCount(stock) > BOOLEAN_LIMIT || this.triCount(tool) > BOOLEAN_LIMIT) {
      this.setStatus(
        `Ø${round1(d)} mm drill is aimed through ${stock.name}. That mesh is over the ${BOOLEAN_LIMIT.toLocaleString()}-triangle cut, so the drill is left in place.`,
      );
      return;
    }
    await this.applyBoolean(stock, tool, "subtract");
  }

  async boltCircle(count: number, diameter: number, pcd: number) {
    const stock = this.current();
    if (!stock || this.busy) return;
    const n = Math.max(2, Math.min(12, Math.round(count) || 4));
    const d = Math.min(40, Math.max(0.5, Number.isFinite(diameter) ? diameter : 6));
    const pitch = Math.min(180, Math.max(d + 1, Number.isFinite(pcd) ? pcd : 18));
    const radius = pitch / 2;
    const chord = 2 * radius * Math.sin(Math.PI / n);
    if (chord < d + 0.3) {
      this.setStatus("Those holes overlap. Use fewer, or a wider pitch circle.");
      return;
    }
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    stock.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(stock.mesh);
    const cx = (box.min.x + box.max.x) / 2;
    const cy = (box.min.y + box.max.y) / 2;
    const half = Math.max(box.max.x - cx, cx - box.min.x, box.max.y - cy, cy - box.min.y);
    if (radius + d / 2 > half + 0.4) {
      this.setStatus(`Ø${round1(pitch)} mm is wider than ${stock.name}. The holes would miss it.`);
      return;
    }
    const h = Math.max(6, box.max.z - box.min.z + 8);
    const z0 = Math.min(box.min.z, 0) - 4;
    const chunks: Float32Array[] = [];
    let total = 0;
    for (let i = 0; i < n; i++) {
      const ang = (i * Math.PI * 2) / n;
      const bit = cylinder(cx + Math.cos(ang) * radius, cy + Math.sin(ang) * radius, z0, d / 2, h, 28);
      chunks.push(bit);
      total += bit.length;
    }
    const tris = new Float32Array(total);
    let at = 0;
    for (const bit of chunks) {
      tris.set(bit, at);
      at += bit.length;
    }
    const name = `${n} × Ø${round1(d)} on Ø${round1(pitch)}`;
    if (this.triCount(stock) > BOOLEAN_LIMIT || tris.length / 9 > BOOLEAN_LIMIT) {
      this.setStatus(`${name} is too heavy to cut out of ${stock.name}.`);
      return;
    }
    this.pushUndo();
    const tool = this.makePart(tris, name, "#c47a5a");
    this.selectedId = tool.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    await this.applyBoolean(stock, tool, "subtract", false);
  }

  async counterbore(diameter: number, bore: number, depth: number) {
    const stock = this.current();
    if (!stock || this.busy) return;
    const d = Math.min(40, Math.max(0.5, Number.isFinite(diameter) ? diameter : 6));
    const cb = Math.min(80, Number.isFinite(bore) ? bore : d * 2);
    const deep = Math.min(40, Math.max(0.4, Number.isFinite(depth) ? depth : 3));
    if (cb < d + 0.4) {
      this.setStatus("The counterbore has to be wider than the drill.");
      return;
    }
    stock.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(stock.mesh);
    const thick = Math.max(0.4, box.max.z - box.min.z);
    if (deep >= thick - 0.15) {
      this.setStatus("That counterbore is as deep as the part. Use Drill through.");
      return;
    }
    const aimed = this.face != null && this.pickPoint;
    const cx = aimed ? this.pickPoint!.x : (box.min.x + box.max.x) / 2;
    const cy = aimed ? this.pickPoint!.y : (box.min.y + box.max.y) / 2;
    const half = Math.max(box.max.x - cx, cx - box.min.x, box.max.y - cy, cy - box.min.y);
    if (cb / 2 > half + 0.2) {
      this.setStatus("That counterbore is wider than the part.");
      return;
    }
    const throughH = Math.max(6, thick + 8);
    const zThrough = Math.min(box.min.z, 0) - 4;
    const drillBit = cylinder(cx, cy, zThrough, d / 2, throughH, 28);
    const boreBit = cylinder(cx, cy, box.max.z - deep, cb / 2, deep + 0.4, 32);
    const tris = union(drillBit, boreBit);
    if (!tris || tris.length < 36) {
      this.setStatus("The counterbore did not form a solid.");
      return;
    }
    const name = `Ø${round1(d)} × Cb Ø${round1(cb)} × ${round1(deep)}`;
    await this.cutTris(stock, tris, name);
  }

  async holeRow(count: number, diameter: number, pitch: number) {
    const stock = this.current();
    if (!stock || this.busy) return;
    const n = Math.max(2, Math.min(8, Math.round(count) || 3));
    const d = Math.min(40, Math.max(0.5, Number.isFinite(diameter) ? diameter : 4));
    const step = Math.min(80, Math.max(d + 0.4, Number.isFinite(pitch) ? pitch : 8));
    if (step < d + 0.3) {
      this.setStatus("Those holes overlap. Widen the pitch.");
      return;
    }
    stock.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(stock.mesh);
    const cx = (box.min.x + box.max.x) / 2;
    const cy = (box.min.y + box.max.y) / 2;
    const span = (n - 1) * step;
    if (cx - span / 2 - d / 2 < box.min.x - 0.2 || cx + span / 2 + d / 2 > box.max.x + 0.2) {
      this.setStatus("That row is wider than the part.");
      return;
    }
    const h = Math.max(6, box.max.z - box.min.z + 8);
    const z0 = Math.min(box.min.z, 0) - 4;
    const chunks: Float32Array[] = [];
    for (let i = 0; i < n; i++) {
      chunks.push(cylinder(cx - span / 2 + i * step, cy, z0, d / 2, h, 28));
    }
    const name = `${n} × Ø${round1(d)} every ${round1(step)}`;
    await this.cutTris(stock, this.joinTris(chunks), name);
  }

  async split() {
    const stock = this.current();
    if (!stock || this.busy) return;
    if (this.triCount(stock) > BOOLEAN_LIMIT) {
      this.setStatus(`${stock.name} is over the ${BOOLEAN_LIMIT.toLocaleString()}-triangle cut, so it stays in one piece.`);
      return;
    }
    if (this.parts.length >= 23) {
      this.setStatus("Not enough room in the list for both halves.");
      return;
    }
    stock.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(stock.mesh);
    const z = this.sectionOn ? this.sectionZ : (box.min.z + box.max.z) / 2;
    if (z <= box.min.z + 0.4 || z >= box.max.z - 0.4) {
      this.setStatus("The section misses the part.");
      return;
    }
    const pad = 2;
    const sx = box.max.x - box.min.x + pad * 2;
    const sy = box.max.y - box.min.y + pad * 2;
    const below = cube(box.min.x - pad, box.min.y - pad, box.min.z - 8, sx, sy, z - (box.min.z - 8));
    const above = cube(box.min.x - pad, box.min.y - pad, z, sx, sy, box.max.z + 8 - z);
    this.busy = true;
    this.setStatus(`Splitting ${stock.name} at ${round1(z)} mm…`);
    await new Promise((r) => setTimeout(r, 40));
    if (this.dead) return;
    try {
      const body = this.worldTris(stock.mesh);
      const low = intersect(body, below);
      const high = intersect(body, above);
      if (!low || low.length < 9 || !high || high.length < 9) {
        this.setStatus("The section did not cut a solid.");
        return;
      }
      this.pushUndo();
      const name = stock.name;
      const color = stock.color;
      this.removePart(stock);
      this.makePart(low, `${name} lower`, color);
      const upper = this.makePart(high, `${name} upper`, color);
      this.selectedId = upper.id;
      this.face = null;
      this.hideFace();
      this.attachGizmo();
      this.setStatus(
        `Split ${name} at ${round1(z)} mm. ${round1(z - box.min.z)} mm below, ${round1(box.max.z - z)} mm above.`,
      );
    } catch {
      this.setStatus("The split failed. The part is unchanged.");
    } finally {
      this.busy = false;
      this.emit();
    }
  }

  private joinTris(chunks: Float32Array[]) {
    let total = 0;
    for (const bit of chunks) total += bit.length;
    const tris = new Float32Array(total);
    let at = 0;
    for (const bit of chunks) {
      tris.set(bit, at);
      at += bit.length;
    }
    return tris;
  }

  private async cutTris(stock: Part, tris: Float32Array, name: string) {
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    if (this.triCount(stock) > BOOLEAN_LIMIT || tris.length / 9 > BOOLEAN_LIMIT) {
      this.setStatus(`${name} is too heavy to cut out of ${stock.name}.`);
      return;
    }
    this.pushUndo();
    const tool = this.makePart(tris, name, "#c47a5a");
    this.selectedId = tool.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    await this.applyBoolean(stock, tool, "subtract", false);
  }

  deleteSelected() {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    this.removePart(part);
    this.selectedId = this.parts[this.parts.length - 1]?.id ?? null;
    this.face = null;
    this.hideFace();
    this.attachGizmo();
    this.setStatus(part.name + " is gone.");
  }

  duplicate() {
    const part = this.current();
    if (!part) return;
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    this.pushUndo();
    const copy = this.clonePart(part, 0);
    const box = new THREE.Box3().setFromObject(part.mesh);
    copy.mesh.position.x += box.max.x - box.min.x + 6;
    this.selectedId = copy.id;
    this.attachGizmo();
    this.setStatus(copy.name + " is a copy.");
  }

  repeat() {
    const part = this.current();
    if (!part) return;
    if (this.parts.length >= 22) {
      this.setStatus("Not enough room in the list for three copies.");
      return;
    }
    this.pushUndo();
    const box = new THREE.Box3().setFromObject(part.mesh);
    const step = box.max.x - box.min.x + 6;
    const a = this.clonePart(part, step);
    const b = this.clonePart(part, step * 2);
    this.selectedId = b.id;
    this.attachGizmo();
    this.setStatus(`${part.name} repeated as ${a.name} and ${b.name}.`);
  }

  mirror() {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const a = attr.array as Float32Array;
    for (let i = 0; i < attr.count; i += 3) {
      const x0 = -(a[i * 3] ?? 0);
      const y0 = a[i * 3 + 1] ?? 0;
      const z0 = a[i * 3 + 2] ?? 0;
      const x1 = -(a[i * 3 + 3] ?? 0);
      const y1 = a[i * 3 + 4] ?? 0;
      const z1 = a[i * 3 + 5] ?? 0;
      const x2 = -(a[i * 3 + 6] ?? 0);
      const y2 = a[i * 3 + 7] ?? 0;
      const z2 = a[i * 3 + 8] ?? 0;
      a[i * 3] = x0;
      a[i * 3 + 1] = y0;
      a[i * 3 + 2] = z0;
      a[i * 3 + 3] = x2;
      a[i * 3 + 4] = y2;
      a[i * 3 + 5] = z2;
      a[i * 3 + 6] = x1;
      a[i * 3 + 7] = y1;
      a[i * 3 + 8] = z1;
    }
    attr.needsUpdate = true;
    part.mesh.geometry.computeVertexNormals();
    part.mesh.geometry.computeBoundingBox();
    part.mesh.geometry.computeBoundingSphere();
    this.rebuildEdges(part);
    part.volume = this.localVolume(part);
    this.setStatus(part.name + " mirrored through X.");
  }

  drop() {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    this.dropPart(part);
    this.setStatus(part.name + " is sitting on the bed.");
  }

  center() {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    this.centerPart(part);
    this.setStatus(part.name + " is centred on the bed.");
  }

  lay() {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    if (this.face == null) {
      this.alignNormalDown(part, this.bestDownNormal(part.mesh.geometry));
      this.centerPart(part);
      this.dropPart(part);
      this.setStatus("Laid on the flattest side, then dropped onto the bed.");
      return;
    }
    const index = this.face;
    this.alignFaceDown(part, index);
    this.centerPart(part);
    this.dropPart(part);
    this.face = index;
    this.showFace(part, index);
    this.setStatus("That face is level, and the part is on the bed.");
  }

  turn(axis: "x" | "y" | "z", angleDeg: number) {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    const v = new THREE.Vector3(axis === "x" ? 1 : 0, axis === "y" ? 1 : 0, axis === "z" ? 1 : 0);
    const q = new THREE.Quaternion().setFromAxisAngle(v, THREE.MathUtils.degToRad(angleDeg));
    part.mesh.quaternion.premultiply(q);
    this.setStatus(`${part.name} turned ${angleDeg}° around ${axis.toUpperCase()}.`);
  }

  setPosition(x: number, y: number, z: number) {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    part.mesh.position.set(x, y, z);
    this.emit();
  }

  setRotation(x: number, y: number, z: number) {
    const part = this.current();
    if (!part) return;
    this.pushUndo();
    part.mesh.rotation.set(
      THREE.MathUtils.degToRad(x),
      THREE.MathUtils.degToRad(y),
      THREE.MathUtils.degToRad(z),
    );
    this.emit();
  }

  setScale(x: number, y: number, z: number) {
    const part = this.current();
    if (!part) return;
    const sx = x === 0 ? 0.01 : x;
    const sy = y === 0 ? 0.01 : y;
    const sz = z === 0 ? 0.01 : z;
    this.pushUndo();
    part.mesh.scale.set(sx, sy, sz);
    part.volume = this.localVolume(part);
    this.emit();
  }

  async combine(op: BoolOp) {
    const cutter = this.current();
    const stock = this.stock();
    if (!cutter || !stock || cutter === stock || this.busy) {
      this.setStatus("Need two parts. The selected one is the tool.");
      return;
    }
    await this.applyBoolean(stock, cutter, op);
  }

  private async applyBoolean(stock: Part, cutter: Part, op: BoolOp, record = true) {
    if (this.busy) return;
    const ta = this.triCount(stock);
    const tb = this.triCount(cutter);
    if (ta > BOOLEAN_LIMIT || tb > BOOLEAN_LIMIT) {
      this.setStatus(
        `Booleans stop at ${BOOLEAN_LIMIT.toLocaleString()} triangles. ${stock.name} has ${ta.toLocaleString()}, ${cutter.name} has ${tb.toLocaleString()}.`,
      );
      return;
    }
    this.busy = true;
    const verb = op === "union" ? "Joining" : op === "subtract" ? "Cutting" : "Intersecting";
    this.setStatus(`${verb} ${cutter.name} with ${stock.name}…`);
    await new Promise((r) => setTimeout(r, 40));
    if (this.dead) return;
    try {
      const A = this.worldTris(stock.mesh);
      const B = this.worldTris(cutter.mesh);
      const out = op === "union" ? union(A, B) : op === "subtract" ? subtract(A, B) : intersect(A, B);
      if (!out || out.length < 9) {
        this.setStatus("Those two do not meet, so the solid was left as it was.");
        return;
      }
      if (out.length / 9 > 20000) {
        this.setStatus("That cut made too many triangles. Nothing was changed.");
        return;
      }
      if (record) this.pushUndo();
      const name = op === "intersect" ? stock.name + " ∩ " + cutter.name : stock.name;
      const color = stock.color;
      const stockName = stock.name;
      const cutterName = cutter.name;
      this.removePart(stock);
      this.removePart(cutter);
      const part = this.makePart(out, name, color);
      part.mesh.position.set(0, 0, 0);
      this.selectedId = part.id;
      this.face = null;
      this.hideFace();
      this.attachGizmo();
      const done =
        op === "subtract"
          ? `Cut ${cutterName} out of ${stockName}.`
          : op === "union"
            ? `Joined into ${part.name}.`
            : `Kept only the overlap, as ${part.name}.`;
      this.setStatus(`${done} ${Math.round(out.length / 9).toLocaleString()} triangles.`);
    } catch {
      this.setStatus("The boolean failed. The parts are unchanged.");
    } finally {
      this.busy = false;
      this.emit();
    }
  }

  undo() {
    const snap = this.undoStack.pop();
    if (!snap) {
      this.setStatus("Nothing to undo.");
      return;
    }
    this.redoStack.push(this.capture());
    this.restore(snap);
    this.setStatus("Undone.");
  }

  redo() {
    const snap = this.redoStack.pop();
    if (!snap) {
      this.setStatus("Nothing to redo.");
      return;
    }
    this.undoStack.push(this.capture());
    this.restore(snap);
    this.setStatus("Redone.");
  }

  frame(all: boolean) {
    const list = all ? this.parts.filter((p) => p.mesh.visible) : this.current() ? [this.current()!] : [];
    const box = new THREE.Box3();
    for (const p of list) box.expandByObject(p.mesh);
    if (box.isEmpty()) {
      box.set(new THREE.Vector3(-40, -40, 0), new THREE.Vector3(40, 40, 30));
    }
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z) * 0.55 || 20;
    const fov = (this.camera.fov * Math.PI) / 180;
    const dist = Math.max(30, (radius / Math.sin(fov / 2)) * 0.92);
    const longerY = size.y > size.x * 1.15;
    const dir = (longerY ? new THREE.Vector3(1.2, -0.35, 0.72) : new THREE.Vector3(0.55, -1.15, 0.72)).normalize();
    this.camera.up.set(0, 0, 1);
    this.camera.position.copy(center).addScaledVector(dir, dist);
    this.orbit.target.copy(center);
    this.camera.lookAt(center);
    this.camera.updateProjectionMatrix();
    this.orbit.update();
  }

  look(which: "top" | "front" | "side") {
    const box = new THREE.Box3();
    const list = this.parts.filter((p) => p.mesh.visible);
    for (const p of list) box.expandByObject(p.mesh);
    if (box.isEmpty()) box.set(new THREE.Vector3(-40, -40, 0), new THREE.Vector3(40, 40, 20));
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const dist = Math.max(size.x, size.y, size.z, 40) * 1.7;
    const pos =
      which === "top"
        ? new THREE.Vector3(center.x + dist * 0.01, center.y - dist * 0.04, center.z + dist)
        : which === "front"
          ? new THREE.Vector3(center.x, center.y - dist, center.z + dist * 0.15)
          : new THREE.Vector3(center.x + dist, center.y, center.z + dist * 0.15);
    this.camera.up.set(0, 0, 1);
    this.camera.position.copy(pos);
    this.orbit.target.copy(center);
    this.camera.lookAt(center);
    this.orbit.update();
  }

  exportSelected(): { filename: string; buffer: ArrayBuffer } | null {
    const part = this.current();
    if (!part) return null;
    const tris = this.worldTris(part.mesh);
    return { filename: fileName(part.name), buffer: encodeSTL(tris) };
  }

  exportAll(): { filename: string; buffer: ArrayBuffer } | null {
    if (!this.parts.length) return null;
    let total = 0;
    const chunks: Float32Array[] = [];
    for (const part of this.parts) {
      if (!part.mesh.visible) continue;
      const tris = this.worldTris(part.mesh);
      chunks.push(tris);
      total += tris.length;
    }
    if (!total) return null;
    const all = new Float32Array(total);
    let o = 0;
    for (const c of chunks) {
      all.set(c, o);
      o += c.length;
    }
    return { filename: "bench.stl", buffer: encodeSTL(all) };
  }

  dispose() {
    this.dead = true;
    this.renderer.setAnimationLoop(null);
    this.renderer.domElement.removeEventListener("pointerdown", this.onPointerDown);
    this.renderer.domElement.removeEventListener("pointermove", this.onPointerMove);
    this.renderer.domElement.removeEventListener("pointerup", this.onPointerUp);
    window.removeEventListener("resize", this.resize);
    this.transform.detach();
    this.transform.dispose();
    this.orbit.dispose();
    this.sketchPreview.geometry.dispose();
    (this.sketchPreview.material as THREE.Material).dispose();
    this.sectionSheet.geometry.dispose();
    (this.sectionSheet.material as THREE.Material).dispose();
    for (const part of [...this.parts]) this.disposePart(part);
    this.renderer.dispose();
  }

  private addPositions(raw: Float32Array, name: string, lay: boolean) {
    const scaled = normalizeUnits(raw);
    let positions = scaled.positions;
    let thinNote = "";
    if (positions.length / 9 > 80000) {
      const before = Math.round(positions.length / 9);
      positions = thinTriangles(positions, 80000);
      thinNote = ` Thinned from ${before.toLocaleString()} triangles so it stays movable.`;
    }
    const part = this.makePart(positions, name);
    this.seat(part, lay);
    this.park(part);
    this.selectedId = part.id;
    this.face = null;
    this.hideFace();
    this.attachGizmo();
    const note = [scaled.note, thinNote].filter(Boolean).join(" ");
    this.setStatus(this.describeSelection(note));
  }

  private describeSelection(prefix: string) {
    const part = this.current();
    if (!part) return prefix;
    const size = this.sizeOf(part);
    const tris = this.triCount(part).toLocaleString();
    const body = `${part.name}. ${tris} triangles. ${round1(size[0])} × ${round1(size[1])} × ${round1(size[2])} mm.`;
    return prefix ? `${prefix} ${body}` : body;
  }

  private makePart(tris: Float32Array, name: string, color?: string): Part {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(tris, 3));
    geometry.computeVertexNormals();
    geometry.computeBoundingBox();
    geometry.computeBoundingSphere();
    const hex = color ?? METALS[this.colorCursor++ % METALS.length]!;
    const material = new THREE.MeshPhysicalMaterial({
      color: hex,
      metalness: 0.42,
      roughness: 0.46,
      clearcoat: 0.15,
      clearcoatRoughness: 0.45,
      envMapIntensity: 0.55,
      side: THREE.DoubleSide,
    });
    const mesh = new THREE.Mesh(geometry, material);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    const id = "p" + this.serial++;
    const part: Part = { id, name: this.uniqueName(name), color: hex, mesh, edges: null, volume: 0 };
    this.rebuildEdges(part);
    part.volume = this.localVolume(part);
    this.applyShade(part);
    this.scene.add(mesh);
    this.parts.push(part);
    return part;
  }

  private uniqueName(name: string) {
    const base = name.trim() || "Part";
    if (!this.parts.some((p) => p.name === base)) return base;
    let n = 2;
    while (this.parts.some((p) => p.name === `${base} ${n}`)) n++;
    return `${base} ${n}`;
  }

  private clonePart(part: Part, dx: number) {
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const tris = new Float32Array((attr.array as Float32Array).slice(0, attr.count * 3));
    const copy = this.makePart(tris, part.name, part.color);
    copy.mesh.position.copy(part.mesh.position);
    copy.mesh.quaternion.copy(part.mesh.quaternion);
    copy.mesh.scale.copy(part.mesh.scale);
    copy.mesh.position.x += dx;
    copy.volume = part.volume;
    return copy;
  }

  private seat(part: Part, lay: boolean) {
    if (lay) this.alignNormalDown(part, this.bestDownNormal(part.mesh.geometry));
    this.centerPart(part);
    this.dropPart(part);
  }

  private bestDownNormal(geometry: THREE.BufferGeometry) {
    const pos = geometry.getAttribute("position") as THREE.BufferAttribute;
    const candidates: THREE.Vector3[] = [
      new THREE.Vector3(1, 0, 0),
      new THREE.Vector3(0, 1, 0),
      new THREE.Vector3(0, 0, 1),
    ];
    const faces = Math.floor(pos.count / 3);
    const ranked: { i: number; area: number }[] = [];
    const a = new THREE.Vector3();
    const b = new THREE.Vector3();
    const c = new THREE.Vector3();
    for (let i = 0; i < faces; i++) {
      a.fromBufferAttribute(pos, i * 3);
      b.fromBufferAttribute(pos, i * 3 + 1);
      c.fromBufferAttribute(pos, i * 3 + 2);
      const area = b.sub(a).cross(c.sub(a)).length();
      if (ranked.length < 16) {
        ranked.push({ i, area });
        ranked.sort((p, q) => q.area - p.area);
      } else if (ranked[15] && area > ranked[15].area) {
        ranked[15] = { i, area };
        ranked.sort((p, q) => q.area - p.area);
      }
    }
    for (const face of ranked) {
      a.fromBufferAttribute(pos, face.i * 3);
      b.fromBufferAttribute(pos, face.i * 3 + 1);
      c.fromBufferAttribute(pos, face.i * 3 + 2);
      const n = new THREE.Vector3().subVectors(b, a).cross(c.sub(a));
      if (n.lengthSq() > 1e-8) candidates.push(n.normalize());
    }
    let best = candidates[2]!;
    let bestH = Infinity;
    const v = new THREE.Vector3();
    for (const n of candidates) {
      let min = Infinity;
      let max = -Infinity;
      for (let k = 0; k < pos.count; k++) {
        const d = v.fromBufferAttribute(pos, k).dot(n);
        if (d < min) min = d;
        if (d > max) max = d;
      }
      const height = max - min;
      if (height < bestH) {
        bestH = height;
        best = n;
      }
    }
    return best.clone().normalize();
  }

  private alignNormalDown(part: Part, normal: THREE.Vector3) {
    const n = normal.clone().transformDirection(part.mesh.matrixWorld);
    if (n.lengthSq() < 1e-8) return;
    const q = new THREE.Quaternion().setFromUnitVectors(n, new THREE.Vector3(0, 0, -1));
    part.mesh.quaternion.premultiply(q);
    part.mesh.updateMatrixWorld(true);
    const mid = new THREE.Box3().setFromObject(part.mesh).getCenter(new THREE.Vector3());
    if (mid.z < 0) {
      const flip = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), Math.PI);
      part.mesh.quaternion.premultiply(flip);
    }
  }

  private park(part: Part) {
    const others = new THREE.Box3();
    for (const p of this.parts) {
      if (p !== part && p.mesh.visible) others.expandByObject(p.mesh);
    }
    if (others.isEmpty()) return;
    const mine = new THREE.Box3().setFromObject(part.mesh);
    const shift = others.max.x + 8 - mine.min.x;
    if (Number.isFinite(shift)) part.mesh.position.x += shift;
  }

  private alignFaceDown(part: Part, face: number) {
    const n = this.faceNormal(part.mesh, face);
    if (n.lengthSq() < 1e-8) return;
    const q = new THREE.Quaternion().setFromUnitVectors(n, new THREE.Vector3(0, 0, -1));
    part.mesh.quaternion.premultiply(q);
    part.mesh.updateMatrixWorld(true);
    const mid = new THREE.Box3().setFromObject(part.mesh).getCenter(new THREE.Vector3());
    const faceZ = this.facePoint(part.mesh, face).z;
    if (mid.z < faceZ - 0.05) {
      const flip = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), Math.PI);
      part.mesh.quaternion.premultiply(flip);
    }
  }

  private faceNormal(mesh: THREE.Mesh, face: number) {
    const pos = mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const a = new THREE.Vector3().fromBufferAttribute(pos, face * 3);
    const b = new THREE.Vector3().fromBufferAttribute(pos, face * 3 + 1);
    const c = new THREE.Vector3().fromBufferAttribute(pos, face * 3 + 2);
    return new THREE.Vector3().subVectors(b, a).cross(new THREE.Vector3().subVectors(c, a)).normalize().transformDirection(mesh.matrixWorld);
  }

  private facePoint(mesh: THREE.Mesh, face: number) {
    const pos = mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const a = new THREE.Vector3().fromBufferAttribute(pos, face * 3);
    const b = new THREE.Vector3().fromBufferAttribute(pos, face * 3 + 1);
    const c = new THREE.Vector3().fromBufferAttribute(pos, face * 3 + 2);
    return a.add(b).add(c).multiplyScalar(1 / 3).applyMatrix4(mesh.matrixWorld);
  }

  private centerPart(part: Part) {
    part.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(part.mesh);
    const cx = (box.min.x + box.max.x) / 2;
    const cy = (box.min.y + box.max.y) / 2;
    part.mesh.position.x -= cx;
    part.mesh.position.y -= cy;
  }

  private dropPart(part: Part) {
    part.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(part.mesh);
    part.mesh.position.z -= box.min.z;
  }

  private showFace(part: Part, face: number) {
    const pos = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    if (face < 0 || face * 3 + 2 >= pos.count) {
      this.hideFace();
      return;
    }
    const arr = new Float32Array(9);
    for (let k = 0; k < 3; k++) {
      arr[k * 3] = pos.getX(face * 3 + k);
      arr[k * 3 + 1] = pos.getY(face * 3 + k);
      arr[k * 3 + 2] = pos.getZ(face * 3 + k);
    }
    this.faceMark.geometry.dispose();
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.BufferAttribute(arr, 3));
    this.faceMark.geometry = g;
    if (this.faceMark.parent !== part.mesh) part.mesh.add(this.faceMark);
    this.faceMark.visible = true;
  }

  private hideFace() {
    this.faceMark.visible = false;
  }

  private current() {
    return this.parts.find((p) => p.id === this.selectedId) ?? null;
  }

  private stock() {
    const idx = this.parts.findIndex((p) => p.id === this.selectedId);
    if (idx < 0) return null;
    if (idx > 0) return this.parts[idx - 1] ?? null;
    return this.parts[1] ?? null;
  }

  private triCount(part: Part) {
    const pos = part.mesh.geometry.getAttribute("position");
    return pos ? Math.round(pos.count / 3) : 0;
  }

  private sizeOf(part: Part): [number, number, number] {
    const box = new THREE.Box3().setFromObject(part.mesh);
    const s = box.getSize(new THREE.Vector3());
    return [s.x, s.y, s.z];
  }

  private localVolume(part: Part) {
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const base = volume(attr.array as Float32Array);
    const s = part.mesh.scale;
    return base * Math.abs(s.x * s.y * s.z);
  }

  private worldTris(mesh: THREE.Mesh) {
    mesh.updateMatrixWorld(true);
    const pos = mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const out = new Float32Array(pos.count * 3);
    const v = new THREE.Vector3();
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i).applyMatrix4(mesh.matrixWorld);
      out[i * 3] = v.x;
      out[i * 3 + 1] = v.y;
      out[i * 3 + 2] = v.z;
    }
    return out;
  }

  private rebuildEdges(part: Part) {
    if (part.edges) {
      part.mesh.remove(part.edges);
      part.edges.geometry.dispose();
      (part.edges.material as THREE.Material).dispose();
      part.edges = null;
    }
    const tris = this.triCount(part);
    if (tris > 4000 || tris < 1) return;
    const eg = new THREE.EdgesGeometry(part.mesh.geometry, 31);
    const mat = new THREE.LineBasicMaterial({ color: "#1a140c", transparent: true, opacity: 0.35 });
    const lines = new THREE.LineSegments(eg, mat);
    lines.raycast = () => {};
    part.mesh.add(lines);
    part.edges = lines;
  }

  private applyShade(part: Part) {
    const mat = part.mesh.material as THREE.MeshPhysicalMaterial;
    const selected = part.id === this.selectedId;
    mat.wireframe = this.shade === "wire";
    mat.metalness = this.shade === "metal" ? 0.42 : 0.04;
    mat.roughness = this.shade === "clay" ? 0.92 : 0.46;
    mat.clearcoat = this.shade === "metal" ? 0.15 : 0;
    mat.envMapIntensity = this.shade === "metal" ? 0.55 : 0.3;
    mat.emissive.set(selected ? "#e8b84a" : "#000000");
    mat.emissiveIntensity = selected ? 0.16 : 0;
    mat.clippingPlanes = this.sectionOn ? [this.clipPlane] : null;
    mat.clipShadows = this.sectionOn;
    if (part.edges) {
      part.edges.visible = part.mesh.visible && this.shade !== "wire";
      const line = part.edges.material as THREE.LineBasicMaterial;
      line.color.set(selected ? "#e8b84a" : "#1a140c");
      line.opacity = selected ? 0.85 : 0.32;
    }
  }

  private attachGizmo() {
    const part = this.current();
    if (!part || !part.mesh.visible || this.measuring) {
      this.transform.detach();
      return;
    }
    this.transform.setMode(this.tool);
    this.transform.attach(part.mesh);
  }

  private removePart(part: Part) {
    const i = this.parts.indexOf(part);
    if (i >= 0) this.parts.splice(i, 1);
    this.disposePart(part);
  }

  private disposePart(part: Part) {
    if (this.faceMark.parent === part.mesh) {
      this.scene.attach(this.faceMark);
      this.faceMark.visible = false;
    }
    part.mesh.removeFromParent();
    part.mesh.geometry.dispose();
    (part.mesh.material as THREE.Material).dispose();
    if (part.edges) {
      part.edges.geometry.dispose();
      (part.edges.material as THREE.Material).dispose();
    }
  }

  private pushUndo() {
    this.undoStack.push(this.capture());
    if (this.undoStack.length > 24) this.undoStack.shift();
    this.redoStack.length = 0;
  }

  private capture(): Snap {
    return {
      selectedId: this.selectedId,
      parts: this.parts.map((part) => {
        const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
        return {
          id: part.id,
          name: part.name,
          color: part.color,
          visible: part.mesh.visible,
          pos: part.mesh.position.toArray() as [number, number, number],
          quat: part.mesh.quaternion.toArray() as [number, number, number, number],
          scale: part.mesh.scale.toArray() as [number, number, number],
          positions: new Float32Array((attr.array as Float32Array).slice(0, attr.count * 3)),
        };
      }),
    };
  }

  private restore(snap: Snap) {
    this.transform.detach();
    for (const part of [...this.parts]) this.removePart(part);
    for (const s of snap.parts) {
      const part = this.makePart(s.positions, s.name, s.color);
      part.id = s.id;
      part.mesh.position.fromArray(s.pos);
      part.mesh.quaternion.fromArray(s.quat);
      part.mesh.scale.fromArray(s.scale);
      part.mesh.visible = s.visible;
      part.volume = this.localVolume(part);
      this.applyShade(part);
      const n = Number(s.id.replace(/\D/g, ""));
      if (n >= this.serial) this.serial = n + 1;
    }
    // makePart uniqueName may have renamed. Put the saved names back.
    snap.parts.forEach((s, i) => {
      const part = this.parts[i];
      if (part) part.name = s.name;
    });
    this.selectedId = snap.selectedId;
    this.face = null;
    this.hideFace();
    this.attachGizmo();
    this.emit();
  }

  private onPointerDown = (ev: PointerEvent) => {
    if (this.pulling && ev.button === 0) {
      const part = this.current();
      if (part && this.face != null && this.beginPull(part, this.face, ev)) {
        this.orbit.enabled = false;
        try {
          this.renderer.domElement.setPointerCapture(ev.pointerId);
        } catch {
          /* the drag still tracks over the view */
        }
      }
    } else if (this.sketch && ev.button === 0) {
      const point = this.pickBed(ev);
      if (point) {
        this.sketchDrag = { x: point.x, y: point.y };
        this.orbit.enabled = false;
        try {
          this.renderer.domElement.setPointerCapture(ev.pointerId);
        } catch {
          /* the drag still tracks while the pointer stays over the view */
        }
      }
    }
    this.pointer = { x: ev.clientX, y: ev.clientY, moved: 0 };
  };

  private onPointerMove = (ev: PointerEvent) => {
    if (this.pullDrag) {
      this.movePull(ev);
      return;
    }
    if (!this.sketchDrag || !this.sketch) return;
    const point = this.pickBed(ev);
    if (!point) return;
    const foot = this.footprint(this.sketchDrag.x, this.sketchDrag.y, point.x, point.y);
    if (foot.w < 0.4 || foot.d < 0.4) {
      this.sketchPreview.visible = false;
      return;
    }
    this.showSketchPreview(foot);
  };

  private onPointerUp = (ev: PointerEvent) => {
    if (this.pullDrag && ev.button === 0) {
      this.finishPull();
      this.orbit.enabled = true;
      this.pointer = null;
      return;
    }
    if (this.sketchDrag && this.sketch && ev.button === 0) {
      const start = this.sketchDrag;
      this.sketchDrag = null;
      this.orbit.enabled = true;
      const point = this.pickBed(ev);
      if (point) this.commitSketch(start.x, start.y, point.x, point.y);
      else this.sketchPreview.visible = false;
      this.pointer = null;
      return;
    }
    if (!this.pointer || this.transform.dragging) {
      this.pointer = null;
      return;
    }
    const moved = Math.abs(ev.clientX - this.pointer.x) + Math.abs(ev.clientY - this.pointer.y);
    this.pointer = null;
    if (moved > 6) return;
    const hit = this.pick(ev);
    if (this.measuring) {
      const point = hit?.point ?? this.pickBed(ev);
      if (!point) return;
      this.measureHits.push(point.clone());
      if (this.measureHits.length === 2) {
        const d = this.measureHits[0]!.distanceTo(this.measureHits[1]!);
        this.drawMeasure();
        this.setStatus(`${round1(d)} mm between the two points.`);
      } else if (this.measureHits.length > 2) {
        this.measureHits.splice(0, this.measureHits.length - 1);
        this.clearMeasure(false);
        this.setStatus("First point set. Click the second.");
      } else {
        this.setStatus("First point set. Click the second.");
      }
      return;
    }
    if (!hit) {
      this.face = null;
      this.hideFace();
      this.emit();
      return;
    }
    const part = this.parts.find((p) => p.mesh === hit.object);
    if (!part) return;
    this.selectedId = part.id;
    this.face = hit.faceIndex ?? null;
    this.pickPoint = this.face != null ? hit.point.clone() : null;
    if (this.face != null) this.showFace(part, this.face);
    if (this.pulling) this.transform.detach();
    else this.attachGizmo();
    this.setStatus(
      this.describeSelection(
        this.face != null ? (this.pulling ? "That face is armed. Drag to pull it." : `Face ${this.face}.`) : "",
      ),
    );
  };

  private leavePull(announce: boolean) {
    if (!this.pulling && !this.pullDrag) return;
    this.pulling = false;
    this.pullDrag = null;
    if (!this.sketch) this.orbit.enableRotate = true;
    if (announce) this.setStatus("Pull is off.");
  }

  private triNormalLocal(pos: THREE.BufferAttribute, face: number) {
    const a = new THREE.Vector3().fromBufferAttribute(pos, face * 3);
    const b = new THREE.Vector3().fromBufferAttribute(pos, face * 3 + 1);
    const c = new THREE.Vector3().fromBufferAttribute(pos, face * 3 + 2);
    return new THREE.Vector3().subVectors(b, a).cross(new THREE.Vector3().subVectors(c, a));
  }

  private regionVerts(part: Part, face: number) {
    const pos = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const tris = Math.floor(pos.count / 3);
    if (face < 0 || face >= tris) return { verts: [] as number[], normal: new THREE.Vector3() };
    const n0 = this.triNormalLocal(pos, face);
    if (n0.lengthSq() < 1e-12) return { verts: [] as number[], normal: n0 };
    n0.normalize();
    const w0 = n0.dot(new THREE.Vector3().fromBufferAttribute(pos, face * 3));
    const verts: number[] = [];
    const eps = 0.4;
    for (let t = 0; t < tris; t++) {
      const n = this.triNormalLocal(pos, t);
      if (n.lengthSq() < 1e-12 || n.normalize().dot(n0) < 0.992) continue;
      const ids: number[] = [];
      let on = true;
      for (let k = 0; k < 3; k++) {
        const i = t * 3 + k;
        const d = n0.x * pos.getX(i) + n0.y * pos.getY(i) + n0.z * pos.getZ(i);
        if (Math.abs(d - w0) > eps) {
          on = false;
          break;
        }
        ids.push(i);
      }
      if (on) verts.push(...ids);
    }
    return { verts, normal: n0 };
  }

  private offsetFace(part: Part, face: number, mm: number) {
    const region = this.regionVerts(part, face);
    if (region.verts.length < 3) {
      this.setStatus("That face has nothing to pull.");
      return 0;
    }
    part.mesh.updateMatrixWorld(true);
    const localN = region.normal.clone().normalize();
    const worldN = localN.clone().transformDirection(part.mesh.matrixWorld).normalize();
    const p0 = new THREE.Vector3().applyMatrix4(part.mesh.matrixWorld);
    const p1 = localN.clone().applyMatrix4(part.mesh.matrixWorld);
    const scale = p1.sub(p0).dot(worldN) || 1;
    const t = mm / scale;
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const a = attr.array as Float32Array;
    this.pushUndo();
    for (const i of region.verts) {
      a[i * 3] = (a[i * 3] ?? 0) + localN.x * t;
      a[i * 3 + 1] = (a[i * 3 + 1] ?? 0) + localN.y * t;
      a[i * 3 + 2] = (a[i * 3 + 2] ?? 0) + localN.z * t;
    }
    attr.needsUpdate = true;
    part.mesh.geometry.computeVertexNormals();
    part.mesh.geometry.computeBoundingBox();
    part.mesh.geometry.computeBoundingSphere();
    this.rebuildEdges(part);
    part.volume = this.localVolume(part);
    this.showFace(part, face);
    return region.verts.length;
  }

  private pointerRay(ev: PointerEvent) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    const y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(new THREE.Vector2(x, y), this.camera);
    return this.raycaster.ray;
  }

  private axisHit(ray: THREE.Ray, origin: THREE.Vector3, dir: THREE.Vector3) {
    const w0 = new THREE.Vector3().subVectors(ray.origin, origin);
    const b = ray.direction.dot(dir);
    const d = ray.direction.dot(w0);
    const e = dir.dot(w0);
    const denom = 1 - b * b;
    if (Math.abs(denom) < 1e-5) return null;
    return (e - b * d) / denom;
  }

  private beginPull(part: Part, face: number, ev: PointerEvent) {
    const region = this.regionVerts(part, face);
    if (region.verts.length < 3) {
      this.setStatus("That face has nothing to pull.");
      return false;
    }
    part.mesh.updateMatrixWorld(true);
    const localN = region.normal.clone().normalize();
    const worldN = localN.clone().transformDirection(part.mesh.matrixWorld).normalize();
    const origin = this.facePoint(part.mesh, face);
    const anchor = this.axisHit(this.pointerRay(ev), origin, worldN);
    if (anchor == null) {
      this.setStatus("Turn the view a little. Pull cannot see along that face.");
      return false;
    }
    const p0 = new THREE.Vector3().applyMatrix4(part.mesh.matrixWorld);
    const p1 = localN.clone().applyMatrix4(part.mesh.matrixWorld);
    const scale = p1.sub(p0).dot(worldN);
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    this.pushUndo();
    this.pullDrag = {
      localN,
      worldN,
      origin,
      indices: region.verts,
      base: new Float32Array((attr.array as Float32Array).slice(0, attr.count * 3)),
      anchor,
      scale: Math.abs(scale) < 1e-6 ? 1 : scale,
    };
    return true;
  }

  private movePull(ev: PointerEvent) {
    const drag = this.pullDrag;
    const part = this.current();
    if (!drag || !part) return;
    const now = this.axisHit(this.pointerRay(ev), drag.origin, drag.worldN);
    if (now == null) return;
    const delta = now - drag.anchor;
    const t = delta / drag.scale;
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const a = attr.array as Float32Array;
    const nx = drag.localN.x;
    const ny = drag.localN.y;
    const nz = drag.localN.z;
    for (const i of drag.indices) {
      a[i * 3] = (drag.base[i * 3] ?? 0) + nx * t;
      a[i * 3 + 1] = (drag.base[i * 3 + 1] ?? 0) + ny * t;
      a[i * 3 + 2] = (drag.base[i * 3 + 2] ?? 0) + nz * t;
    }
    attr.needsUpdate = true;
    part.mesh.geometry.computeBoundingSphere();
  }

  private finishPull() {
    const drag = this.pullDrag;
    this.pullDrag = null;
    const part = this.current();
    if (!drag || !part) return;
    const attr = part.mesh.geometry.getAttribute("position") as THREE.BufferAttribute;
    const a = attr.array as Float32Array;
    let moved = 0;
    const i0 = drag.indices[0] ?? 0;
    moved = Math.hypot(
      (a[i0 * 3] ?? 0) - (drag.base[i0 * 3] ?? 0),
      (a[i0 * 3 + 1] ?? 0) - (drag.base[i0 * 3 + 1] ?? 0),
      (a[i0 * 3 + 2] ?? 0) - (drag.base[i0 * 3 + 2] ?? 0),
    );
    part.mesh.geometry.computeVertexNormals();
    part.mesh.geometry.computeBoundingBox();
    part.mesh.geometry.computeBoundingSphere();
    this.rebuildEdges(part);
    part.volume = this.localVolume(part);
    if (this.face != null) this.showFace(part, this.face);
    const mm = moved * Math.abs(drag.scale);
    if (mm < 0.15) {
      this.undo();
      this.pulling = true;
      this.transform.detach();
      this.orbit.enableRotate = false;
      this.setStatus("That pull was under 0.2 mm, so it was dropped.");
      return;
    }
    this.setStatus(`Pulled ${round1(mm)} mm. ${drag.indices.length} points moved on that face.`);
  }

  private mill(n: number) {
    return this.snap ? Math.round(n) : Math.round(n * 10) / 10;
  }

  private footprint(x0: number, y0: number, x1: number, y1: number) {
    const a = this.mill(x0);
    const b = this.mill(y0);
    const c = this.mill(x1);
    const d = this.mill(y1);
    if (this.sketch === "circle") {
      const r = Math.hypot(c - a, d - b);
      return { kind: "circle" as const, cx: a, cy: b, w: r, d: r };
    }
    return {
      kind: this.sketch === "slot" ? ("slot" as const) : ("rect" as const),
      cx: (a + c) / 2,
      cy: (b + d) / 2,
      w: Math.abs(c - a),
      d: Math.abs(d - b),
    };
  }

  private showSketchPreview(foot: { kind: "rect" | "circle" | "slot"; cx: number; cy: number; w: number; d: number }) {
    const h = this.sketchHeight;
    const old = this.sketchPreview.geometry;
    if (foot.kind === "slot") {
      this.sketchPreview.geometry = this.slotPreview(foot.w, foot.d, h);
      this.sketchPreview.rotation.set(0, 0, 0);
    } else if (foot.kind === "rect") {
      this.sketchPreview.geometry = new THREE.BoxGeometry(Math.max(foot.w, 0.2), Math.max(foot.d, 0.2), h);
      this.sketchPreview.rotation.set(0, 0, 0);
    } else {
      const r = Math.max(foot.w, 0.2);
      this.sketchPreview.geometry = new THREE.CylinderGeometry(r, r, h, 40);
      this.sketchPreview.rotation.set(Math.PI / 2, 0, 0);
    }
    this.sketchPreview.position.set(foot.cx, foot.cy, this.sketchLift(h));
    old.dispose();
    this.sketchPreview.visible = true;
  }

  private slotPreview(w: number, d: number, h: number) {
    const long = Math.max(w, d, 0.4);
    const short = Math.max(Math.min(w, d), 0.4);
    const r = short / 2;
    const hx = Math.max(0, long / 2 - r);
    const shape = new THREE.Shape();
    shape.absarc(-hx, 0, r, Math.PI / 2, -Math.PI / 2, true);
    shape.absarc(hx, 0, r, -Math.PI / 2, Math.PI / 2, true);
    const geo = new THREE.ExtrudeGeometry(shape, { depth: h, bevelEnabled: false, curveSegments: 10 });
    geo.translate(0, 0, -h / 2);
    if (w < d) geo.rotateZ(Math.PI / 2);
    return geo;
  }

  private commitSketch(x0: number, y0: number, x1: number, y1: number) {
    this.sketchPreview.visible = false;
    this.commitFoot(this.footprint(x0, y0, x1, y1));
  }

  private commitFoot(foot: { kind: "rect" | "circle" | "slot"; cx: number; cy: number; w: number; d: number }) {
    const h = this.sketchHeight;
    if (foot.w < 1 || foot.d < 1) {
      this.setStatus("That footprint is under 1 mm. Drag a larger one.");
      return;
    }
    if (this.parts.length >= 24) {
      this.setStatus("Twenty-four parts is the limit on this bench.");
      return;
    }
    const span = this.sketchSpan(h);
    const tris =
      foot.kind === "rect"
        ? cube(-foot.w / 2, -foot.d / 2, 0, foot.w, foot.d, span.h)
        : foot.kind === "slot"
          ? stadium(foot.w, foot.d, span.h)
          : cylinder(0, 0, 0, foot.w, span.h, 40);
    const name = foot.kind === "rect" ? "Block" : foot.kind === "slot" ? "Slot" : "Round";
    const across = foot.kind === "circle" ? `Ø${round1(foot.w * 2)}` : `${round1(foot.w)} × ${round1(foot.d)}`;
    if (this.sketchOp === "cut" || this.sketchOp === "top") {
      const stock = this.current();
      if (!stock) {
        this.setStatus(
          this.sketchOp === "cut"
            ? "Select the part you want to cut, then drag the footprint."
            : "Select the part the boss stands on.",
        );
        return;
      }
      this.pushUndo();
      const tool = this.makePart(tris, name, this.sketchOp === "cut" ? "#e85d4c" : "#3e7a45");
      tool.mesh.position.set(foot.cx, foot.cy, span.z0);
      this.selectedId = tool.id;
      this.face = null;
      this.pickPoint = null;
      this.hideFace();
      if (this.triCount(stock) > BOOLEAN_LIMIT) {
        this.attachGizmo();
        this.setStatus(
          `${name} ${across} is aimed at ${stock.name}, but that mesh is over the ${BOOLEAN_LIMIT.toLocaleString()}-triangle cut.`,
        );
        return;
      }
      void this.applyBoolean(stock, tool, this.sketchOp === "cut" ? "subtract" : "union", false);
      return;
    }
    this.pushUndo();
    const part = this.makePart(tris, name);
    part.mesh.position.set(foot.cx, foot.cy, span.z0);
    this.selectedId = part.id;
    this.face = null;
    this.pickPoint = null;
    this.hideFace();
    this.attachGizmo();
    this.setStatus(`${part.name} ${across} × ${round1(h)} mm is on the bed.`);
  }

  private sketchSpan(h: number) {
    if (this.sketchOp === "add") return { z0: 0, h };
    const part = this.current();
    if (!part) return { z0: 0, h };
    part.mesh.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(part.mesh);
    const overlap = 0.4;
    if (this.sketchOp === "top") return { z0: box.max.z - overlap, h: h + overlap };
    const thick = Math.max(0.4, box.max.z - box.min.z);
    const through = h >= thick - 0.05;
    if (through) return { z0: box.min.z - overlap, h: thick + overlap * 2 };
    return { z0: box.max.z - h, h: h + overlap };
  }

  private sketchLift(h: number) {
    const span = this.sketchSpan(h);
    return span.z0 + span.h / 2;
  }

  private leaveSketch(announce: boolean) {
    if (!this.sketch && !this.sketchDrag) return;
    this.sketch = null;
    this.sketchDrag = null;
    this.sketchPreview.visible = false;
    this.orbit.enableRotate = true;
    if (announce) this.setStatus("Sketch is off.");
  }

  private pick(ev: PointerEvent) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    const y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(new THREE.Vector2(x, y), this.camera);
    const hits = this.raycaster.intersectObjects(
      this.parts.filter((p) => p.mesh.visible).map((p) => p.mesh),
      false,
    );
    return hits[0] ?? null;
  }

  private pickBed(ev: PointerEvent) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    const y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(new THREE.Vector2(x, y), this.camera);
    const point = new THREE.Vector3();
    return this.raycaster.ray.intersectPlane(this.bedPlane, point) ? point : null;
  }

  private drawMeasure() {
    this.clearMeasure(false);
    const a = this.measureHits[0];
    const b = this.measureHits[1];
    if (!a || !b) return;
    const geo = new THREE.BufferGeometry().setFromPoints([a, b]);
    const line = new THREE.Line(geo, new THREE.LineBasicMaterial({ color: "#e8b84a", depthTest: false }));
    line.renderOrder = 4;
    this.measureGroup.add(line);
    for (const p of [a, b]) {
      const dot = new THREE.Mesh(
        new THREE.SphereGeometry(1.3, 12, 8),
        new THREE.MeshBasicMaterial({ color: "#f4e8c8", depthTest: false }),
      );
      dot.position.copy(p);
      dot.renderOrder = 5;
      this.measureGroup.add(dot);
    }
  }

  private clearMeasure(resetPoints = true) {
    if (resetPoints) this.measureHits.length = 0;
    for (const child of [...this.measureGroup.children]) {
      this.measureGroup.remove(child);
      const mesh = child as THREE.Mesh;
      mesh.geometry?.dispose();
      const mat = mesh.material as THREE.Material | THREE.Material[] | undefined;
      if (Array.isArray(mat)) mat.forEach((m) => m.dispose());
      else mat?.dispose();
    }
  }

  private buildRoom() {
    const half = BED / 2;
    const bed = new THREE.Mesh(
      new THREE.PlaneGeometry(BED, BED),
      new THREE.MeshStandardMaterial({ color: "#1a1d26", roughness: 0.94, metalness: 0.04 }),
    );
    bed.position.z = -0.45;
    bed.receiveShadow = true;
    this.scene.add(bed);

    const gridPts: number[] = [];
    for (let i = -half; i <= half; i += 10) {
      const major = i % 50 === 0;
      if (major) continue;
      gridPts.push(-half, i, -0.2, half, i, -0.2, i, -half, -0.2, i, half, -0.2);
    }
    const grid = new THREE.LineSegments(
      new THREE.BufferGeometry().setAttribute("position", new THREE.Float32BufferAttribute(gridPts, 3)),
      new THREE.LineBasicMaterial({ color: "#343844" }),
    );
    this.scene.add(grid);

    const frame: number[] = [];
    for (let i = -half; i <= half; i += 50) {
      frame.push(-half, i, -0.15, half, i, -0.15, i, -half, -0.15, i, half, -0.15);
    }
    frame.push(-half, -half, -0.12, half, -half, -0.12, half, -half, -0.12, half, half, -0.12);
    frame.push(half, half, -0.12, -half, half, -0.12, -half, half, -0.12, -half, -half, -0.12);
    this.scene.add(
      new THREE.LineSegments(
        new THREE.BufferGeometry().setAttribute("position", new THREE.Float32BufferAttribute(frame, 3)),
        new THREE.LineBasicMaterial({ color: "#e8b84a" }),
      ),
    );

    this.addAxis(new THREE.Vector3(28, 0, 0), "#e85d4c", "X");
    this.addAxis(new THREE.Vector3(0, 28, 0), "#3e8f96", "Y");
    this.addAxis(new THREE.Vector3(0, 0, 28), "#e8b84a", "Z");

    this.scene.add(new THREE.HemisphereLight("#f4e8c8", "#1a2030", 0.55));
    const key = new THREE.DirectionalLight("#fff1d6", 2.5);
    key.position.set(90, -130, 180);
    key.castShadow = true;
    key.shadow.mapSize.set(2048, 2048);
    key.shadow.camera.near = 40;
    key.shadow.camera.far = 560;
    key.shadow.camera.left = -180;
    key.shadow.camera.right = 180;
    key.shadow.camera.top = 180;
    key.shadow.camera.bottom = -180;
    key.shadow.bias = -0.0004;
    key.shadow.normalBias = 0.04;
    this.scene.add(key);
    const fill = new THREE.DirectionalLight("#8fd0d4", 0.55);
    fill.position.set(-120, 70, 80);
    this.scene.add(fill);
    const rim = new THREE.DirectionalLight("#e8b84a", 0.35);
    rim.position.set(10, 140, 40);
    this.scene.add(rim);
  }

  private addAxis(tip: THREE.Vector3, color: string, label: string) {
    const origin = new THREE.Vector3(-BED / 2 + 8, -BED / 2 + 8, 0);
    const end = origin.clone().add(tip);
    const geo = new THREE.BufferGeometry().setFromPoints([origin, end]);
    this.scene.add(new THREE.Line(geo, new THREE.LineBasicMaterial({ color, depthTest: false })));
    const canvas = document.createElement("canvas");
    canvas.width = 64;
    canvas.height = 64;
    const g = canvas.getContext("2d");
    if (!g) return;
    g.fillStyle = color;
    g.font = "700 42px Outfit, sans-serif";
    g.textAlign = "center";
    g.textBaseline = "middle";
    g.fillText(label, 32, 34);
    const sprite = new THREE.Sprite(
      new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(canvas), depthTest: false }),
    );
    sprite.position.copy(end);
    sprite.scale.set(10, 10, 1);
    sprite.renderOrder = 3;
    this.scene.add(sprite);
  }

  private resize = () => {
    const canvas = this.renderer.domElement;
    const w = canvas.clientWidth || 1;
    const h = canvas.clientHeight || 1;
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  };

  private hangsOff(part: Part) {
    const box = new THREE.Box3().setFromObject(part.mesh);
    const h = BED / 2;
    return box.min.x < -h - 0.2 || box.max.x > h + 0.2 || box.min.y < -h - 0.2 || box.max.y > h + 0.2 || box.min.z < -0.4;
  }

  private sectionLimit() {
    let max = 40;
    for (const part of this.parts) {
      const box = new THREE.Box3().setFromObject(part.mesh);
      if (Number.isFinite(box.max.z) && box.max.z > max) max = box.max.z;
    }
    return Math.ceil(max + 2);
  }

  private syncSection() {
    this.clipPlane.constant = this.sectionZ;
    this.sectionSheet.position.z = this.sectionZ + 0.05;
    this.sectionSheet.visible = this.sectionOn;
    this.renderer.localClippingEnabled = this.sectionOn;
  }

  private setStatus(status: string) {
    this.status = status;
    this.emit();
  }

  private emit() {
    for (const part of this.parts) this.applyShade(part);
    const selected = this.current();
    const stock = this.stock();
    const parts: PartInfo[] = this.parts.map((part) => {
      const size = this.sizeOf(part);
      return {
        id: part.id,
        name: part.name,
        color: part.color,
        tris: this.triCount(part),
        visible: part.mesh.visible,
        selected: part.id === this.selectedId,
        size: [round1(size[0]), round1(size[1]), round1(size[2])],
        pos: [round1(part.mesh.position.x), round1(part.mesh.position.y), round1(part.mesh.position.z)],
        rot: [deg(part.mesh.rotation.x), deg(part.mesh.rotation.y), deg(part.mesh.rotation.z)],
        scale: [round1(part.mesh.scale.x), round1(part.mesh.scale.y), round1(part.mesh.scale.z)],
        volume: part.volume,
        offBed: this.hangsOff(part),
      };
    });
    const next: BenchSnapshot = {
      parts,
      status: this.status,
      tool: this.tool,
      shade: this.shade,
      snap: this.snap,
      measuring: this.measuring,
      stockName: stock && stock.id !== selected?.id ? stock.name : null,
      cutterName: selected?.name ?? null,
      face: this.face,
      busy: this.busy,
      section: this.sectionOn,
      sectionZ: round1(this.sectionZ),
      sectionMax: this.sectionLimit(),
      sketch: this.sketch,
      sketchHeight: round1(this.sketchHeight),
      sketchOp: this.sketchOp,
      pulling: this.pulling,
    };
    this.snapshot = next;
    for (const fn of this.listeners) fn(next);
  }
}

function fileName(name: string) {
  const safe = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
  return (safe || "part") + ".stl";
}
