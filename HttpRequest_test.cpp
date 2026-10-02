#include "tiny_test.hpp"
#include "HttpRequest.hpp"
#include <string>
#include <vector>
#include <map>

// =============================================================================
// 1. Initial State & Constructors
// =============================================================================

TEST(HttpRequest_TestInitialState) {
    HttpRequest req;

    ASSERT_EQ(req.getRequestState(), HttpRequest::PARSE_REQUEST_LINE);
    ASSERT_EQ(req.getMethod(), "");
    ASSERT_EQ(req.getUri(), "");
    ASSERT_EQ(req.getQueryString(), "");
    ASSERT_EQ(req.getVersion(), "");
    ASSERT_TRUE(req.getHeaders().empty());
    ASSERT_TRUE(req.getBody().empty());
    ASSERT_EQ(req.getContentLength(), 0u);
    ASSERT_EQ(req.getErrorCode(), 0);
    ASSERT_FALSE(req.isChunked());
}

TEST(HttpRequest_TestCopyConstructor) {
    HttpRequest original;
    original.setRequestState(HttpRequest::PARSE_HEADERS);
    original.setMethod("POST");
    original.setUri("/upload?user=42");
    original.setVersion("HTTP/1.1");
    original.addHeader("Content-Type", "text/plain");
    original.addHeader("Content-Length", "5");
    const char data[] = "hello";
    original.appendBody(data, 5);

    HttpRequest copy(original);

    ASSERT_EQ(copy.getRequestState(), HttpRequest::PARSE_HEADERS);
    ASSERT_EQ(copy.getMethod(), "POST");
    ASSERT_EQ(copy.getUri(), "/upload");
    ASSERT_EQ(copy.getQueryString(), "user=42");
    ASSERT_EQ(copy.getVersion(), "HTTP/1.1");
    ASSERT_EQ(copy.getContentLength(), 5u);
    ASSERT_EQ(copy.getErrorCode(), 0);
    ASSERT_EQ(copy.getBody().size(), 5u);
    ASSERT_EQ(copy.getHeaders().size(), 2u);
    ASSERT_EQ(copy.getHeaders().find("content-type")->second, "text/plain");

    // Modify original to verify deep copy independence
    const char extra[] = "world";
    original.appendBody(extra, 5);
    ASSERT_EQ(original.getBody().size(), 10u);
    ASSERT_EQ(copy.getBody().size(), 5u);
}

TEST(HttpRequest_TestAssignmentOperator) {
    HttpRequest original;
    original.setMethod("GET");
    original.setUri("/index.html");
    original.setVersion("HTTP/1.0");
    original.addHeader("Host", "localhost");

    HttpRequest assigned;
    assigned = original;

    ASSERT_EQ(assigned.getMethod(), "GET");
    ASSERT_EQ(assigned.getUri(), "/index.html");
    ASSERT_EQ(assigned.getVersion(), "HTTP/1.0");
    ASSERT_EQ(assigned.getHeaders().find("host")->second, "localhost");

    // Self-assignment safety check
    assigned = assigned;
    ASSERT_EQ(assigned.getMethod(), "GET");
    ASSERT_EQ(assigned.getUri(), "/index.html");
}

// =============================================================================
// 2. HTTP Method Validation (setMethod)
// =============================================================================

TEST(HttpRequest_TestSetMethodValid) {
    HttpRequest req1;
    req1.setMethod("GET");
    ASSERT_EQ(req1.getMethod(), "GET");
    ASSERT_EQ(req1.getErrorCode(), 0);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_REQUEST_LINE);

    HttpRequest req2;
    req2.setMethod("POST");
    ASSERT_EQ(req2.getMethod(), "POST");
    ASSERT_EQ(req2.getErrorCode(), 0);

    HttpRequest req3;
    req3.setMethod("DELETE");
    ASSERT_EQ(req3.getMethod(), "DELETE");
    ASSERT_EQ(req3.getErrorCode(), 0);
}

TEST(HttpRequest_TestSetMethodInvalidCharacters) {
    // Lowercase method must be rejected as 400 Bad Request
    HttpRequest req1;
    req1.setMethod("get");
    ASSERT_EQ(req1.getErrorCode(), 400);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);
    ASSERT_EQ(req1.getMethod(), "");

    // Mixed case
    HttpRequest req2;
    req2.setMethod("Post");
    ASSERT_EQ(req2.getErrorCode(), 400);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);

    // Alphanumeric / special characters
    HttpRequest req3;
    req3.setMethod("GET1");
    ASSERT_EQ(req3.getErrorCode(), 400);
    ASSERT_EQ(req3.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req4;
    req4.setMethod("POST!");
    ASSERT_EQ(req4.getErrorCode(), 400);
    ASSERT_EQ(req4.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req5;
    req5.setMethod("GET ");
    ASSERT_EQ(req5.getErrorCode(), 400);
    ASSERT_EQ(req5.getRequestState(), HttpRequest::PARSE_ERROR);
}

TEST(HttpRequest_TestSetMethodUnsupported) {
    // Valid HTTP token characters, but method is not implemented (501 Not Implemented)
    HttpRequest req1;
    req1.setMethod("PUT");
    ASSERT_EQ(req1.getErrorCode(), 501);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req2;
    req2.setMethod("HEAD");
    ASSERT_EQ(req2.getErrorCode(), 501);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req3;
    req3.setMethod("OPTIONS");
    ASSERT_EQ(req3.getErrorCode(), 501);
    ASSERT_EQ(req3.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req4;
    req4.setMethod("PATCH");
    ASSERT_EQ(req4.getErrorCode(), 501);
    ASSERT_EQ(req4.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req5;
    req5.setMethod("CUSTOM");
    ASSERT_EQ(req5.getErrorCode(), 501);
    ASSERT_EQ(req5.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req6;
    req6.setMethod("");
    ASSERT_EQ(req6.getErrorCode(), 501);
    ASSERT_EQ(req6.getRequestState(), HttpRequest::PARSE_ERROR);
}

// =============================================================================
// 3. HTTP Version Validation (setVersion)
// =============================================================================

TEST(HttpRequest_TestSetVersionValid) {
    HttpRequest req1;
    req1.setVersion("HTTP/1.1");
    ASSERT_EQ(req1.getVersion(), "HTTP/1.1");
    ASSERT_EQ(req1.getErrorCode(), 0);

    HttpRequest req2;
    req2.setVersion("HTTP/1.0");
    ASSERT_EQ(req2.getVersion(), "HTTP/1.0");
    ASSERT_EQ(req2.getErrorCode(), 0);
}

TEST(HttpRequest_TestSetVersionInvalid) {
    // Unsupported or malformed versions must yield 505 HTTP Version Not Supported
    HttpRequest req1;
    req1.setVersion("HTTP/2.0");
    ASSERT_EQ(req1.getErrorCode(), 505);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req2;
    req2.setVersion("HTTP/0.9");
    ASSERT_EQ(req2.getErrorCode(), 505);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req3;
    req3.setVersion("http/1.1"); // Lowercase
    ASSERT_EQ(req3.getErrorCode(), 505);
    ASSERT_EQ(req3.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req4;
    req4.setVersion("");
    ASSERT_EQ(req4.getErrorCode(), 505);
    ASSERT_EQ(req4.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req5;
    req5.setVersion("FTP/1.0");
    ASSERT_EQ(req5.getErrorCode(), 505);
    ASSERT_EQ(req5.getRequestState(), HttpRequest::PARSE_ERROR);
}

// =============================================================================
// 4. URI Parsing, Query String, Percent Decoding & Security (setUri)
// =============================================================================

TEST(HttpRequest_TestSetUriValidSimple) {
    HttpRequest req1;
    req1.setUri("/");
    ASSERT_EQ(req1.getUri(), "/");
    ASSERT_EQ(req1.getQueryString(), "");
    ASSERT_EQ(req1.getErrorCode(), 0);

    HttpRequest req2;
    req2.setUri("/index.html");
    ASSERT_EQ(req2.getUri(), "/index.html");
    ASSERT_EQ(req2.getQueryString(), "");
    ASSERT_EQ(req2.getErrorCode(), 0);

    HttpRequest req3;
    req3.setUri("/images/logo.png");
    ASSERT_EQ(req3.getUri(), "/images/logo.png");
    ASSERT_EQ(req3.getQueryString(), "");
    ASSERT_EQ(req3.getErrorCode(), 0);
}

TEST(HttpRequest_TestSetUriWithQueryString) {
    HttpRequest req1;
    req1.setUri("/search?q=webserv&page=1");
    ASSERT_EQ(req1.getUri(), "/search");
    ASSERT_EQ(req1.getQueryString(), "q=webserv&page=1");
    ASSERT_EQ(req1.getErrorCode(), 0);

    HttpRequest req2;
    req2.setUri("/api?");
    ASSERT_EQ(req2.getUri(), "/api");
    ASSERT_EQ(req2.getQueryString(), "");
    ASSERT_EQ(req2.getErrorCode(), 0);
}

TEST(HttpRequest_TestSetUriPercentDecoding) {
    // Path percent-decoding
    HttpRequest req1;
    req1.setUri("/hello%20world");
    ASSERT_EQ(req1.getUri(), "/hello world");
    ASSERT_EQ(req1.getQueryString(), "");
    ASSERT_EQ(req1.getErrorCode(), 0);

    // Uppercase and lowercase hex digits
    HttpRequest req2;
    req2.setUri("/test%2fslash%2Fanother");
    ASSERT_EQ(req2.getUri(), "/test/slash/another");
    ASSERT_EQ(req2.getErrorCode(), 0);

    // Query string percent-decoding
    HttpRequest req3;
    req3.setUri("/profile?user=John%20Doe&comment=%3Chello%3E");
    ASSERT_EQ(req3.getUri(), "/profile");
    ASSERT_EQ(req3.getQueryString(), "user=John Doe&comment=<hello>");
    ASSERT_EQ(req3.getErrorCode(), 0);
}

TEST(HttpRequest_TestSetUriInvalidFormat) {
    // Empty URI
    HttpRequest req1;
    req1.setUri("");
    ASSERT_EQ(req1.getErrorCode(), 400);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    // Missing leading slash (relative path)
    HttpRequest req2;
    req2.setUri("index.html");
    ASSERT_EQ(req2.getErrorCode(), 400);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);

    // Unencoded whitespace
    HttpRequest req3;
    req3.setUri("/hello world");
    ASSERT_EQ(req3.getErrorCode(), 400);
    ASSERT_EQ(req3.getRequestState(), HttpRequest::PARSE_ERROR);

    // Control characters (e.g., tab, newline)
    HttpRequest req4;
    req4.setUri("/hello\tworld");
    ASSERT_EQ(req4.getErrorCode(), 400);
    ASSERT_EQ(req4.getRequestState(), HttpRequest::PARSE_ERROR);

    // Incomplete percent-encoding
    HttpRequest req5;
    req5.setUri("/test%");
    ASSERT_EQ(req5.getErrorCode(), 400);
    ASSERT_EQ(req5.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req6;
    req6.setUri("/test%2");
    ASSERT_EQ(req6.getErrorCode(), 400);
    ASSERT_EQ(req6.getRequestState(), HttpRequest::PARSE_ERROR);

    // Invalid hexadecimal characters in percent-encoding
    HttpRequest req7;
    req7.setUri("/test%2G");
    ASSERT_EQ(req7.getErrorCode(), 400);
    ASSERT_EQ(req7.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req8;
    req8.setUri("/test%ZZ");
    ASSERT_EQ(req8.getErrorCode(), 400);
    ASSERT_EQ(req8.getRequestState(), HttpRequest::PARSE_ERROR);
}

TEST(HttpRequest_TestSetUriPathTraversalSecurity) {
    // Sequences allowing directory climbing must be rejected with 403 Forbidden
    HttpRequest req1;
    req1.setUri("/../etc/passwd");
    ASSERT_EQ(req1.getErrorCode(), 403);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req2;
    req2.setUri("/sub/../secret");
    ASSERT_EQ(req2.getErrorCode(), 403);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req3;
    req3.setUri("/path/..");
    ASSERT_EQ(req3.getErrorCode(), 403);
    ASSERT_EQ(req3.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req4;
    req4.setUri("/path/..?query=val");
    ASSERT_EQ(req4.getErrorCode(), 403);
    ASSERT_EQ(req4.getRequestState(), HttpRequest::PARSE_ERROR);
}

TEST(HttpRequest_TestSetUriMaxLength) {
    // Max length is 8192
    std::string boundaryUri = "/" + std::string(8191, 'a');
    HttpRequest req1;
    req1.setUri(boundaryUri);
    ASSERT_EQ(req1.getErrorCode(), 0);
    ASSERT_EQ(req1.getUri(), boundaryUri);

    // 8193 characters exceeds MAX_URI_LENGTH -> 414 URI Too Long
    std::string tooLongUri = "/" + std::string(8192, 'a');
    HttpRequest req2;
    req2.setUri(tooLongUri);
    ASSERT_EQ(req2.getErrorCode(), 414);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);
}

// =============================================================================
// 5. Header Handling & Validation (addHeader)
// =============================================================================

TEST(HttpRequest_TestAddHeaderGeneric) {
    HttpRequest req;
    req.addHeader("User-Agent", "curl/7.81.0");
    req.addHeader("Accept", "text/html,application/json");

    ASSERT_EQ(req.getErrorCode(), 0);
    ASSERT_EQ(req.getHeaders().size(), 2u);

    std::map<std::string, std::string>::const_iterator it = req.getHeaders().find("user-agent");
    ASSERT_TRUE(it != req.getHeaders().end());
    ASSERT_EQ(it->second, "curl/7.81.0");

    it = req.getHeaders().find("accept");
    ASSERT_TRUE(it != req.getHeaders().end());
    ASSERT_EQ(it->second, "text/html,application/json");
}

TEST(HttpRequest_TestAddHeaderCaseInsensitivity) {
    HttpRequest req;
    // Header keys should be normalized to lower-case
    req.addHeader("Content-Type", "text/html");
    req.addHeader("X-CUSTOM-HEADER", "custom-value");

    ASSERT_TRUE(req.getHeaders().find("content-type") != req.getHeaders().end());
    ASSERT_TRUE(req.getHeaders().find("Content-Type") == req.getHeaders().end());
    ASSERT_TRUE(req.getHeaders().find("x-custom-header") != req.getHeaders().end());
}

TEST(HttpRequest_TestAddHeaderInvalidKeys) {
    // Forbidden RFC separator characters or whitespace in field name -> 400 Bad Request
    HttpRequest req1;
    req1.addHeader("", "value");
    ASSERT_EQ(req1.getErrorCode(), 400);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req2;
    req2.addHeader("Header:Name", "value");
    ASSERT_EQ(req2.getErrorCode(), 400);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req3;
    req3.addHeader("Header Name", "value");
    ASSERT_EQ(req3.getErrorCode(), 400);
    ASSERT_EQ(req3.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req4;
    req4.addHeader("Header@Name", "value");
    ASSERT_EQ(req4.getErrorCode(), 400);
    ASSERT_EQ(req4.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req5;
    req5.addHeader("Header[Name]", "value");
    ASSERT_EQ(req5.getErrorCode(), 400);
    ASSERT_EQ(req5.getRequestState(), HttpRequest::PARSE_ERROR);
}

TEST(HttpRequest_TestAddHeaderHostValid) {
    HttpRequest req1;
    req1.addHeader("Host", "localhost");
    ASSERT_EQ(req1.getErrorCode(), 0);

    HttpRequest req2;
    req2.addHeader("Host", "example.com");
    ASSERT_EQ(req2.getErrorCode(), 0);

    HttpRequest req3;
    req3.addHeader("Host", "sub.domain-test.org");
    ASSERT_EQ(req3.getErrorCode(), 0);

    HttpRequest req4;
    req4.addHeader("Host", "127.0.0.1");
    ASSERT_EQ(req4.getErrorCode(), 0);

    HttpRequest req5;
    req5.addHeader("Host", "192.168.1.100:8080");
    ASSERT_EQ(req5.getErrorCode(), 0);

    HttpRequest req6;
    req6.addHeader("Host", "localhost:4242");
    ASSERT_EQ(req6.getErrorCode(), 0);
}

TEST(HttpRequest_TestAddHeaderHostInvalid) {
    // Empty Host
    HttpRequest req1;
    req1.addHeader("Host", "");
    ASSERT_EQ(req1.getErrorCode(), 400);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    // Invalid port (non-numeric, out-of-range, or port 0)
    HttpRequest req2;
    req2.addHeader("Host", "localhost:abc");
    ASSERT_EQ(req2.getErrorCode(), 400);

    HttpRequest req3;
    req3.addHeader("Host", "localhost:0");
    ASSERT_EQ(req3.getErrorCode(), 400);

    HttpRequest req4;
    req4.addHeader("Host", "localhost:70000");
    ASSERT_EQ(req4.getErrorCode(), 400);

    HttpRequest req5;
    req5.addHeader("Host", "localhost:");
    ASSERT_EQ(req5.getErrorCode(), 400);

    // Invalid IPv4 (octet out of range, bad segment count, leading zeros)
    HttpRequest req6;
    req6.addHeader("Host", "256.0.0.1");
    ASSERT_EQ(req6.getErrorCode(), 400);

    HttpRequest req7;
    req7.addHeader("Host", "192.168.1");
    ASSERT_EQ(req7.getErrorCode(), 400);

    HttpRequest req8;
    req8.addHeader("Host", "192.168.01.1");
    ASSERT_EQ(req8.getErrorCode(), 400);

    HttpRequest req9;
    req9.addHeader("Host", ".192.168.1.1");
    ASSERT_EQ(req9.getErrorCode(), 400);

    // Invalid domain names (starts or ends with hyphen, all-digit TLD)
    HttpRequest req10;
    req10.addHeader("Host", "-example.com");
    ASSERT_EQ(req10.getErrorCode(), 400);

    HttpRequest req11;
    req11.addHeader("Host", "example-.com");
    ASSERT_EQ(req11.getErrorCode(), 400);

    HttpRequest req12;
    req12.addHeader("Host", "example..com");
    ASSERT_EQ(req12.getErrorCode(), 400);

    HttpRequest req13;
    req13.addHeader("Host", "123.456");
    ASSERT_EQ(req13.getErrorCode(), 400);
}

TEST(HttpRequest_TestAddHeaderHostDuplicate) {
    HttpRequest req;
    req.addHeader("Host", "localhost");
    ASSERT_EQ(req.getErrorCode(), 0);

    // Duplicate Host header must trigger 400 Bad Request
    req.addHeader("host", "example.com");
    ASSERT_EQ(req.getErrorCode(), 400);
    ASSERT_EQ(req.getRequestState(), HttpRequest::PARSE_ERROR);
}

TEST(HttpRequest_TestAddHeaderContentLengthValid) {
    HttpRequest req1;
    req1.addHeader("Content-Length", "1024");
    ASSERT_EQ(req1.getErrorCode(), 0);
    ASSERT_EQ(req1.getContentLength(), 1024u);

    HttpRequest req2;
    req2.addHeader("content-length", "0");
    ASSERT_EQ(req2.getErrorCode(), 0);
    ASSERT_EQ(req2.getContentLength(), 0u);
}

TEST(HttpRequest_TestAddHeaderContentLengthInvalid) {
    // Non-digit characters
    HttpRequest req1;
    req1.addHeader("Content-Length", "1024abc");
    ASSERT_EQ(req1.getErrorCode(), 400);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    // Negative number
    HttpRequest req2;
    req2.addHeader("Content-Length", "-10");
    ASSERT_EQ(req2.getErrorCode(), 400);

    // Empty value
    HttpRequest req3;
    req3.addHeader("Content-Length", "");
    ASSERT_EQ(req3.getErrorCode(), 400);

    // Duplicate Content-Length
    HttpRequest req4;
    req4.addHeader("Content-Length", "100");
    ASSERT_EQ(req4.getErrorCode(), 0);
    req4.addHeader("Content-Length", "200");
    ASSERT_EQ(req4.getErrorCode(), 400);
}

TEST(HttpRequest_TestAddHeaderTransferEncodingValid) {
    HttpRequest req1;
    req1.addHeader("Transfer-Encoding", "chunked");
    ASSERT_EQ(req1.getErrorCode(), 0);
    ASSERT_TRUE(req1.isChunked());

    // Case-insensitivity in "chunked" value
    HttpRequest req2;
    req2.addHeader("transfer-encoding", "Chunked");
    ASSERT_EQ(req2.getErrorCode(), 0);
    ASSERT_TRUE(req2.isChunked());
}

TEST(HttpRequest_TestAddHeaderTransferEncodingUnsupported) {
    // Non-chunked values must be rejected with 501 Not Implemented
    HttpRequest req1;
    req1.addHeader("Transfer-Encoding", "gzip");
    ASSERT_EQ(req1.getErrorCode(), 501);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req2;
    req2.addHeader("Transfer-Encoding", "compress");
    ASSERT_EQ(req2.getErrorCode(), 501);

    HttpRequest req3;
    req3.addHeader("Transfer-Encoding", "deflate");
    ASSERT_EQ(req3.getErrorCode(), 501);
}

TEST(HttpRequest_TestAddHeaderTransferEncodingDuplicate) {
    HttpRequest req;
    req.addHeader("Transfer-Encoding", "chunked");
    ASSERT_EQ(req.getErrorCode(), 0);

    req.addHeader("Transfer-Encoding", "chunked");
    ASSERT_EQ(req.getErrorCode(), 400);
    ASSERT_EQ(req.getRequestState(), HttpRequest::PARSE_ERROR);
}

TEST(HttpRequest_TestAddHeaderConflictContentLengthAndTransferEncoding) {
    // Both Content-Length and Transfer-Encoding in the same request is forbidden (RFC 7230)
    HttpRequest req1;
    req1.addHeader("Content-Length", "50");
    req1.addHeader("Transfer-Encoding", "chunked");
    ASSERT_EQ(req1.getErrorCode(), 400);
    ASSERT_EQ(req1.getRequestState(), HttpRequest::PARSE_ERROR);

    HttpRequest req2;
    req2.addHeader("Transfer-Encoding", "chunked");
    req2.addHeader("Content-Length", "50");
    ASSERT_EQ(req2.getErrorCode(), 400);
    ASSERT_EQ(req2.getRequestState(), HttpRequest::PARSE_ERROR);
}

// =============================================================================
// 6. Body Accumulation & Raw Bytes (appendBody)
// =============================================================================

TEST(HttpRequest_TestAppendBodyText) {
    HttpRequest req;
    const char chunk1[] = "Hello";
    const char chunk2[] = ", ";
    const char chunk3[] = "World!";

    req.appendBody(chunk1, 5);
    ASSERT_EQ(req.getBody().size(), 5u);

    req.appendBody(chunk2, 2);
    ASSERT_EQ(req.getBody().size(), 7u);

    req.appendBody(chunk3, 6);
    ASSERT_EQ(req.getBody().size(), 13u);

    std::string bodyStr(req.getBody().begin(), req.getBody().end());
    ASSERT_EQ(bodyStr, "Hello, World!");
}

TEST(HttpRequest_TestAppendBodyBinaryAndNullBytes) {
    HttpRequest req;
    const char binaryData[] = {'\x00', 'A', '\x00', 'B', '\xFF', '\x7F'};
    size_t dataSize = sizeof(binaryData);

    req.appendBody(binaryData, dataSize);
    ASSERT_EQ(req.getBody().size(), dataSize);

    // Verify raw bytes are not null-truncated
    ASSERT_EQ(req.getBody()[0], '\x00');
    ASSERT_EQ(req.getBody()[1], 'A');
    ASSERT_EQ(req.getBody()[2], '\x00');
    ASSERT_EQ(req.getBody()[3], 'B');
    ASSERT_EQ(req.getBody()[4], '\xFF');
    ASSERT_EQ(req.getBody()[5], '\x7F');
}

TEST(HttpRequest_TestAppendBodyEdgeCases) {
    HttpRequest req;

    // Passing NULL or size 0 should safely do nothing
    req.appendBody(NULL, 10);
    ASSERT_EQ(req.getBody().size(), 0u);

    const char data[] = "data";
    req.appendBody(data, 0);
    ASSERT_EQ(req.getBody().size(), 0u);
}

// =============================================================================
// 7. Direct State and Attribute Setters/Getters
// =============================================================================

TEST(HttpRequest_TestDirectSettersAndGetters) {
    HttpRequest req;

    req.setRequestState(HttpRequest::PARSE_BODY);
    ASSERT_EQ(req.getRequestState(), HttpRequest::PARSE_BODY);

    req.setRequestState(HttpRequest::PARSE_DONE);
    ASSERT_EQ(req.getRequestState(), HttpRequest::PARSE_DONE);

    req.setErrorCode(418);
    ASSERT_EQ(req.getErrorCode(), 418);

    req.setContentLength(9999);
    ASSERT_EQ(req.getContentLength(), 9999u);
}

// =============================================================================
// Main Test Runner Entry Point
// =============================================================================

int main() {
    return RUN_ALL_TESTS();
}
