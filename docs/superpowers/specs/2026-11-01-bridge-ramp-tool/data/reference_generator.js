// Reference generator for the bridge/ramp tool spec. It reads only bridge_kits.json and ramp_kits.json.
// It is a model for the C++ implementation, not editor code.
//
//   node reference_generator.js bridge <tileset> <d|u> <lengthSize> <widthSize> [seed]
//   node reference_generator.js ramp <tileset> <N|S|E|W|NE|NW|SE|SW> <widthSize>
//
// Output: JSON {width, height, tiles:[[x, y, value], ...]} with x, y relative to the top-left tile of the result.
// Sizes: 0.5, 1, 2, 3, ... (0.5 = the ends only, n = n middle pieces).
const fs = require('fs');
const path = require('path');
const BRIDGES = JSON.parse(fs.readFileSync(path.join(__dirname, 'bridge_kits.json'), 'utf8'));
const RAMPS = JSON.parse(fs.readFileSync(path.join(__dirname, 'ramp_kits.json'), 'utf8'));

const pieces = size => (size === 0.5 ? 0 : size);

function rng(seed) {
    let s = seed >>> 0 || 1;
    return () => (s = (s * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
}

// The class of one coordinate along one axis: S<i> (start cap), F<i> (far cap) or M<phase> (middle).
function cls(w, extent, capStart, capFar, period) {
    if (w < capStart) return 'S' + w;
    if (extent - 1 - w < capFar) return 'F' + (extent - 1 - w);
    return 'M' + ((w - capStart) % period);
}

function bridge(tileset, dir, lengthSize, widthSize, seed = 1) {
    const k = BRIDGES.tilesets[tileset][dir];
    const random = rng(seed);
    // Extent along each axis, in steps of the axis vector (2 steps = one isometric cell).
    const extL = k.caps.start + k.caps.far + k.middleAtSizeHalf.length + 2 * pieces(lengthSize);
    const extW = k.caps.side0 + k.caps.side1 + k.middleAtSizeHalf.width + 2 * pieces(widthSize);
    const L = k.step.length, W = k.step.width, det = L[0] * W[1] - L[1] * W[0];
    const tiles = [];
    for (let a = 0; a < extL; a++)
        for (let c = 0; c < extW; c++) {
            const wl = a + k.start.length, ww = c + k.start.width;
            const P = (wl * W[1] - ww * L[1]) / det, y = (L[0] * ww - W[0] * wl) / det;
            if (!Number.isInteger(P) || !Number.isInteger(y)) continue;
            const key = cls(a, extL, k.caps.start, k.caps.far, k.period.length) + '|' + cls(c, extW, k.caps.side0, k.caps.side1, k.period.width);
            const choices = k.table[key];
            if (!choices) continue;
            // Weighted random choice among the variants the reference bridges use for this key.
            const total = Object.values(choices).reduce((s, n) => s + n, 0);
            let r = random() * total, pick = null;
            for (const [v, n] of Object.entries(choices)) { if ((r -= n) <= 0) { pick = v; break; } }
            if (pick === null) pick = Object.keys(choices)[0];
            if (pick === 'none') continue;
            const [t0, t1] = pick.split(',').map(Number);
            tiles.push([2 * P, y, t0], [2 * P + 1, y, t1]);
        }
    return normalize(tiles);
}

function ramp(tileset, orientation, widthSize) {
    const o = RAMPS.tilesets[tileset].orientations[orientation];
    const n = pieces(widthSize);
    const parts = [[o.wallA, [0, 0]]];
    for (let i = 0; i < n; i++) parts.push([o.slope, [o.start[0] + i * o.step[0], o.start[1] + i * o.step[1]]]);
    parts.push([o.wallB, [o.start[0] + n * o.step[0], o.start[1] + n * o.step[1]]]);
    const tiles = [];
    for (const [piece, at] of parts) for (const [x, y, v] of piece.tiles) tiles.push([at[0] + x, at[1] + y, v]);
    return normalize(tiles);
}

function normalize(tiles) {
    const x0 = Math.min(...tiles.map(t => t[0])), y0 = Math.min(...tiles.map(t => t[1]));
    // Keep tile pairs on even columns: shift by an even number of columns only.
    const sx = x0 - (((x0 % 2) + 2) % 2);
    const out = tiles.map(([x, y, v]) => [x - sx, y - y0, v]);
    return { width: Math.max(...out.map(t => t[0])) + 1, height: Math.max(...out.map(t => t[1])) + 1, tiles: out };
}

module.exports = { bridge, ramp, BRIDGES, RAMPS };

if (require.main === module) {
    const [kind, ts, a, b, c, d] = process.argv.slice(2);
    const r = kind === 'bridge' ? bridge(ts, a, Number(b), Number(c), Number(d || 1)) : ramp(ts, a, Number(b));
    console.log(JSON.stringify(r));
}
