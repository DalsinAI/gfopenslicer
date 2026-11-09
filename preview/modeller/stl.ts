const VERTEX =
  /vertex\s+([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s+([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s+([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)/g;

export function parseSTL(buf: ArrayBuffer): Float32Array {
  const dv = new DataView(buf);
  if (buf.byteLength >= 84) {
    const n = dv.getUint32(80, true);
    if (n > 0 && n < 5_000_000 && buf.byteLength === 84 + n * 50) {
      const out = new Float32Array(n * 9);
      for (let i = 0; i < n; i++) {
        const o = 84 + i * 50 + 12;
        for (let k = 0; k < 9; k++) out[i * 9 + k] = dv.getFloat32(o + k * 4, true);
      }
      return out;
    }
  }
  const text = new TextDecoder("latin1").decode(new Uint8Array(buf));
  if (!/vertex/i.test(text)) throw new Error("not-stl");
  const nums: number[] = [];
  VERTEX.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = VERTEX.exec(text))) nums.push(+m[1], +m[2], +m[3]);
  if (nums.length < 9 || nums.length % 9 !== 0) throw new Error("empty-stl");
  return Float32Array.from(nums);
}

export function encodeSTL(tris: Float32Array): ArrayBuffer {
  const n = Math.floor(tris.length / 9);
  const buf = new ArrayBuffer(84 + n * 50);
  const dv = new DataView(buf);
  const head = "GFOpenMake";
  for (let i = 0; i < head.length; i++) dv.setUint8(i, head.charCodeAt(i));
  dv.setUint32(80, n, true);
  for (let i = 0; i < n; i++) {
    const a = i * 9;
    const ax = tris[a] ?? 0, ay = tris[a + 1] ?? 0, az = tris[a + 2] ?? 0;
    const bx = tris[a + 3] ?? 0, by = tris[a + 4] ?? 0, bz = tris[a + 5] ?? 0;
    const cx = tris[a + 6] ?? 0, cy = tris[a + 7] ?? 0, cz = tris[a + 8] ?? 0;
    const ux = bx - ax, uy = by - ay, uz = bz - az;
    const vx = cx - ax, vy = cy - ay, vz = cz - az;
    let nx = uy * vz - uz * vy;
    let ny = uz * vx - ux * vz;
    let nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz) || 1;
    nx /= len;
    ny /= len;
    nz /= len;
    const base = 84 + i * 50;
    dv.setFloat32(base, nx, true);
    dv.setFloat32(base + 4, ny, true);
    dv.setFloat32(base + 8, nz, true);
    for (let k = 0; k < 9; k++) dv.setFloat32(base + 12 + k * 4, tris[a + k] ?? 0, true);
  }
  return buf;
}

/** Metres become millimetres. Huge scans are brought onto a 160 mm bed. */
export function normalizeUnits(pos: Float32Array): { positions: Float32Array; note: string } {
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < pos.length; i += 3) {
    const x = pos[i] ?? 0, y = pos[i + 1] ?? 0, z = pos[i + 2] ?? 0;
    if (x < minX) minX = x;
    if (y < minY) minY = y;
    if (z < minZ) minZ = z;
    if (x > maxX) maxX = x;
    if (y > maxY) maxY = y;
    if (z > maxZ) maxZ = z;
  }
  const span = Math.max(maxX - minX, maxY - minY, maxZ - minZ);
  let scale = 1;
  let note = "";
  if (span > 0 && span < 2) {
    scale = 1000;
    note = "Read as metres and shown in millimetres.";
  } else if (span > 2500) {
    scale = 160 / span;
    note = "Scaled down to 160 mm so it sits on the bed.";
  }
  if (scale !== 1) {
    const out = new Float32Array(pos.length);
    for (let i = 0; i < pos.length; i++) out[i] = (pos[i] ?? 0) * scale;
    return { positions: out, note };
  }
  return { positions: pos, note };
}

/** Keep a readable shell when a scan is far past what booleans, or even the GPU budget, wants. */
export function thinTriangles(pos: Float32Array, target: number): Float32Array {
  const n = Math.floor(pos.length / 9);
  const step = Math.ceil(n / target);
  if (step <= 1) return pos;
  const out = new Float32Array(Math.ceil(n / step) * 9);
  let w = 0;
  for (let i = 0; i < n; i += step) {
    out.set(pos.subarray(i * 9, i * 9 + 9), w);
    w += 9;
  }
  return out.slice(0, w);
}

const cache = new Map<string, Promise<ArrayBuffer>>();

export function loadBuffer(url: string): Promise<ArrayBuffer> {
  let pending = cache.get(url);
  if (!pending) {
    pending = fetch(url).then((res) => {
      if (!res.ok) throw new Error(String(res.status));
      return res.arrayBuffer();
    });
    cache.set(url, pending);
  }
  return pending;
}
