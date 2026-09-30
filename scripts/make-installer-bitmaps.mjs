/**
 * scripts/make-installer-bitmaps.mjs
 *
 * Generates the two BMP assets NSIS MUI2 needs for branded installer pages:
 *
 *   dist/installer/welcome.bmp   164 × 314 px   left panel — Welcome & Finish pages
 *   dist/installer/header.bmp    150 ×  57 px   top-right strip — every inner page
 *
 * Sources (both optional — missing file → skip + warn):
 *   dist/branding/installer.png       wide brand artwork (cover-cropped to portrait)
 *   dist/branding/logo/logo_512.png   app icon composited onto a white header strip
 *
 * Usage:
 *   node scripts/make-installer-bitmaps.mjs
 *   node scripts/make-installer-bitmaps.mjs --force   (re-generate even if up-to-date)
 *
 * Called automatically by: npm run build:installer
 * Requires: npm install --save-dev sharp
 */

import sharp from 'sharp';
import { existsSync, mkdirSync, statSync, writeFileSync } from 'fs';
import { dirname } from 'path';

/**
 * Write an uncompressed 24-bit Windows BMP file.
 * Sharp can't output BMP, so we build the file from its raw RGB pixel buffer.
 *
 * @param {number} w  - image width in pixels
 * @param {number} h  - image height in pixels
 * @param {Buffer} rgb - raw pixel data, top-to-bottom, left-to-right, 3 bytes/pixel (R,G,B)
 * @param {string} outPath - destination file path
 */
function writeBmp(w, h, rgb, outPath) {
  const rowBytes  = w * 3;
  const rowPad    = (4 - (rowBytes % 4)) % 4;   // each row padded to 4-byte boundary
  const pixelSize = (rowBytes + rowPad) * h;
  const fileSize  = 54 + pixelSize;              // 14-byte file header + 40-byte info header

  const buf = Buffer.alloc(fileSize, 0);
  let o = 0;

  // File header
  buf.write('BM', o);                            o += 2;
  buf.writeUInt32LE(fileSize, o);                o += 4;
  buf.writeUInt32LE(0, o);                       o += 4;  // reserved
  buf.writeUInt32LE(54, o);                      o += 4;  // pixel data offset

  // BITMAPINFOHEADER
  buf.writeUInt32LE(40, o);                      o += 4;  // header size
  buf.writeInt32LE(w, o);                        o += 4;
  buf.writeInt32LE(h, o);                        o += 4;  // positive = bottom-up
  buf.writeUInt16LE(1, o);                       o += 2;  // planes
  buf.writeUInt16LE(24, o);                      o += 2;  // bits per pixel
  buf.writeUInt32LE(0, o);                       o += 4;  // compression (BI_RGB)
  buf.writeUInt32LE(pixelSize, o);               o += 4;
  buf.writeInt32LE(3937, o);                     o += 4;  // ~100 DPI X
  buf.writeInt32LE(3937, o);                     o += 4;  // ~100 DPI Y
  buf.writeUInt32LE(0, o);                       o += 4;  // colors used
  buf.writeUInt32LE(0, o);                       o += 4;  // colors important

  // Pixel data — BMP stores rows bottom-to-top, channels as BGR
  for (let row = h - 1; row >= 0; row--) {
    for (let col = 0; col < w; col++) {
      const src = (row * w + col) * 3;
      buf[o++] = rgb[src + 2]; // B
      buf[o++] = rgb[src + 1]; // G
      buf[o++] = rgb[src + 0]; // R
    }
    o += rowPad;
  }

  writeFileSync(outPath, buf);
}

/** Fail fast if sharp promoted the image to RGBA — writeBmp only handles RGB. */
function assertRGB(info, w, h) {
  if (info.channels !== 3 || info.width !== w || info.height !== h) {
    throw new Error(
      `Unexpected raw buffer: ${info.width}×${info.height} channels=${info.channels} ` +
      `(expected ${w}×${h} channels=3). Check .flatten() / .toColorspace() pipeline.`
    );
  }
}

// ── sizes required by NSIS MUI2 ────────────────────────────────────────────
const WELCOME_W = 164, WELCOME_H = 314;
const HEADER_W  = 150, HEADER_H  =  57;

// ── source / output paths ───────────────────────────────────────────────────
const WELCOME_SRC = 'dist/branding/installer.png';
const LOGO_SRC    = 'dist/branding/logo/logo_512.png';
const WELCOME_OUT = 'dist/installer/welcome.bmp';
const HEADER_OUT  = 'dist/installer/header.bmp';

const force = process.argv.includes('--force');

function ensureDir(file) {
  const dir = dirname(file);
  if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
}

/** Returns true when src is newer than dst (or dst is missing). */
function needsRebuild(src, dst) {
  if (!existsSync(dst)) return true;
  return statSync(src).mtimeMs > statSync(dst).mtimeMs;
}

// ── Welcome / Finish sidebar bitmap ────────────────────────────────────────
//
// Cover-crops the wide brand image to a portrait strip.
// The artwork has the focal point (glowing hand, "Pw" mark) toward the left,
// so we anchor the crop there.
//
if (!existsSync(WELCOME_SRC)) {
  console.warn(`⚠  ${WELCOME_SRC} not found — skipping ${WELCOME_OUT}`);
} else if (!force && !needsRebuild(WELCOME_SRC, WELCOME_OUT)) {
  console.log(`–  ${WELCOME_OUT} is up-to-date`);
} else {
  ensureDir(WELCOME_OUT);
  const wRaw = await sharp(WELCOME_SRC)
    .resize(WELCOME_W, WELCOME_H, {
      fit:      'cover',
      position: 'left',   // keep left edge — where the visual focal point is
    })
    .flatten({ background: '#000000' })
    .toColorspace('srgb')
    .raw()
    .toBuffer({ resolveWithObject: true });
  assertRGB(wRaw.info, WELCOME_W, WELCOME_H);
  writeBmp(WELCOME_W, WELCOME_H, wRaw.data, WELCOME_OUT);
  console.log(`✓  ${WELCOME_OUT}  (${WELCOME_W}×${WELCOME_H})`);
}

// ── Inner-page header bitmap ─────────────────────────────────────────────────
//
// White strip (150×57) with the app icon scaled to 48 px, right-aligned with
// a 4 px margin.  MUI_HEADERIMAGE_RIGHT places it in the top-right corner;
// the left portion is filled by NSIS with the page title / subtitle text.
//
if (!existsSync(LOGO_SRC)) {
  console.warn(`⚠  ${LOGO_SRC} not found — skipping ${HEADER_OUT}`);
} else if (!force && !needsRebuild(LOGO_SRC, HEADER_OUT)) {
  console.log(`–  ${HEADER_OUT} is up-to-date`);
} else {
  ensureDir(HEADER_OUT);

  const LOGO_PX = HEADER_H - 9;           // 48 px — comfortable fit in 57 px
  const MARGIN  = 4;

  const logoBuffer = await sharp(LOGO_SRC)
    .resize(LOGO_PX, LOGO_PX, { fit: 'fill' })
    .flatten({ background: '#ffffff' })   // strip alpha onto white
    .toBuffer();

  const hRaw = await sharp({
    create: {
      width:      HEADER_W,
      height:     HEADER_H,
      channels:   4,                         // use 4-ch base so composite keeps full colour
      background: { r: 255, g: 255, b: 255, alpha: 1 },
    },
  })
    .composite([{
      input: logoBuffer,
      left:  HEADER_W - LOGO_PX - MARGIN,
      top:   Math.floor((HEADER_H - LOGO_PX) / 2),
    }])
    .flatten({ background: '#ffffff' })    // alpha-blend onto white
    .removeAlpha()                         // drop alpha channel — BMP has no alpha
    .raw()
    .toBuffer({ resolveWithObject: true });

  assertRGB(hRaw.info, HEADER_W, HEADER_H);
  writeBmp(HEADER_W, HEADER_H, hRaw.data, HEADER_OUT);
  console.log(`✓  ${HEADER_OUT}  (${HEADER_W}×${HEADER_H})`);
}
