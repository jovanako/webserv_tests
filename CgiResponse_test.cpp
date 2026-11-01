#include "tiny_test.hpp"
#include "CgiResponse.hpp"
#include "HttpResponse.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstring>

namespace {

std::vector<char> toVector(const std::string& str) {
    return std::vector<char>(str.begin(), str.end());
}

std::string toString(const std::vector<char>& vec) {
    if (vec.empty()) {
        return std::string();
    }
    return std::string(vec.begin(), vec.end());
}

} // namespace

// =============================================================================
// 1. Initial State & Orthodox Canonical Form
// =============================================================================

TEST(CgiResponse_TestInitialState) {
    CgiResponse cgiResp;

    ASSERT_EQ(cgiResp.getStatusCode(), 200);
    ASSERT_TRUE(cgiResp.getHeaders().empty());
    ASSERT_TRUE(cgiResp.getBody().empty());
}

TEST(CgiResponse_TestCopyConstructorAndAssignment) {
    CgiResponse original;
    std::string raw =
        "Status: 201 Created\r\n"
        "Content-Type: application/json\r\n"
        "X-Request-Id: 42\r\n"
        "\r\n"
        "{\"created\":true}";
    ASSERT_TRUE(original.parse(toVector(raw)));

    // Copy constructor
    CgiResponse copy(original);
    ASSERT_EQ(copy.getStatusCode(), 201);
    ASSERT_EQ(copy.getHeaders().size(), original.getHeaders().size());
    ASSERT_EQ(toString(copy.getBody()), "{\"created\":true}");

    // Assignment operator & self-assignment
    CgiResponse assigned;
    assigned = original;
    assigned = assigned;
    ASSERT_EQ(assigned.getStatusCode(), 201);
    ASSERT_EQ(assigned.getHeaders().size(), original.getHeaders().size());
    ASSERT_EQ(toString(assigned.getBody()), "{\"created\":true}");
}

// =============================================================================
// 2. Document Response Parsing & Header/Body Delimiters (RFC 3875 Section 6.2)
// =============================================================================

TEST(CgiResponse_TestParseStandardCRLFDelimiter) {
    CgiResponse cgiResp;
    std::string raw =
        "Content-Type: text/html; charset=utf-8\r\n"
        "X-Powered-By: python3\r\n"
        "\r\n"
        "<html><body><h1>Hello CGI</h1></body></html>";

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));
    ASSERT_EQ(cgiResp.getStatusCode(), 200);
    ASSERT_EQ(toString(cgiResp.getBody()), "<html><body><h1>Hello CGI</h1></body></html>");

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 200);
    ASSERT_EQ(httpResp.getHeader("Content-Type"), "text/html; charset=utf-8");
    ASSERT_EQ(httpResp.getHeader("X-Powered-By"), "python3");
    ASSERT_EQ(toString(httpResp.getBody()), "<html><body><h1>Hello CGI</h1></body></html>");
}

TEST(CgiResponse_TestParseLFDelimiter) {
    // POSIX CGI scripts (e.g., Python print() or shell echo) commonly emit \n\n
    CgiResponse cgiResp;
    std::string raw =
        "Content-Type: text/plain\n"
        "Cache-Control: no-cache\n"
        "\n"
        "Line 1\nLine 2\n";

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));
    ASSERT_EQ(cgiResp.getStatusCode(), 200);
    ASSERT_EQ(toString(cgiResp.getBody()), "Line 1\nLine 2\n");

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 200);
    ASSERT_EQ(httpResp.getHeader("Content-Type"), "text/plain");
    ASSERT_EQ(httpResp.getHeader("Cache-Control"), "no-cache");
    ASSERT_EQ(toString(httpResp.getBody()), "Line 1\nLine 2\n");
}

TEST(CgiResponse_TestParseEmptyBodyAfterHeaders) {
    CgiResponse cgiResp;
    std::string raw =
        "Status: 204 No Content\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n";

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));
    ASSERT_EQ(cgiResp.getStatusCode(), 204);
    ASSERT_TRUE(cgiResp.getBody().empty());

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 204);
    ASSERT_TRUE(httpResp.getBody().empty());
}

TEST(CgiResponse_TestParseBodyWithEmbeddedBlankLinesAndHeaderLikeText) {
    // Blank lines and "Key: Value" text inside the body must not be parsed as headers
    CgiResponse cgiResp;
    std::string bodyContent =
        "First paragraph\r\n\r\n"
        "Status: 500 Internal Server Error\r\n"
        "Second paragraph\n\n"
        "Location: /should-not-redirect";
    std::string raw = "Content-Type: text/plain\r\n\r\n" + bodyContent;

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));
    ASSERT_EQ(cgiResp.getStatusCode(), 200);
    ASSERT_EQ(toString(cgiResp.getBody()), bodyContent);

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 200);
    ASSERT_EQ(httpResp.getHeader("Location"), "");
    ASSERT_EQ(toString(httpResp.getBody()), bodyContent);
}

TEST(CgiResponse_TestParseBinaryBodyWithEmbeddedNulls) {
    CgiResponse cgiResp;
    std::string header = "Content-Type: application/octet-stream\r\n\r\n";
    const char binaryPayload[] = {'\x00', '\x01', 'C', 'G', 'I', '\x00', '\xFF', '\x7F', '\x00'};
    size_t binSize = sizeof(binaryPayload);

    std::vector<char> raw(header.begin(), header.end());
    raw.insert(raw.end(), binaryPayload, binaryPayload + binSize);

    ASSERT_TRUE(cgiResp.parse(raw));
    ASSERT_EQ(cgiResp.getBody().size(), binSize);
    ASSERT_EQ(std::memcmp(&cgiResp.getBody()[0], binaryPayload, binSize), 0);

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getBody().size(), binSize);
    ASSERT_EQ(std::memcmp(&httpResp.getBody()[0], binaryPayload, binSize), 0);
}

// =============================================================================
// 3. CGI Status Header Field (RFC 3875 Section 6.3.3)
// =============================================================================

TEST(CgiResponse_TestStatusHeaderCustomCodesAndCaseInsensitivity) {
    // 1. Standard "Status: 404 Not Found"
    {
        CgiResponse cgiResp;
        std::string raw = "Status: 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found";
        ASSERT_TRUE(cgiResp.parse(toVector(raw)));
        ASSERT_EQ(cgiResp.getStatusCode(), 404);

        HttpResponse httpResp;
        cgiResp.applyTo(httpResp);
        ASSERT_EQ(httpResp.getStatusCode(), 404);
        // RFC 3875: Status is a CGI meta-header, not an HTTP response header field
        ASSERT_EQ(httpResp.getHeader("Status"), "");
        ASSERT_EQ(httpResp.getHeader("status"), "");
    }

    // 2. Lowercase "status:" and uppercase "STATUS:" with numeric code only
    {
        CgiResponse cgiResp1;
        ASSERT_TRUE(cgiResp1.parse(toVector("status: 201\nContent-Type: text/plain\n\ncreated")));
        ASSERT_EQ(cgiResp1.getStatusCode(), 201);

        CgiResponse cgiResp2;
        ASSERT_TRUE(cgiResp2.parse(toVector("STATUS: 503 Service Unavailable\r\n\r\nbusy")));
        ASSERT_EQ(cgiResp2.getStatusCode(), 503);
    }
}

TEST(CgiResponse_TestInvalidStatusHeaderRejected) {
    CgiResponse cgiResp;

    // Non-numeric status
    ASSERT_FALSE(cgiResp.parse(toVector("Status: OK\r\nContent-Type: text/plain\r\n\r\nbody")));
    // Empty status value
    ASSERT_FALSE(cgiResp.parse(toVector("Status: \r\nContent-Type: text/plain\r\n\r\nbody")));
    // Out of valid HTTP status range (< 100 or > 599)
    ASSERT_FALSE(cgiResp.parse(toVector("Status: 0\r\nContent-Type: text/plain\r\n\r\nbody")));
    ASSERT_FALSE(cgiResp.parse(toVector("Status: 99\r\nContent-Type: text/plain\r\n\r\nbody")));
    ASSERT_FALSE(cgiResp.parse(toVector("Status: 600\r\nContent-Type: text/plain\r\n\r\nbody")));
    ASSERT_FALSE(cgiResp.parse(toVector("Status: -200\r\nContent-Type: text/plain\r\n\r\nbody")));
}

// =============================================================================
// 4. CGI Redirect Response & Location Header (RFC 3875 Section 6.2.3 & 6.2.4)
// =============================================================================

TEST(CgiResponse_TestLocationHeaderDefaultsTo302Found) {
    CgiResponse cgiResp;
    std::string raw = "Location: https://example.com/login\r\n\r\n";

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 302);
    ASSERT_EQ(httpResp.getHeader("Location"), "https://example.com/login");
}

TEST(CgiResponse_TestLocationHeaderWithExplicitStatusPreservesStatus) {
    CgiResponse cgiResp;
    std::string raw =
        "Status: 301 Moved Permanently\r\n"
        "Location: /new-destination\r\n"
        "\r\n";

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 301);
    ASSERT_EQ(httpResp.getHeader("Location"), "/new-destination");
}

// =============================================================================
// 5. Header Whitespace, Default Content-Type & Reuse
// =============================================================================

TEST(CgiResponse_TestHeaderWhitespaceTrimmingAndDefaultContentType) {
    CgiResponse cgiResp;
    // CGI script outputs custom header with extra whitespace, no Content-Type, and non-empty body
    std::string raw =
        "X-Custom-Header :   \t  custom value with spaces   \t \r\n"
        "Set-Cookie: session=abc123; Path=/; HttpOnly\r\n"
        "\r\n"
        "<p>Fallback content type test</p>";

    ASSERT_TRUE(cgiResp.parse(toVector(raw)));

    HttpResponse httpResp;
    cgiResp.applyTo(httpResp);
    ASSERT_EQ(httpResp.getStatusCode(), 200);
    ASSERT_EQ(httpResp.getHeader("X-Custom-Header"), "custom value with spaces");
    ASSERT_EQ(httpResp.getHeader("Set-Cookie"), "session=abc123; Path=/; HttpOnly");
    ASSERT_FALSE(httpResp.getHeader("Content-Type").empty());
    ASSERT_EQ(toString(httpResp.getBody()), "<p>Fallback content type test</p>");
}

TEST(CgiResponse_TestReparseResetsStateCleanly) {
    CgiResponse cgiResp;

    ASSERT_TRUE(cgiResp.parse(toVector("Status: 404 Not Found\r\nX-Old: 1\r\n\r\nold body")));
    ASSERT_EQ(cgiResp.getStatusCode(), 404);
    ASSERT_EQ(toString(cgiResp.getBody()), "old body");

    // Second parse without Status or body must reset status to 200 and clear old headers/body
    ASSERT_TRUE(cgiResp.parse(toVector("Content-Type: text/plain\r\n\r\n")));
    ASSERT_EQ(cgiResp.getStatusCode(), 200);
    ASSERT_TRUE(cgiResp.getHeaders().find("X-Old") == cgiResp.getHeaders().end());
    ASSERT_TRUE(cgiResp.getBody().empty());
}

// =============================================================================
// 6. Malformed CGI Output Rejection (RFC 3875 Section 6 -> 502 Bad Gateway)
// =============================================================================

TEST(CgiResponse_TestMalformedOutputRejected) {
    CgiResponse cgiResp;

    // 1. Empty output (EOF immediately with 0 bytes)
    ASSERT_FALSE(cgiResp.parse(std::vector<char>()));

    // 2. Output without header-body blank line separator
    ASSERT_FALSE(cgiResp.parse(toVector("Content-Type: text/plain\r\nHello without blank line")));
    ASSERT_FALSE(cgiResp.parse(toVector("Just plain text without any headers")));

    // 3. Header line missing colon separator
    ASSERT_FALSE(cgiResp.parse(toVector("Content-Type: text/plain\r\nMalformedHeaderLine\r\n\r\nbody")));

    // 4. Header line with empty header name before colon
    ASSERT_FALSE(cgiResp.parse(toVector(": value-without-key\r\n\r\nbody")));
}

