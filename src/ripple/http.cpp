// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>

namespace cutefish::ripple {

namespace {

std::string lower(std::string text)
{
    for (char &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trimmed(const std::string &text)
{
    const size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return {};
    const size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

std::string urlDecode(const std::string &text)
{
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            result.push_back(char(std::stoi(text.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else if (text[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

const char *statusText(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    default: return "Unknown";
    }
}

class SocketStream final : public Stream
{
public:
    explicit SocketStream(int fd)
        : m_fd(fd)
    {
    }

    int read(char *data, size_t size) override
    {
        for (;;) {
            const ssize_t got = ::recv(m_fd, data, size, 0);
            if (got >= 0)
                return int(got);
            if (errno == EINTR)
                continue;
            return -1;
        }
    }

    bool write(const char *data, size_t size) override
    {
        size_t done = 0;
        while (done < size) {
            const ssize_t sent = ::send(m_fd, data + done, size - done, MSG_NOSIGNAL);
            if (sent <= 0) {
                if (sent < 0 && errno == EINTR)
                    continue;
                return false;
            }
            done += size_t(sent);
        }
        return true;
    }

private:
    int m_fd;
};

class TlsStream final : public Stream
{
public:
    TlsStream(SSL *ssl, int fd)
        : m_ssl(ssl)
        , m_fd(fd)
    {
    }
    ~TlsStream() override
    {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        ::close(m_fd);
    }

    int read(char *data, size_t size) override
    {
        for (;;) {
            const int got = SSL_read(m_ssl, data, int(size));
            if (got > 0)
                return got;
            const int error = SSL_get_error(m_ssl, got);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
                continue;
            if (error == SSL_ERROR_ZERO_RETURN)
                return 0;
            return -1;
        }
    }

    bool write(const char *data, size_t size) override
    {
        size_t done = 0;
        while (done < size) {
            const int sent = SSL_write(m_ssl, data + done, int(size - done));
            if (sent <= 0) {
                const int error = SSL_get_error(m_ssl, sent);
                if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ)
                    continue;
                return false;
            }
            done += size_t(sent);
        }
        return true;
    }

private:
    SSL *m_ssl;
    int m_fd;
};

// A stream whose first bytes were already read with the headers.
class PrefixedStream final : public Stream
{
public:
    PrefixedStream(Stream &inner, std::string prefix)
        : m_inner(inner)
        , m_prefix(std::move(prefix))
    {
    }

    int read(char *data, size_t size) override
    {
        if (m_offset < m_prefix.size()) {
            const size_t count = std::min(size, m_prefix.size() - m_offset);
            std::memcpy(data, m_prefix.data() + m_offset, count);
            m_offset += count;
            return int(count);
        }
        return m_inner.read(data, size);
    }

    bool write(const char *data, size_t size) override { return m_inner.write(data, size); }

private:
    Stream &m_inner;
    std::string m_prefix;
    size_t m_offset = 0;
};

} // namespace

std::string HttpRequest::header(const std::string &name) const
{
    const auto it = headers.find(lower(name));
    return it == headers.end() ? std::string() : it->second;
}

std::string HttpRequest::queryValue(const std::string &name) const
{
    const auto it = query.find(name);
    return it == query.end() ? std::string() : it->second;
}

HttpResponse HttpResponse::json(int status, const std::string &body)
{
    HttpResponse response;
    response.status = status;
    response.contentType = "application/json";
    response.body = body;
    return response;
}

HttpResponse HttpResponse::text(int status, const std::string &body)
{
    HttpResponse response;
    response.status = status;
    response.contentType = "text/plain";
    response.body = body;
    return response;
}

bool Stream::readExact(char *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        const int got = read(data + done, size - done);
        if (got <= 0)
            return false;
        done += size_t(got);
    }
    return true;
}

bool readRequestHead(Stream &stream, HttpRequest &request, std::string &pending, std::string &error)
{
    std::string buffer;
    char chunk[4096];
    size_t end = std::string::npos;
    // Headers stop at an empty line; a request that sends more than this is
    // not one of ours.
    while (buffer.size() < 64 * 1024) {
        end = buffer.find("\r\n\r\n");
        if (end != std::string::npos)
            break;
        const int got = stream.read(chunk, sizeof(chunk));
        if (got <= 0) {
            error = "the connection closed before the request";
            return false;
        }
        buffer.append(chunk, size_t(got));
    }
    if (end == std::string::npos) {
        error = "the request headers are too long";
        return false;
    }

    pending = buffer.substr(end + 4);
    const std::string head = buffer.substr(0, end);

    size_t lineStart = 0;
    bool firstLine = true;
    while (lineStart <= head.size()) {
        const size_t lineEnd = head.find("\r\n", lineStart);
        const std::string line = head.substr(
                lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
        lineStart = lineEnd == std::string::npos ? head.size() + 1 : lineEnd + 2;

        if (firstLine) {
            firstLine = false;
            const size_t first = line.find(' ');
            const size_t second = first == std::string::npos ? std::string::npos : line.find(' ', first + 1);
            if (first == std::string::npos || second == std::string::npos) {
                error = "the request line is malformed";
                return false;
            }
            request.method = line.substr(0, first);
            const std::string target = line.substr(first + 1, second - first - 1);
            const size_t question = target.find('?');
            request.path = question == std::string::npos ? target : target.substr(0, question);
            if (question != std::string::npos) {
                const std::string query = target.substr(question + 1);
                size_t start = 0;
                while (start <= query.size()) {
                    const size_t amp = query.find('&', start);
                    const std::string pair = query.substr(
                            start, amp == std::string::npos ? std::string::npos : amp - start);
                    const size_t equal = pair.find('=');
                    if (!pair.empty()) {
                        request.query[urlDecode(pair.substr(0, equal))] =
                                equal == std::string::npos ? std::string() : urlDecode(pair.substr(equal + 1));
                    }
                    if (amp == std::string::npos)
                        break;
                    start = amp + 1;
                }
            }
            continue;
        }

        if (line.empty())
            continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        request.headers[lower(trimmed(line.substr(0, colon)))] = trimmed(line.substr(colon + 1));
    }

    const std::string length = request.header("content-length");
    request.contentLength = length.empty() ? 0 : uint64_t(std::strtoull(length.c_str(), nullptr, 10));
    request.chunked = lower(request.header("transfer-encoding")).find("chunked") != std::string::npos;
    request.expectContinue = lower(request.header("expect")).find("100-continue") != std::string::npos;
    const std::string connection = lower(request.header("connection"));
    request.keepAlive = connection != "close";
    if (request.path.empty()) {
        error = "the request has no path";
        return false;
    }
    return true;
}

namespace {

// One header line of a chunked body, without the line break.
bool readChunkLine(Stream &stream, std::string &line)
{
    line.clear();
    for (;;) {
        char c = 0;
        if (stream.read(&c, 1) != 1)
            return false;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        if (line.size() > 4096)
            return false;
        line.push_back(c);
    }
}

// The next chunk's size; 0 ends the body, and its trailers are read past.
bool nextChunkSize(Stream &stream, size_t &size)
{
    std::string line;
    if (!readChunkLine(stream, line))
        return false;
    size = size_t(std::strtoull(line.c_str(), nullptr, 16));
    return true;
}

bool skipChunkEnd(Stream &stream)
{
    char crlf[2] = {};
    return stream.readExact(crlf, 2) && crlf[0] == '\r' && crlf[1] == '\n';
}

} // namespace

bool readBody(Stream &stream, const HttpRequest &request, size_t limit, std::string &body)
{
    body.clear();
    if (!request.chunked) {
        if (request.contentLength > limit)
            return false;
        body.resize(size_t(request.contentLength));
        return request.contentLength == 0 || stream.readExact(body.data(), body.size());
    }
    for (;;) {
        size_t size = 0;
        if (!nextChunkSize(stream, size))
            return false;
        if (size == 0) {
            // Trailers, then the empty line that ends them.
            std::string line;
            while (readChunkLine(stream, line) && !line.empty()) {
            }
            return true;
        }
        if (body.size() + size > limit)
            return false;
        const size_t offset = body.size();
        body.resize(offset + size);
        if (!stream.readExact(body.data() + offset, size) || !skipChunkEnd(stream))
            return false;
    }
}

bool readBodyToFile(Stream &stream, const HttpRequest &request, const std::string &path,
                    const std::function<void(uint64_t)> &progress)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    uint64_t done = 0;
    bool ok = true;
    char buffer[1 << 16];

    auto copy = [&](size_t amount) {
        while (amount > 0) {
            const size_t want = std::min(amount, sizeof(buffer));
            if (!stream.readExact(buffer, want) || std::fwrite(buffer, 1, want, file) != want) {
                ok = false;
                return false;
            }
            amount -= want;
            done += want;
            if (progress)
                progress(done);
        }
        return true;
    };

    if (!request.chunked) {
        if (!copy(size_t(request.contentLength)))
            ok = false;
    } else {
        for (;;) {
            size_t size = 0;
            if (!nextChunkSize(stream, size)) {
                ok = false;
                break;
            }
            if (size == 0) {
                std::string line;
                while (readChunkLine(stream, line) && !line.empty()) {
                }
                break;
            }
            if (!copy(size) || !skipChunkEnd(stream)) {
                ok = false;
                break;
            }
        }
    }

    std::fclose(file);
    return ok;
}

bool writeResponse(Stream &stream, const HttpResponse &response, bool keepAlive)
{
    std::string head = "HTTP/1.1 " + std::to_string(response.status) + " " + statusText(response.status)
            + "\r\n";
    head += "Content-Type: " + response.contentType + "\r\n";
    head += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    head += "Connection: ";
    head += keepAlive ? "keep-alive\r\n" : "close\r\n";
    head += response.extraHeaders;
    head += "\r\n";
    return stream.writeAll(head) && stream.writeAll(response.body);
}

Server::Server() = default;

Server::~Server()
{
    stop();
}

bool Server::start(int port, const std::string &certificate, const std::string &key)
{
    if (m_running)
        return true;
    if (!certificate.empty() && !key.empty()) {
        SSL_CTX *context = SSL_CTX_new(TLS_server_method());
        if (!context)
            return false;
        SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
        bool ok = false;
        BIO *certBio = BIO_new_mem_buf(certificate.data(), int(certificate.size()));
        X509 *x509 = certBio ? PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr) : nullptr;
        BIO *keyBio = BIO_new_mem_buf(key.data(), int(key.size()));
        EVP_PKEY *privateKey = keyBio ? PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr) : nullptr;
        if (x509 && privateKey && SSL_CTX_use_certificate(context, x509) == 1
            && SSL_CTX_use_PrivateKey(context, privateKey) == 1 && SSL_CTX_check_private_key(context) == 1)
            ok = true;
        if (x509)
            X509_free(x509);
        if (privateKey)
            EVP_PKEY_free(privateKey);
        if (certBio)
            BIO_free(certBio);
        if (keyBio)
            BIO_free(keyBio);
        if (!ok) {
            SSL_CTX_free(context);
            return false;
        }
        m_context = context;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        stop();
        return false;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(uint16_t(port));
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
        || ::listen(fd, 16) != 0) {
        ::close(fd);
        stop();
        return false;
    }

    m_fd = fd;
    m_running = true;
    m_thread = std::make_unique<std::thread>([this] { run(); });
    return true;
}

void Server::stop()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_fd >= 0) {
        ::shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_thread && m_thread->joinable() && m_thread->get_id() != std::this_thread::get_id())
        m_thread->join();
    m_thread.reset();
    if (m_context) {
        SSL_CTX_free(static_cast<SSL_CTX *>(m_context));
        m_context = nullptr;
    }
}

void Server::run()
{
    while (m_running) {
        sockaddr_in peer {};
        socklen_t length = sizeof(peer);
        const int fd = ::accept4(m_fd, reinterpret_cast<sockaddr *>(&peer), &length, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        timeval timeout {30, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        char address[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &peer.sin_addr, address, sizeof(address));
        handle(fd, address);
        if (!m_running)
            break;
    }
}

void Server::handle(int fd, std::string peer)
{
    if (!m_context) {
        SocketStream stream(fd);
        HttpRequest request;
        request.peer = std::move(peer);
        std::string pending, error;
        if (readRequestHead(stream, request, pending, error)) {
            PrefixedStream body(stream, std::move(pending));
            HttpResponse response = m_handler ? m_handler(request, body) : HttpResponse::json(404, "{}");
            writeResponse(stream, response, false);
        }
        ::close(fd);
        return;
    }

    SSL *ssl = SSL_new(static_cast<SSL_CTX *>(m_context));
    if (!ssl) {
        ::close(fd);
        return;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        ::close(fd);
        return;
    }
    TlsStream stream(ssl, fd);
    HttpRequest request;
    request.peer = std::move(peer);
    std::string pending, error;
    if (readRequestHead(stream, request, pending, error)) {
        PrefixedStream body(stream, std::move(pending));
        if (request.expectContinue)
            stream.writeAll("HTTP/1.1 100 Continue\r\n\r\n");
        HttpResponse response = m_handler ? m_handler(request, body) : HttpResponse::json(404, "{}");
        writeResponse(stream, response, false);
    }
}

} // namespace cutefish::ripple
