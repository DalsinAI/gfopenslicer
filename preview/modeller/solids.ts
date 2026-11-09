import { cube, cylinder, union } from "./gs.js";

export type SolidKind = "box" | "cylinder" | "sphere" | "cone" | "torus" | "wedge" | "slot";

function tri(out: number[], ax: number, ay: number, az: number, bx: number, by: number, bz: number, cx: number, cy: number, cz: number) {
  out.push(ax, ay, az, bx, by, bz, cx, cy, cz);
}

function sphere(r: number): Float32Array {
  const seg = 28;
  const rings = 16;
  const out: number[] = [];
  const p = (v: number, u: number) =>
    [r * Math.sin(v) * Math.cos(u), r * Math.sin(v) * Math.sin(u), r * Math.cos(v)] as const;
  for (let i = 0; i < rings; i++) {
    const v0 = (i / rings) * Math.PI;
    const v1 = ((i + 1) / rings) * Math.PI;
    for (let j = 0; j < seg; j++) {
      const u0 = (j / seg) * Math.PI * 2;
      const u1 = ((j + 1) / seg) * Math.PI * 2;
      const a = p(v0, u0);
      const b = p(v0, u1);
      const c = p(v1, u0);
      const d = p(v1, u1);
      tri(out, a[0], a[1], a[2], c[0], c[1], c[2], d[0], d[1], d[2]);
      tri(out, a[0], a[1], a[2], d[0], d[1], d[2], b[0], b[1], b[2]);
    }
  }
  return Float32Array.from(out);
}

function cone(r: number, h: number): Float32Array {
  const seg = 32;
  const out: number[] = [];
  const z0 = -h / 2;
  const z1 = h / 2;
  for (let i = 0; i < seg; i++) {
    const a0 = (i / seg) * Math.PI * 2;
    const a1 = ((i + 1) / seg) * Math.PI * 2;
    const x0 = r * Math.cos(a0);
    const y0 = r * Math.sin(a0);
    const x1 = r * Math.cos(a1);
    const y1 = r * Math.sin(a1);
    tri(out, x0, y0, z0, 0, 0, z1, x1, y1, z0);
    tri(out, 0, 0, z0, x1, y1, z0, x0, y0, z0);
  }
  return Float32Array.from(out);
}

function torus(R: number, tube: number): Float32Array {
  const seg = 28;
  const tubeSeg = 14;
  const out: number[] = [];
  const at = (i: number, j: number) => {
    const u = (i / seg) * Math.PI * 2;
    const v = (j / tubeSeg) * Math.PI * 2;
    const cx = Math.cos(u);
    const cy = Math.sin(u);
    const rr = R + tube * Math.cos(v);
    return [rr * cx, rr * cy, tube * Math.sin(v)] as const;
  };
  for (let i = 0; i < seg; i++) {
    for (let j = 0; j < tubeSeg; j++) {
      const a = at(i, j);
      const b = at(i + 1, j);
      const c = at(i, j + 1);
      const d = at(i + 1, j + 1);
      tri(out, a[0], a[1], a[2], c[0], c[1], c[2], d[0], d[1], d[2]);
      tri(out, a[0], a[1], a[2], d[0], d[1], d[2], b[0], b[1], b[2]);
    }
  }
  return Float32Array.from(out);
}

function wedge(): Float32Array {
  const x0 = -14, x1 = 14, y0 = -10, y1 = 10, z0 = -6, z1 = 8;
  const out: number[] = [];
  // Slope from (x0, z1) down to (x1, z0).
  tri(out, x0, y0, z0, x1, y0, z0, x0, y0, z1);
  tri(out, x0, y1, z1, x1, y1, z0, x0, y1, z0);
  tri(out, x0, y0, z0, x0, y0, z1, x0, y1, z1);
  tri(out, x0, y0, z0, x0, y1, z1, x0, y1, z0);
  tri(out, x0, y0, z0, x0, y1, z0, x1, y1, z0);
  tri(out, x0, y0, z0, x1, y1, z0, x1, y0, z0);
  tri(out, x0, y0, z1, x1, y0, z0, x1, y1, z0);
  tri(out, x0, y0, z1, x1, y1, z0, x0, y1, z1);
  return Float32Array.from(out);
}

function slot(): Float32Array {
  return stadium(38, 10, 8);
}

/** A stadium. Length and width are the overall sizes. Z runs from 0 up to `height`. */
export function stadium(length: number, width: number, height: number): Float32Array {
  const alongX = length >= width;
  const L = Math.max(length, width);
  const W = Math.min(length, Math.max(width, 0.4));
  const r = W / 2;
  const straight = Math.max(0, L - W);
  const hx = straight / 2;
  const body =
    straight > 0.15 ? cube(-hx, -r, 0, straight, W, height) : cylinder(0, 0, 0, r, height, 24);
  let tris = body;
  if (straight > 0.15) {
    const left = cylinder(-hx, 0, 0, r, height, 20);
    const right = cylinder(hx, 0, 0, r, height, 20);
    const one = union(body, left);
    const two = one ? union(one, right) : null;
    if (two && two.length > 36) tris = two;
  }
  if (alongX) return tris;
  const out = new Float32Array(tris.length);
  for (let i = 0; i < tris.length; i += 3) {
    out[i] = -tris[i + 1]!;
    out[i + 1] = tris[i]!;
    out[i + 2] = tris[i + 2]!;
  }
  return out;
}

export function makeSolid(kind: SolidKind): Float32Array {
  if (kind === "box") return cube(-12, -12, -8, 24, 24, 16);
  if (kind === "cylinder") return cylinder(0, 0, -11, 10, 22, 40);
  if (kind === "sphere") return sphere(12);
  if (kind === "cone") return cone(12, 24);
  if (kind === "torus") return torus(12, 4);
  if (kind === "slot") return slot();
  return wedge();
}

export const SOLID_LABEL: Record<SolidKind, string> = {
  box: "Box",
  cylinder: "Cylinder",
  sphere: "Sphere",
  cone: "Cone",
  torus: "Torus",
  wedge: "Wedge",
  slot: "Slot",
};
