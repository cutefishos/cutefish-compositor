// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_HTTP_H_
#define CUTEFISH_RIPPLE_HTTP_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace cutefish::ripple {

// The bytes of one connection, over a socket or over TLS.
class Stream
{
public:
    virtual ~Stream() = default;
    // Reads what is there; 0 when the peer closed, -1 on error.
    virtual int read(char *data, size_t size) = 0;
    virtual bool write(const char *data, size_t size) = 0;
    // Reads exactly size bytes; false when the peer stopped short.
    bool readExact(char *data, size_t size);
    bool writeAll(const std::string &text) { return write(text.data(), text.size()); }
};

struct HttpRequest
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    // Lower-case names.
    std::map<std::string, std::string> headers;
    uint64_t contentLength = 0;
    // The body arrives in chunks, its length unknown up front: the official
    // LocalSend apps stream an upload this way.
    bool chunked = false;
    bool expectContinue = false;
    bool keepAlive = false;
    // The device that connected, as an IPv4 address.
    std::string peer;

    std::string header(const std::string &name) const;
    std::string queryValue(const std::string &name) const;
};

struct HttpResponse
{
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    // Optional extra headers, one line each, without the line break.
    std::string extraHeaders;

    static HttpResponse json(int status, const std::string &body);
    static HttpResponse text(int status, const std::string &body);
};

// Reads the request line and headers; the body, if any, is left on the stream.
bool readRequestHead(Stream &stream, HttpRequest &request, std::string &error);
// Reads a whole body, up to limit bytes; false when it is longer.
bool readBody(Stream &stream, const HttpRequest &request, size_t limit, std::string &body);
// Copies the body into a file, telling how much has arrived.
bool readBodyToFile(Stream &stream, const HttpRequest &request, const std::string &path,
                    const std::function<void(uint64_t)> &progress);
bool writeResponse(Stream &stream, const HttpResponse &response, bool keepAlive);

// Serves HTTP or HTTPS on one port, one connection at a time.
class Server
{
public:
    using Handler = std::function<HttpResponse(HttpRequest &, Stream &)>;

    Server();
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // The certificate is required for HTTPS; without it the port speaks HTTP.
    bool start(int port, const std::string &certificate, const std::string &key);
    void stop();
    bool isRunning() const { return m_fd >= 0; }
    bool isSecure() const { return m_context != nullptr; }

    void setHandler(Handler handler) { m_handler = std::move(handler); }

private:
    void run();
    void handle(int fd, std::string peer);

    int m_fd = -1;
    void *m_context = nullptr; // SSL_CTX*
    bool m_running = false;
    std::unique_ptr<std::thread> m_thread;
    Handler m_handler;
};

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_HTTP_H_
