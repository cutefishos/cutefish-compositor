// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/client.h"

#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "ripple/tls.h"

namespace cutefish::ripple {

namespace {

size_t writeToString(char *data, size_t size, size_t count, void *user)
{
    static_cast<std::string *>(user)->append(data, size * count);
    return size * count;
}

int transferProgress(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t uploaded)
{
    const auto *callback = static_cast<const std::function<void(uint64_t)> *>(user);
    if (callback && *callback)
        (*callback)(uint64_t(uploaded));
    return 0;
}

// libcurl hands the chain over after the handshake; the device's fingerprint
// is the certificate's, so a peer that announces one and presents another is
// not the device that was chosen.
std::string presentedFingerprint(CURL *handle)
{
    struct curl_certinfo *info = nullptr;
    if (curl_easy_getinfo(handle, CURLINFO_CERTINFO, &info) != CURLE_OK || !info || info->num_of_certs <= 0)
        return {};
    std::string pem;
    for (curl_slist *entry = info->certinfo[0]; entry; entry = entry->next) {
        const std::string line(entry->data);
        if (line.rfind("Cert:", 0) == 0) {
            pem = line.substr(line.find(':') + 1);
            break;
        }
    }
    if (pem.empty())
        return {};
    BIO *bio = BIO_new_mem_buf(pem.data(), int(pem.size()));
    X509 *certificate = bio ? PEM_read_bio_X509(bio, nullptr, nullptr, nullptr) : nullptr;
    if (bio)
        BIO_free(bio);
    if (!certificate)
        return {};
    unsigned char *der = nullptr;
    const int size = i2d_X509(certificate, &der);
    std::string fingerprint;
    if (size > 0)
        fingerprint = sha256Hex(der, size_t(size));
    if (der)
        OPENSSL_free(der);
    X509_free(certificate);
    return fingerprint;
}

std::string urlFor(const Device &peer, const std::string &path)
{
    return peer.protocol + "://" + peer.host + ":" + std::to_string(peer.port) + path;
}

} // namespace

std::string deviceJson(const Device &device)
{
    return device.toJson().serialize();
}

Client::Client()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void Client::setCertificate(std::string certificate, std::string key)
{
    m_certificate = std::move(certificate);
    m_key = std::move(key);
}

Client::~Client()
{
    curl_global_cleanup();
}

int Client::postJson(const Device &peer, const std::string &path, const std::string &body,
                     std::string &reply, std::string &error)
{
    CURL *handle = curl_easy_init();
    if (!handle) {
        error = "cannot make a request";
        return 0;
    }
    reply.clear();
    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(handle, CURLOPT_URL, urlFor(peer, path).c_str());
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body.size()));
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_PROXY, "");
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_CERTINFO, 1L);
    if (peer.protocol == "https") {
        // Self-signed by design; the fingerprint below is the check.
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
        // LocalSend receivers require a client certificate; ours is the
        // device's identity, the same one announced on the network.
        curl_blob certificate {m_certificate.data(), m_certificate.size(), CURL_BLOB_NOCOPY};
        curl_blob key {m_key.data(), m_key.size(), CURL_BLOB_NOCOPY};
        if (!m_certificate.empty()) {
            curl_easy_setopt(handle, CURLOPT_SSLCERT_BLOB, &certificate);
            curl_easy_setopt(handle, CURLOPT_SSLCERTTYPE, "PEM");
        }
        if (!m_key.empty()) {
            curl_easy_setopt(handle, CURLOPT_SSLKEY_BLOB, &key);
            curl_easy_setopt(handle, CURLOPT_SSLKEYTYPE, "PEM");
        }
    }

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    const std::string presented = presentedFingerprint(handle);

    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        status = 0;
    } else if (peer.protocol == "https" && !presented.empty() && !peer.fingerprint.empty()
               && presented != peer.fingerprint) {
        error = "the device presented another certificate";
        status = 0;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    return int(status);
}

int Client::postFile(const Device &peer, const std::string &path, const std::string &file,
                     const std::function<void(uint64_t)> &progress, std::string &error)
{
    struct stat info {};
    if (::stat(file.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        error = "cannot read " + file;
        return 0;
    }
    FILE *input = std::fopen(file.c_str(), "rb");
    if (!input) {
        error = "cannot open " + file;
        return 0;
    }
    CURL *handle = curl_easy_init();
    if (!handle) {
        std::fclose(input);
        error = "cannot make a request";
        return 0;
    }
    std::string reply;
    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/octet-stream");
    curl_easy_setopt(handle, CURLOPT_URL, urlFor(peer, path).c_str());
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_READDATA, input);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(info.st_size));
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 3600L);
    curl_easy_setopt(handle, CURLOPT_PROXY, "");
    curl_easy_setopt(handle, CURLOPT_CERTINFO, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, transferProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progress);
    if (peer.protocol == "https") {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_blob certificate {m_certificate.data(), m_certificate.size(), CURL_BLOB_NOCOPY};
        curl_blob key {m_key.data(), m_key.size(), CURL_BLOB_NOCOPY};
        if (!m_certificate.empty()) {
            curl_easy_setopt(handle, CURLOPT_SSLCERT_BLOB, &certificate);
            curl_easy_setopt(handle, CURLOPT_SSLCERTTYPE, "PEM");
        }
        if (!m_key.empty()) {
            curl_easy_setopt(handle, CURLOPT_SSLKEY_BLOB, &key);
            curl_easy_setopt(handle, CURLOPT_SSLKEYTYPE, "PEM");
        }
    }

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    const std::string presented = presentedFingerprint(handle);
    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        status = 0;
    } else if (peer.protocol == "https" && !presented.empty() && !peer.fingerprint.empty()
               && presented != peer.fingerprint) {
        error = "the device presented another certificate";
        status = 0;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    std::fclose(input);
    return int(status);
}

} // namespace cutefish::ripple
