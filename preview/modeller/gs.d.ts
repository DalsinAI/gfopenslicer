export function cube(
  x: number,
  y: number,
  z: number,
  sx: number,
  sy: number,
  sz: number,
): Float32Array;
export function cylinder(
  x: number,
  y: number,
  z: number,
  r: number,
  h: number,
  seg: number,
): Float32Array;
export function union(a: ArrayLike<number>, b: ArrayLike<number>): Float32Array | null;
export function subtract(a: ArrayLike<number>, b: ArrayLike<number>): Float32Array | null;
export function intersect(a: ArrayLike<number>, b: ArrayLike<number>): Float32Array | null;
export function volume(flat: ArrayLike<number>): number;
