export type ShelfItem = {
  file: string;
  name: string;
  note: string;
};

export const SHELF: ShelfItem[] = [
  { file: "/library/nasa-wrench.stl", name: "NASA wrench", note: "Public domain · 114 mm" },
  { file: "/library/galileo.stl", name: "Galileo", note: "Public domain · 13 mm print" },
  { file: "/library/jwst-body.stl", name: "James Webb body", note: "Public domain · NASA" },
  { file: "/library/curiosity-body.stl", name: "Curiosity body", note: "Public domain · NASA" },
  { file: "/library/hubble-body.stl", name: "Hubble body", note: "Public domain · NASA" },
  { file: "/library/voyager-dish.stl", name: "Voyager dish", note: "Public domain · NASA" },
  { file: "/library/utah-teapot.stl", name: "Utah teapot", note: "CC0 solid" },
  { file: "/library/bracket.stl", name: "Angle bracket", note: "Workshop · watertight" },
  { file: "/library/knob.stl", name: "Knob", note: "Workshop · watertight" },
  { file: "/library/spur-gear.stl", name: "Spur gear", note: "Workshop · watertight" },
  { file: "/library/hole-block.stl", name: "Block with hole", note: "Workshop · 20 mm" },
];

export const BED = 220;
export const BOOLEAN_LIMIT = 2500;
export const METALS = ["#c4a15a", "#d7c4a2", "#c47a5a", "#8aa0a8", "#3e8f96", "#a3b18a"] as const;
