/**
 * Generate PNG RGB fixtures for media-img tests (no extra npm deps).
 * Run: node tests/assets/build-fixtures.mjs
 */
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

const __dirname = dirname(fileURLToPath(import.meta.url));

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i];
    for (let j = 0; j < 8; j++) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
  }
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const typeBuf = Buffer.from(type, 'ascii');
  const crcInput = Buffer.concat([typeBuf, data]);
  const c = crc32(crcInput);
  const crcBuf = Buffer.alloc(4);
  crcBuf.writeUInt32BE(c >>> 0, 0);
  return Buffer.concat([len, typeBuf, data, crcBuf]);
}

function ihdr(w, h) {
  const b = Buffer.alloc(13);
  b.writeUInt32BE(w, 0);
  b.writeUInt32BE(h, 4);
  b[8] = 8;
  b[9] = 2;
  b[10] = 0;
  b[11] = 0;
  b[12] = 0;
  return b;
}

/** Raw RGB scanlines: filter 0 + width*3 bytes per row */
function rawRgb(w, h, pixel) {
  const row = 1 + w * 3;
  const buf = Buffer.alloc(row * h);
  const fn =
    typeof pixel === 'function'
      ? pixel
      : (x, y) => {
          const p = pixel;
          return [p[0], p[1], p[2]];
        };
  for (let y = 0; y < h; y++) {
    const off = y * row;
    buf[off] = 0;
    for (let x = 0; x < w; x++) {
      const [r, g, b] = fn(x, y);
      const p = off + 1 + x * 3;
      buf[p] = r;
      buf[p + 1] = g;
      buf[p + 2] = b;
    }
  }
  return buf;
}

function encodePng(w, h, pixel) {
  const raw = rawRgb(w, h, pixel);
  const idat = deflateSync(raw);
  const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  return Buffer.concat([sig, chunk('IHDR', ihdr(w, h)), chunk('IDAT', idat), chunk('IEND', Buffer.alloc(0))]);
}

function writeFixture(name, w, h, pixel) {
  const p = join(__dirname, name);
  writeFileSync(p, encodePng(w, h, pixel));
  console.log(`wrote ${name} (${w}x${h})`);
}

mkdirSync(__dirname, { recursive: true });

writeFixture('tiny-1x1.png', 1, 1, [255, 0, 0]);
writeFixture('tiny-8x8.png', 8, 8, [40, 80, 160]);
writeFixture('square-64.png', 64, 64, (x, y) => {
  const v = ((x + y) * 4) & 255;
  return [v, 128, 255 - v];
});
writeFixture('wide-320x80.png', 320, 80, (x) => {
  const r = (x * 255) / 319;
  return [r & 255, 90, 200];
});
writeFixture('tall-80x320.png', 80, 320, (x, y) => {
  const g = (y * 255) / 319;
  return [30, g & 255, 100];
});
writeFixture('mid-256x256.png', 256, 256, (x, y) => {
  const cx = x - 128;
  const cy = y - 128;
  const d = Math.sqrt(cx * cx + cy * cy);
  const v = Math.min(255, Math.floor(d));
  return [v, 255 - v, (x + y) & 255];
});
writeFixture('photo-ish-640x360.png', 640, 360, (x, y) => {
  return [
    (x * 31) & 255,
    (y * 17 + x) & 255,
    ((x + y) * 13) & 255,
  ];
});
writeFixture('stripes-512x64.png', 512, 64, (x) => {
  const band = Math.floor(x / 32) % 3;
  return band === 0 ? [220, 40, 40] : band === 1 ? [40, 200, 60] : [40, 80, 220];
});
writeFixture('checker-128x128.png', 128, 128, (x, y) => {
  const c = (Math.floor(x / 16) + Math.floor(y / 16)) % 2 === 0 ? 240 : 20;
  return [c, c, c];
});

// Pixlwiz `pm-image service upload` integration (orchestrator/test-service.mjs).
mkdirSync(join(__dirname, 'service'), { recursive: true });
writeFixture(join('service', 'upload-a.png'), 32, 32, (x, y) => {
  return [(x * 8) & 255, (y * 8) & 255, 90];
});
writeFixture(join('service', 'upload-b.png'), 32, 32, (x, y) => {
  return [200, (x + y * 2) & 255, 40];
});

// Nested PNGs for recursive-glob tests (see orchestrator/test-media.mjs).
mkdirSync(join(__dirname, 'glob-in', 'sub'), { recursive: true });
writeFixture(join('glob-in', 'root.png'), 48, 48, (x, y) => {
  const v = ((x + y) * 3) & 255;
  return [v, 200, 100];
});
writeFixture(join('glob-in', 'sub', 'leaf.png'), 24, 24, [180, 60, 220]);

console.log('done.');
