#!/usr/bin/env node
/**
 * Generate Ed25519 keypair (libsodium, matches pm-image C++ verification).
 */
const sodium = require("libsodium-wrappers");

(async () => {
  await sodium.ready;
  const kp = sodium.crypto_sign_keypair();
  const pub = Buffer.from(kp.publicKey);
  const sk = Buffer.from(kp.privateKey);
  console.log("LICENSE_ISSUER_SECRET_HEX=" + sk.toString("hex"));
  console.log("");
  console.log("// Paste into src/win/license_pub_key.hpp");
  const lines = [];
  for (let i = 0; i < 32; i += 8) {
    const chunk = Array.from(pub.slice(i, i + 8))
      .map((b) => "0x" + b.toString(16).padStart(2, "0"))
      .join(", ");
    lines.push("    " + chunk + (i + 8 < 32 ? "," : ""));
  }
  console.log(
    "constexpr unsigned char kLicenseIssuerPublicKeyEd25519[32] = {\n" +
      lines.join("\n") +
      "\n};"
  );
  console.log("");
  console.log("PUBLIC_HEX=" + pub.toString("hex"));
})();
