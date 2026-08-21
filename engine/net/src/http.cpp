#include "luaug/net/http.h"

#include "luaug/core/i18n.h"
#include "luaug/net/tcp.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace luaug::net {
namespace {

using core::I18nArg;

constexpr std::string_view HeaderTerminator = "\r\n\r\n";

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool parseUnsigned(std::string_view text, core::u64& out, int base = 10)
{
    text = trimmed(text);
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] const HttpHeader* find(const std::vector<HttpHeader>& headers, std::string_view name)
{
    for (const HttpHeader& header : headers) {
        if (lowered(header.name) == name) {
            return &header;
        }
    }
    return nullptr;
}

// A chunked body, decoded. Servers choose this and a client cannot refuse it, so
// "we do not send chunked requests" and "we must read chunked responses" are
// both true and only one of them is a choice.
[[nodiscard]] std::optional<core::EngineError> decodeChunked(std::string_view encoded, core::usize maxBodyBytes,
                                                             std::string& out)
{
    core::usize cursor = 0;
    while (cursor < encoded.size()) {
        const core::usize lineEnd = encoded.find("\r\n", cursor);
        if (lineEnd == std::string_view::npos) {
            return core::makeError(LUAUG_TR("net.err.http_chunk_truncated"));
        }
        std::string_view sizeLine = encoded.substr(cursor, lineEnd - cursor);
        // A chunk extension follows a semicolon and is ignored, which is what
        // the RFC says to do with one you do not understand.
        if (const core::usize semicolon = sizeLine.find(';'); semicolon != std::string_view::npos) {
            sizeLine = sizeLine.substr(0, semicolon);
        }

        core::u64 size = 0;
        if (!parseUnsigned(sizeLine, size, 16)) {
            return core::makeError(LUAUG_TR("net.err.http_chunk_bad_size"));
        }
        cursor = lineEnd + 2;
        if (size == 0) {
            return std::nullopt;
        }
        if (out.size() + size > maxBodyBytes) {
            const I18nArg args[] = {{"limit", static_cast<core::i64>(maxBodyBytes)}};
            return core::makeError(LUAUG_TR("net.err.http_body_too_large"), args);
        }
        if (cursor + size > encoded.size()) {
            return core::makeError(LUAUG_TR("net.err.http_chunk_truncated"));
        }
        out.append(encoded.substr(cursor, static_cast<core::usize>(size)));
        cursor += static_cast<core::usize>(size);
        // The CRLF after the chunk data. Skipped rather than verified: a server
        // that omits it has already produced a body we read correctly.
        if (cursor + 2 <= encoded.size()) {
            cursor += 2;
        }
    }
    return core::makeError(LUAUG_TR("net.err.http_chunk_truncated"));
}

} // namespace

std::optional<core::EngineError> parseUrl(std::string_view url, ParsedUrl& out)
{
    out = ParsedUrl{};

    const core::usize schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        const I18nArg args[] = {{"url", std::string(url)}};
        return core::makeError(LUAUG_TR("net.err.http_url_malformed"), args);
    }
    out.scheme = lowered(url.substr(0, schemeEnd));
    if (out.scheme == "https") {
        // Refused rather than downgraded. See `http.h`.
        const I18nArg args[] = {{"url", std::string(url)}};
        return core::makeError(LUAUG_TR("net.err.http_tls_unsupported"), args);
    }
    if (out.scheme != "http") {
        const I18nArg args[] = {{"scheme", out.scheme}};
        return core::makeError(LUAUG_TR("net.err.http_scheme_unsupported"), args);
    }

    std::string_view rest = url.substr(schemeEnd + 3);
    const core::usize pathStart = rest.find('/');
    std::string_view authority = rest;
    if (pathStart != std::string_view::npos) {
        authority = rest.substr(0, pathStart);
        out.target = std::string(rest.substr(pathStart));
    }
    else {
        out.target = "/";
    }

    // No userinfo. `http://user:pass@host/` is a credential in a URL, and this
    // client refusing it is better than this client transmitting it.
    if (authority.find('@') != std::string_view::npos) {
        const I18nArg args[] = {{"url", std::string(url)}};
        return core::makeError(LUAUG_TR("net.err.http_userinfo_unsupported"), args);
    }

    const core::usize colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        core::u64 port = 0;
        if (!parseUnsigned(authority.substr(colon + 1), port) || port == 0 || port > 65535) {
            const I18nArg args[] = {{"url", std::string(url)}};
            return core::makeError(LUAUG_TR("net.err.http_url_malformed"), args);
        }
        out.port = static_cast<u16>(port);
        authority = authority.substr(0, colon);
    }
    out.host = std::string(authority);
    if (out.host.empty()) {
        const I18nArg args[] = {{"url", std::string(url)}};
        return core::makeError(LUAUG_TR("net.err.http_url_malformed"), args);
    }
    return std::nullopt;
}

std::optional<core::EngineError> parseHttpResponse(std::string_view raw, core::usize maxBodyBytes, HttpResponse& out)
{
    out = HttpResponse{};

    const core::usize headerEnd = raw.find(HeaderTerminator);
    if (headerEnd == std::string_view::npos) {
        return core::makeError(LUAUG_TR("net.err.http_headers_truncated"));
    }

    std::string_view head = raw.substr(0, headerEnd);
    const core::usize statusEnd = head.find("\r\n");
    const std::string_view statusLine = head.substr(0, statusEnd == std::string_view::npos ? head.size() : statusEnd);

    // `HTTP/1.1 200 OK` -- and `HTTP/1.1 200` too, because the reason phrase is
    // optional and a parser that requires it rejects real servers.
    const core::usize firstSpace = statusLine.find(' ');
    if (firstSpace == std::string_view::npos) {
        return core::makeError(LUAUG_TR("net.err.http_status_malformed"));
    }
    const core::usize secondSpace = statusLine.find(' ', firstSpace + 1);
    const std::string_view codeText = statusLine.substr(
        firstSpace + 1, (secondSpace == std::string_view::npos ? statusLine.size() : secondSpace) - firstSpace - 1);
    core::u64 code = 0;
    if (!parseUnsigned(codeText, code) || code > 999) {
        return core::makeError(LUAUG_TR("net.err.http_status_malformed"));
    }
    out.statusCode = static_cast<u16>(code);
    if (secondSpace != std::string_view::npos) {
        out.statusMessage = std::string(trimmed(statusLine.substr(secondSpace + 1)));
    }

    if (statusEnd != std::string_view::npos) {
        std::string_view fields = head.substr(statusEnd + 2);
        while (!fields.empty()) {
            const core::usize lineEnd = fields.find("\r\n");
            const std::string_view line = fields.substr(0, lineEnd == std::string_view::npos ? fields.size() : lineEnd);
            const core::usize colon = line.find(':');
            if (colon != std::string_view::npos) {
                out.headers.push_back(
                    {std::string(trimmed(line.substr(0, colon))), std::string(trimmed(line.substr(colon + 1)))});
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            fields = fields.substr(lineEnd + 2);
        }
    }

    const std::string_view rawBody = raw.substr(headerEnd + HeaderTerminator.size());
    const HttpHeader* const encoding = find(out.headers, "transfer-encoding");
    if (encoding != nullptr && lowered(encoding->value).find("chunked") != std::string::npos) {
        if (auto error = decodeChunked(rawBody, maxBodyBytes, out.body); error.has_value()) {
            return error;
        }
    }
    else {
        if (rawBody.size() > maxBodyBytes) {
            const I18nArg args[] = {{"limit", static_cast<core::i64>(maxBodyBytes)}};
            return core::makeError(LUAUG_TR("net.err.http_body_too_large"), args);
        }
        out.body = std::string(rawBody);
    }

    out.ok = true;
    return std::nullopt;
}

std::optional<core::EngineError> performHttp(const HttpRequest& request, HttpResponse& response)
{
    response = HttpResponse{};

    ParsedUrl url;
    if (auto error = parseUrl(request.url, url); error.has_value()) {
        return error;
    }

    TcpStream stream;
    if (auto error = stream.connect(url.host, url.port, request.timeoutMs); error.has_value()) {
        return error;
    }

    std::string wire;
    wire.reserve(256 + request.body.size());
    wire.append(request.method).append(" ").append(url.target).append(" HTTP/1.1\r\n");
    wire.append("Host: ").append(url.host);
    if (url.port != 80) {
        wire.append(":").append(std::to_string(url.port));
    }
    wire.append("\r\n");

    // `Connection: close` is what makes "read until the peer closes" a correct
    // way to find the end of a body, and it is why there is no keep-alive here:
    // pooling connections means tracking their idle state and their server's
    // willingness to keep them, which is a feature and not a detail.
    bool sawContentLength = false;
    bool sawConnection = false;
    for (const HttpHeader& header : request.headers) {
        const std::string name = lowered(header.name);
        sawContentLength = sawContentLength || name == "content-length";
        sawConnection = sawConnection || name == "connection";
        if (name == "host") {
            // Refused rather than allowed to win: two Host headers is a request
            // smuggling primitive, and the one above is derived from the URL
            // the caller actually asked for.
            continue;
        }
        // A header value carrying a newline would let a caller inject a second
        // request into this one. Refused, and named, because silently stripping
        // it hides that somebody tried.
        if (header.name.find_first_of("\r\n") != std::string::npos ||
            header.value.find_first_of("\r\n") != std::string::npos) {
            const I18nArg args[] = {{"header", header.name}};
            return core::makeError(LUAUG_TR("net.err.http_header_injection"), args);
        }
        wire.append(header.name).append(": ").append(header.value).append("\r\n");
    }
    if (!sawConnection) {
        wire.append("Connection: close\r\n");
    }
    if (!request.body.empty() && !sawContentLength) {
        wire.append("Content-Length: ").append(std::to_string(request.body.size())).append("\r\n");
    }
    wire.append("\r\n").append(request.body);

    const auto* const bytes = reinterpret_cast<const u8*>(wire.data());
    if (auto error = stream.send({bytes, wire.size()}); error.has_value()) {
        return error;
    }

    std::string raw;
    std::array<u8, 8192> chunk{};
    while (true) {
        core::usize received = 0;
        const auto error = stream.receive(chunk, received, request.timeoutMs);
        if (error.has_value()) {
            // An orderly close is how a `Connection: close` response ends, so it
            // is the SUCCESS path here and not a failure. Anything else is real.
            if (error->key == LUAUG_TR("net.err.closed")) {
                break;
            }
            return error;
        }
        if (received == 0) {
            return core::makeError(LUAUG_TR("net.err.http_response_timeout"));
        }
        if (raw.size() + received > request.maxBodyBytes) {
            const I18nArg args[] = {{"limit", static_cast<core::i64>(request.maxBodyBytes)}};
            return core::makeError(LUAUG_TR("net.err.http_body_too_large"), args);
        }
        raw.append(reinterpret_cast<const char*>(chunk.data()), received);
    }

    return parseHttpResponse(raw, request.maxBodyBytes, response);
}

} // namespace luaug::net
