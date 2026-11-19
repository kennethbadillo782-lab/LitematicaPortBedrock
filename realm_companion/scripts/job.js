import { parseBlockSpec } from './protocol.js';

/**
 * LP3 job stream (hex of bytes): u16 headerLength, header JSON {"d": dimension, "p": [block specs]}, then chunk groups:
 * i32 chunkX, i32 chunkZ, u16 count, count * 7-byte cuboids [x4|z4, u16 y+128, (w-1)4|(d-1)4, h-1, u16 palette].
 * All cuboids stay inside their chunk.
 * @param {string} hex
 */
export function decodeJob(hex) {
    if (typeof hex !== 'string' || hex.length % 2 !== 0 || hex.length < 8 || hex.length > 6000000) throw new Error('Invalid job size');
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; ++i) bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
    const view = new DataView(bytes.buffer);
    const headerLength = view.getUint16(0, true);
    if (headerLength < 2 || 2 + headerLength > bytes.length) throw new Error('Invalid job header');
    let headerText = '';
    for (let i = 0; i < headerLength; ++i) headerText += String.fromCharCode(bytes[2 + i]);
    const header = JSON.parse(headerText);
    if (!header || !['overworld', 'nether', 'the_end'].includes(header.d) || !Array.isArray(header.p) || header.p.length < 1 || header.p.length > 4096)
        throw new Error('Invalid job header');
    const palette = header.p.map(spec => { if (typeof spec !== 'string' || spec.length > 600) throw new Error('Invalid palette'); return parseBlockSpec(spec); });
    let at = 2 + headerLength, total = 0;
    const chunks = [];
    while (at < bytes.length) {
        if (at + 10 > bytes.length) throw new Error('Truncated job');
        const cx = view.getInt32(at, true), cz = view.getInt32(at + 4, true), count = view.getUint16(at + 8, true);
        at += 10;
        if (Math.abs(cx) > 1875000 || Math.abs(cz) > 1875000 || at + count * 7 > bytes.length || chunks.length >= 16384) throw new Error('Invalid chunk group');
        const cuboids = [];
        for (let i = 0; i < count; ++i, at += 7) {
            const x = bytes[at] >> 4, z = bytes[at] & 15, y = view.getUint16(at + 1, true) - 128;
            const w = (bytes[at + 3] >> 4) + 1, d = (bytes[at + 3] & 15) + 1, h = bytes[at + 4] + 1, p = view.getUint16(at + 5, true);
            if (x + w > 16 || z + d > 16 || p >= palette.length || y < -64 || y + h > 4096) throw new Error('Invalid cuboid');
            total += w * h * d;
            cuboids.push({ x: cx * 16 + x, y, z: cz * 16 + z, w, h, d, p });
        }
        if (cuboids.length) chunks.push({ cx, cz, cuboids, cursor: 0 });
    }
    if (total > 20000000) throw new Error('Job too large');
    return { dimension: header.d, palette, chunks, total };
}
