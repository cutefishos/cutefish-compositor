// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/tls.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ripple/config.h"

namespace cutefish::ripple {

namespace {

std::string hexOf(const unsigned char *bytes, size_t size)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text(size * 2, '\0');
    for (size_t i = 0; i < size; ++i) {
        text[i * 2] = kDigits[bytes[i] >> 4];
        text[i * 2 + 1] = kDigits[bytes[i] & 0x0F];
    }
    return text;
}

std::string pemOf(X509 *certificate)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio)
        return {};
    PEM_write_bio_X509(bio, certificate);
    char *data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    std::string text(data ? std::string(data, size_t(size)) : std::string());
    BIO_free(bio);
    return text;
}

std::string pemOf(EVP_PKEY *key)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio)
        return {};
    PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    char *data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    std::string text(data ? std::string(data, size_t(size)) : std::string());
    BIO_free(bio);
    return text;
}

std::string fingerprintOf(X509 *certificate)
{
    unsigned char *der = nullptr;
    const int size = i2d_X509(certificate, &der);
    if (size <= 0)
        return {};
    const std::string text = sha256Hex(der, size_t(size));
    OPENSSL_free(der);
    return text;
}

std::string makeCertificate(std::string &keyPem)
{
    EVP_PKEY *key = nullptr;
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!context)
        return {};
    std::string certificatePem;
    if (EVP_PKEY_keygen_init(context) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) > 0
        && EVP_PKEY_keygen(context, &key) > 0)
        ;
    EVP_PKEY_CTX_free(context);
    if (!key)
        return {};

    X509 *certificate = X509_new();
    if (!certificate) {
        EVP_PKEY_free(key);
        return {};
    }

    // A random serial, a year of slack either way so clocks a little apart
    // still line up, and ten years of life.
    std::array<unsigned char, 16> serial {};
    RAND_bytes(serial.data(), int(serial.size()));
    serial[0] &= 0x7F;
    ASN1_INTEGER *serialNumber = X509_get_serialNumber(certificate);
    BIGNUM *number = BN_bin2bn(serial.data(), int(serial.size()), nullptr);
    if (number) {
        BN_to_ASN1_INTEGER(number, serialNumber);
        BN_free(number);
    }

    X509_set_version(certificate, 2);
    X509_gmtime_adj(X509_get_notBefore(certificate), -365L * 24 * 60 * 60);
    X509_gmtime_adj(X509_get_notAfter(certificate), 10L * 365 * 24 * 60 * 60);
    X509_set_pubkey(certificate, key);

    X509_NAME *name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("Cutefish Ripple"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("Ripple"), -1, -1, 0);
    X509_set_issuer_name(certificate, name);

    if (X509_sign(certificate, key, EVP_sha256()) > 0) {
        certificatePem = pemOf(certificate);
        keyPem = pemOf(key);
    }

    X509_free(certificate);
    EVP_PKEY_free(key);
    return certificatePem;
}

bool writePrivate(const std::filesystem::path &path, const std::string &text)
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    size_t done = 0;
    while (done < text.size()) {
        const ssize_t written = ::write(fd, text.data() + done, text.size() - done);
        if (written <= 0) {
            ::close(fd);
            return false;
        }
        done += size_t(written);
    }
    ::close(fd);
    return true;
}

std::string readAll(const std::filesystem::path &path)
{
    std::ifstream file(path);
    if (!file)
        return {};
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

std::string sha256Hex(const void *data, size_t size)
{
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
    SHA256(static_cast<const unsigned char *>(data), size, digest.data());
    return hexOf(digest.data(), digest.size());
}

std::string sha256File(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!context)
        return {};
    std::string result;
    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1) {
        std::vector<char> buffer(1 << 16);
        bool ok = true;
        while (file) {
            file.read(buffer.data(), std::streamsize(buffer.size()));
            const std::streamsize got = file.gcount();
            if (got <= 0)
                break;
            if (EVP_DigestUpdate(context, buffer.data(), size_t(got)) != 1) {
                ok = false;
                break;
            }
        }
        unsigned char digest[SHA256_DIGEST_LENGTH] = {};
        unsigned int length = 0;
        if (ok && EVP_DigestFinal_ex(context, digest, &length) == 1)
            result = hexOf(digest, length);
    }
    EVP_MD_CTX_free(context);
    return result;
}

Certificate loadOrCreateCertificate()
{
    const std::filesystem::path directory(dataPath());
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    const std::filesystem::path certPath = directory / "cert.pem";
    const std::filesystem::path keyPath = directory / "key.pem";

    Certificate certificate;
    certificate.certificate = readAll(certPath);
    certificate.key = readAll(keyPath);

    if (certificate.certificate.empty() || certificate.key.empty()) {
        certificate.certificate = makeCertificate(certificate.key);
        if (certificate.certificate.empty())
            return {};
        writePrivate(certPath, certificate.certificate);
        writePrivate(keyPath, certificate.key);
    }

    // Read it back rather than trusting the PEM text for the identity.
    BIO *bio = BIO_new_mem_buf(certificate.certificate.data(), int(certificate.certificate.size()));
    X509 *x509 = bio ? PEM_read_bio_X509(bio, nullptr, nullptr, nullptr) : nullptr;
    if (bio)
        BIO_free(bio);
    if (!x509)
        return {};
    certificate.fingerprint = fingerprintOf(x509);
    X509_free(x509);
    return certificate;
}

} // namespace cutefish::ripple
