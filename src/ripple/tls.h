// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_TLS_H_
#define CUTEFISH_RIPPLE_TLS_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace cutefish::ripple {

// The identity of this device: one self-signed certificate, made at the first
// start and kept, so a peer recognizes the same computer between sessions.
struct Certificate
{
    std::string certificate;
    std::string key;
    // SHA-256 of the certificate in DER, uppercase hex, as LocalSend defines it.
    std::string fingerprint;
};

// Reads ~/.config/cutefish/ripple.d/{cert,key}.pem, or makes and stores them.
Certificate loadOrCreateCertificate();

// Uppercase hex SHA-256 of the bytes.
std::string sha256Hex(const void *data, size_t size);
// The same, of a file's bytes; empty when the file cannot be read.
std::string sha256File(const std::string &path);

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_TLS_H_
