/**
 * Binary license.dat v1 — same JSON envelope as license.json, wrapped in random noise.
 * Layout must match src/win/license_file.cpp (PMK1 header, LE u32).
 */
import crypto from "node:crypto";

export const LICENSE_DAT_MAGIC = Buffer.from("PMK1", "ascii");
export const LICENSE_DAT_HEADER_SIZE = 32;

/**
 * @param {object} fileJsonObj — { v, payload_hex, signature_hex } (same as /v1/license/issue JSON)
 * @param {object} [options]
 * @param {number} [options.minNoiseBytes] — minimum total random prefix+suffix (default 8192)
 * @returns {Buffer}
 */
export function buildLicenseDatBlob(fileJsonObj, options = {}) {
  const minNoise = options.minNoiseBytes ?? 8192;
  const jsonUtf8 = Buffer.from(JSON.stringify(fileJsonObj), "utf8");
  const minEach = Math.max(1024, Math.floor(minNoise / 2));
  const prefixLen = minEach + crypto.randomInt(0, 49152);
  const suffixLen = minEach + crypto.randomInt(0, 49152);
  const jsonOffset = LICENSE_DAT_HEADER_SIZE + prefixLen;
  const totalSize = jsonOffset + jsonUtf8.length + suffixLen;
  const buf = Buffer.alloc(totalSize);
  LICENSE_DAT_MAGIC.copy(buf, 0);
  buf.writeUInt32LE(1, 4);
  buf.writeUInt32LE(jsonOffset, 8);
  buf.writeUInt32LE(jsonUtf8.length, 12);
  buf.fill(0, 16, LICENSE_DAT_HEADER_SIZE);
  crypto.randomFillSync(buf.subarray(LICENSE_DAT_HEADER_SIZE, jsonOffset));
  jsonUtf8.copy(buf, jsonOffset);
  crypto.randomFillSync(buf.subarray(jsonOffset + jsonUtf8.length));
  return buf;
}
