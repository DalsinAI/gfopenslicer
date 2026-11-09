/* Cube, cylinder, and the three joins. The same BSP as the Amiga side:
 * split a polygon by a plane, drop the side that does not belong, join
 * the surfaces. Not a translation of another modeller.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
// @ts-nocheck

  var VMAX = 16;
  var EPS = 1e-5;

  function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
  function cross(a, b) {
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  }
  function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  function unit(n) {
    var L = Math.hypot(n[0], n[1], n[2]);
    if (L < 1e-12) return null;
    return [n[0] / L, n[1] / L, n[2] / L];
  }

  function bagAdd(b, p) {
    if (p.n < 3 || p.n > VMAX) return 0;
    b.push({ n: p.n, v: p.v.slice(0, p.n) });
    return 0;
  }

  function planeOf(p) {
    if (p.n < 3) return null;
    var n = unit(cross(sub(p.v[1], p.v[0]), sub(p.v[2], p.v[0])));
    if (!n) return null;
    return { n: n, w: dot(n, p.v[0]) };
  }

  function side(nd, v) {
    return nd.nrm[0] * v[0] + nd.nrm[1] * v[1] + nd.nrm[2] * v[2] - nd.w;
  }

  function flipPoly(p) {
    var i, h = (p.n / 2) | 0, t;
    for (i = 0; i < h; i++) {
      t = p.v[i];
      p.v[i] = p.v[p.n - 1 - i];
      p.v[p.n - 1 - i] = t;
    }
  }

  function splitPoly(nd, inn, front, back) {
    var i, nf = 0, nb = 0, d = [], c = [], f, b, j, t, p, pl;
    for (i = 0; i < inn.n; i++) {
      d[i] = side(nd, inn.v[i]);
      if (d[i] > EPS) { c[i] = 1; nf++; }
      else if (d[i] < -EPS) { c[i] = -1; nb++; }
      else c[i] = 0;
    }
    if (!nf && !nb) {
      pl = planeOf(inn);
      if (!pl) return 0;
      return bagAdd(dot(pl.n, nd.nrm) > 0 ? front : back, inn);
    }
    if (!nb) return bagAdd(front, inn);
    if (!nf) return bagAdd(back, inn);
    f = { n: 0, v: [] };
    b = { n: 0, v: [] };
    for (i = 0; i < inn.n; i++) {
      j = (i + 1) % inn.n;
      if (c[i] !== -1 && f.n < VMAX) { f.v[f.n++] = inn.v[i]; }
      if (c[i] !== 1 && b.n < VMAX) { b.v[b.n++] = inn.v[i]; }
      if ((c[i] === 1 && c[j] === -1) || (c[i] === -1 && c[j] === 1)) {
        t = d[i] / (d[i] - d[j]);
        p = [
          inn.v[i][0] + (inn.v[j][0] - inn.v[i][0]) * t,
          inn.v[i][1] + (inn.v[j][1] - inn.v[i][1]) * t,
          inn.v[i][2] + (inn.v[j][2] - inn.v[i][2]) * t
        ];
        if (f.n < VMAX) f.v[f.n++] = p;
        if (b.n < VMAX) b.v[b.n++] = p;
      }
    }
    if (bagAdd(front, f) || bagAdd(back, b)) return -1;
    return 0;
  }

  function nodeBuild(n, src) {
    var front = [], back = [], i, p, vi, on, out, inn, d;
    if (!src.length) return 0;
    if (!n.has) {
      var pl = planeOf(src[0]);
      if (!pl) return 0;
      n.nrm = pl.n;
      n.w = pl.w;
      n.has = 1;
    }
    for (i = 0; i < src.length; i++) {
      p = src[i];
      on = 1; out = 0; inn = 0;
      for (vi = 0; vi < p.n; vi++) {
        d = side(n, p.v[vi]);
        if (d > EPS) { out = 1; on = 0; }
        else if (d < -EPS) { inn = 1; on = 0; }
      }
      if (on) { if (bagAdd(n.co, p)) return -1; continue; }
      if (out && !inn) { if (bagAdd(front, p)) return -1; continue; }
      if (inn && !out) { if (bagAdd(back, p)) return -1; continue; }
      if (splitPoly(n, p, front, back)) return -1;
    }
    if (front.length) {
      if (!n.front) n.front = { has: 0, co: [], front: null, back: null };
      if (nodeBuild(n.front, front)) return -1;
    }
    if (back.length) {
      if (!n.back) n.back = { has: 0, co: [], front: null, back: null };
      if (nodeBuild(n.back, back)) return -1;
    }
    return 0;
  }

  function nodeClip(n, src, out) {
    var front = [], back = [], i;
    if (!n || !n.has) {
      for (i = 0; i < src.length; i++) if (bagAdd(out, src[i])) return -1;
      return 0;
    }
    for (i = 0; i < src.length; i++) {
      if (splitPoly(n, src[i], front, back)) return -1;
    }
    if (n.front) { if (nodeClip(n.front, front, out)) return -1; }
    else { for (i = 0; i < front.length; i++) if (bagAdd(out, front[i])) return -1; }
    if (n.back) { if (nodeClip(n.back, back, out)) return -1; }
    return 0;
  }

  function nodeClipTo(n, bsp) {
    var next;
    if (!n) return 0;
    next = [];
    if (nodeClip(bsp, n.co, next)) return -1;
    n.co = next;
    if (nodeClipTo(n.front, bsp)) return -1;
    if (nodeClipTo(n.back, bsp)) return -1;
    return 0;
  }

  function nodeInvert(n) {
    var t, i;
    if (!n) return;
    for (i = 0; i < n.co.length; i++) flipPoly(n.co[i]);
    if (n.has) {
      n.nrm = [-n.nrm[0], -n.nrm[1], -n.nrm[2]];
      n.w = -n.w;
    }
    t = n.front;
    n.front = n.back;
    n.back = t;
    nodeInvert(n.front);
    nodeInvert(n.back);
  }

  function nodeAll(n, out) {
    var i;
    if (!n) return 0;
    for (i = 0; i < n.co.length; i++) if (bagAdd(out, n.co[i])) return -1;
    if (nodeAll(n.front, out)) return -1;
    if (nodeAll(n.back, out)) return -1;
    return 0;
  }

  function fromMesh(flat) {
    var b = [], i, o;
    for (i = 0; i < flat.length; i += 9) {
      o = i;
      b.push({
        n: 3,
        v: [
          [flat[o], flat[o + 1], flat[o + 2]],
          [flat[o + 3], flat[o + 4], flat[o + 5]],
          [flat[o + 6], flat[o + 7], flat[o + 8]]
        ]
      });
    }
    return b;
  }

  function meshFrom(b) {
    var count = 0, i, k, out, n = 0;
    for (i = 0; i < b.length; i++) if (b[i].n >= 3) count += b[i].n - 2;
    out = new Float32Array(count * 9);
    for (i = 0; i < b.length; i++) {
      var p = b[i];
      for (k = 1; k + 1 < p.n; k++) {
        out[n++] = p.v[0][0]; out[n++] = p.v[0][1]; out[n++] = p.v[0][2];
        out[n++] = p.v[k][0]; out[n++] = p.v[k][1]; out[n++] = p.v[k][2];
        out[n++] = p.v[k + 1][0]; out[n++] = p.v[k + 1][1]; out[n++] = p.v[k + 1][2];
      }
    }
    return out;
  }

  function csg(a, b, op) {
    var A = { has: 0, co: [], front: null, back: null };
    var B = { has: 0, co: [], front: null, back: null };
    var all;
    if (nodeBuild(A, fromMesh(a)) || nodeBuild(B, fromMesh(b))) return null;
    if (op !== 0) nodeInvert(A);
    if (op === 2) nodeInvert(B);
    if (nodeClipTo(A, B) || nodeClipTo(B, A)) return null;
    nodeInvert(B);
    if (nodeClipTo(B, A)) return null;
    nodeInvert(B);
    all = [];
    if (nodeAll(B, all) || nodeBuild(A, all)) return null;
    if (op !== 0) nodeInvert(A);
    all = [];
    if (nodeAll(A, all)) return null;
    return meshFrom(all);
  }

  function tri(t, ax, ay, az, bx, by, bz, cx, cy, cz) {
    t.push(ax, ay, az, bx, by, bz, cx, cy, cz);
  }

  function cube(x, y, z, sx, sy, sz) {
    var x1 = x + sx, y1 = y + sy, z1 = z + sz, t = [];
    tri(t, x, y, z1, x1, y, z1, x1, y1, z1);
    tri(t, x, y, z1, x1, y1, z1, x, y1, z1);
    tri(t, x, y, z, x, y1, z, x1, y1, z);
    tri(t, x, y, z, x1, y1, z, x1, y, z);
    tri(t, x, y, z, x, y, z1, x, y1, z1);
    tri(t, x, y, z, x, y1, z1, x, y1, z);
    tri(t, x1, y, z, x1, y1, z, x1, y1, z1);
    tri(t, x1, y, z, x1, y1, z1, x1, y, z1);
    tri(t, x, y, z, x1, y, z, x1, y, z1);
    tri(t, x, y, z, x1, y, z1, x, y, z1);
    tri(t, x, y1, z, x, y1, z1, x1, y1, z1);
    tri(t, x, y1, z, x1, y1, z1, x1, y1, z);
    return Float32Array.from(t);
  }

  function cylinder(x, y, z, r, h, seg) {
    var z1 = z + h, t = [], i;
    if (seg < 3) seg = 3;
    for (i = 0; i < seg; i++) {
      var a0 = i / seg * Math.PI * 2;
      var a1 = (i + 1) / seg * Math.PI * 2;
      var x0 = x + r * Math.cos(a0), y0 = y + r * Math.sin(a0);
      var x2 = x + r * Math.cos(a1), y2 = y + r * Math.sin(a1);
      tri(t, x0, y0, z, x2, y2, z, x2, y2, z1);
      tri(t, x0, y0, z, x2, y2, z1, x0, y0, z1);
      tri(t, x, y, z1, x0, y0, z1, x2, y2, z1);
      tri(t, x, y, z, x2, y2, z, x0, y0, z);
    }
    return Float32Array.from(t);
  }

  function volume(flat) {
    var s = 0, i;
    for (i = 0; i < flat.length; i += 9) {
      var ax = flat[i], ay = flat[i + 1], az = flat[i + 2];
      var bx = flat[i + 3], by = flat[i + 4], bz = flat[i + 5];
      var cx = flat[i + 6], cy = flat[i + 7], cz = flat[i + 8];
      s += ax * (by * cz - cy * bz);
      s += bx * (cy * az - ay * cz);
      s += cx * (ay * bz - by * az);
    }
    return Math.abs(s) / 6;
  }

function union(a, b) { return csg(a, b, 0); }
function subtract(a, b) { return csg(a, b, 1); }
function intersect(a, b) { return csg(a, b, 2); }

export { cube, cylinder, union, subtract, intersect, volume };
