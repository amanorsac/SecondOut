// ============================================================================
//  LicenseKeys.h -- this app's trust anchor for Amanorsac licensing.
//
//  This is the PUBLIC half of the shared Amanorsac signing key: an
//  uncompressed NIST P-256 point (0x04 || X || Y). The SAME key signs
//  PerformLive pack manifests/receipts AND every product's license proofs --
//  one trust anchor, company-wide.
//
//  These bytes are the key the LIVE server at amanorsac.studio signs with,
//  taken from the studio's integration notes and checked against a real proof
//  issued for a real license before being committed. The local dev
//  store-backend has its own separate keypair, so storectl.js must NOT
//  overwrite this file unless the live private key is what it holds --
//  a mismatch here silently rejects every genuine activation, which is
//  exactly the bug that shipped in 1.3.0.
//
//  It is compiled in deliberately. If this key lived in a file beside the
//  executable, anyone who could write to that directory could substitute
//  their own key and sign whatever they liked -- precisely what signing
//  exists to prevent.
//
//  ROTATION: keep the old key in a *Previous constant while migrating, so
//  copies of the app already in the wild keep accepting content signed with it.
// ============================================================================
#pragma once
#include <cstdint>

namespace amanorsacstudio
{

// P-256 public key, uncompressed point. 65 bytes.
inline constexpr uint8_t kLicenseSigningKey[] = {
    0x04, 0xcd, 0xa5, 0x7d, 0x1c, 0xc8, 0xa6, 0xe2, 0x71, 0xd5, 0x48, 0x49,
    0xce, 0x55, 0xd5, 0x03, 0x77, 0x56, 0x66, 0x90, 0xfd, 0xb6, 0x95, 0x45,
    0xa4, 0x1a, 0x92, 0xc4, 0x77, 0xda, 0xcb, 0x00, 0x0d, 0x2c, 0x06, 0x0b,
    0xa8, 0x3f, 0xbd, 0x9b, 0x70, 0x85, 0xaf, 0xff, 0xc0, 0x42, 0xd4, 0x00,
    0x7e, 0x5b, 0x96, 0xfe, 0x68, 0xff, 0xec, 0x91, 0x11, 0xf6, 0x21, 0x00,
    0x79, 0xfc, 0x43, 0x59, 0x52,
};

// Set to true once a real key has been generated. While this is false the app
// refuses to trust any signed content at all, rather than accepting it
// unverified -- fail closed.
inline constexpr bool kLicenseSigningKeyConfigured = true;

} // namespace amanorsacstudio
