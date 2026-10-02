#include "tiny_test.hpp"
#include "HttpResponse.hpp"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstring>

namespace {

std::string vectorToString(const std::vector<char>& vec) {
    if (vec.empty()) {
        return std::string();
    }
    return std::string(vec.begin(), vec.end());
}

} // namespace

// =============================================================================
// 1. Initial State & Orthodox Canonical Form
// =============================================================================

TEST(HttpResponse_TestInitialState) {
    HttpResponse resp;

    ASSERT_EQ(resp.getStatusCode(), 0);
    ASSERT_TRUE(resp.getBody().empty());
    ASSERT_EQ(resp.getBody().size(), 0u);

    // Initial createResponse produces default HTTP/1.0 status line with code 0 and empty status message
    std::vector<char> raw = resp.createResponse();
    std::string responseStr = vectorToString(raw);
    ASSERT_EQ(responseStr, "HTTP/1.0 0 \r\n\r\n");
}

TEST(HttpResponse_TestCopyConstructorBasic) {
    HttpResponse original;
    original.setStatusCode(200);
    original.setHeader("Content-Type", "text/plain");
    original.setHeader("Content-Length", "5");
    original.setBody("hello");

    HttpResponse copy(original);

    ASSERT_EQ(copy.getStatusCode(), 200);
    ASSERT_EQ(copy.getBody().size(), 5u);
    ASSERT_EQ(vectorToString(copy.getBody()), "hello");

    std::string originalStr = vectorToString(original.createResponse());
    std::string copyStr = vectorToString(copy.createResponse());
    ASSERT_EQ(copyStr, originalStr);
}

TEST(HttpResponse_TestCopyConstructorIndependence) {
    HttpResponse original;
    original.setStatusCode(200);
    original.setHeader("Server", "webserv");
    original.setBody("original content");

    HttpResponse copy(original);

    // Mutate original - copy must remain unaffected
    original.setStatusCode(404);
    original.setHeader("Server", "modified");
    original.setHeader("X-Extra", "extra");
    original.setBody("different content");

    ASSERT_EQ(copy.getStatusCode(), 200);
    ASSERT_EQ(vectorToString(copy.getBody()), "original content");

    std::string copyStr = vectorToString(copy.createResponse());
    ASSERT_TRUE(copyStr.find("HTTP/1.0 200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(copyStr.find("Server: webserv\r\n") != std::string::npos);
    ASSERT_TRUE(copyStr.find("X-Extra") == std::string::npos);
    ASSERT_TRUE(copyStr.find("original content") != std::string::npos);

    // Mutate copy - original must remain unaffected
    copy.setStatusCode(500);
    copy.setBody("copy mutated");
    ASSERT_EQ(original.getStatusCode(), 404);
    ASSERT_EQ(vectorToString(original.getBody()), "different content");
}

TEST(HttpResponse_TestAssignmentOperatorBasic) {
    HttpResponse original;
    original.setStatusCode(201);
    original.setHeader("Location", "/users/42");
    original.setBody("Created resource");

    HttpResponse assigned;
    assigned = original;

    ASSERT_EQ(assigned.getStatusCode(), 201);
    ASSERT_EQ(assigned.getBody().size(), 16u);
    ASSERT_EQ(vectorToString(assigned.getBody()), "Created resource");

    std::string originalStr = vectorToString(original.createResponse());
    std::string assignedStr = vectorToString(assigned.createResponse());
    ASSERT_EQ(assignedStr, originalStr);
}

TEST(HttpResponse_TestAssignmentOperatorIndependence) {
    HttpResponse original;
    original.setStatusCode(200);
    original.setHeader("Content-Type", "application/json");
    original.setBody("{\"key\":\"value\"}");

    HttpResponse assigned;
    assigned = original;

    // Mutate original after assignment
    original.setStatusCode(503);
    original.setHeader("Retry-After", "120");
    original.setBody("Service unavailable");

    ASSERT_EQ(assigned.getStatusCode(), 200);
    ASSERT_EQ(vectorToString(assigned.getBody()), "{\"key\":\"value\"}");

    std::string assignedStr = vectorToString(assigned.createResponse());
    ASSERT_TRUE(assignedStr.find("HTTP/1.0 200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(assignedStr.find("Retry-After") == std::string::npos);
}

TEST(HttpResponse_TestAssignmentOperatorSelfAssignment) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Content-Type", "text/html");
    resp.setBody("test body");

    resp = resp;

    ASSERT_EQ(resp.getStatusCode(), 200);
    ASSERT_EQ(resp.getBody().size(), 9u);
    ASSERT_EQ(vectorToString(resp.getBody()), "test body");

    std::string respStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(respStr.find("HTTP/1.0 200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(respStr.find("Content-Type: text/html\r\n") != std::string::npos);
    ASSERT_TRUE(respStr.find("test body") != std::string::npos);
}

TEST(HttpResponse_TestAssignmentOperatorChaining) {
    HttpResponse src;
    src.setStatusCode(204);
    src.setHeader("Connection", "close");

    HttpResponse dest1;
    HttpResponse dest2;

    dest2 = dest1 = src;

    ASSERT_EQ(dest1.getStatusCode(), 204);
    ASSERT_EQ(dest2.getStatusCode(), 204);
    ASSERT_EQ(vectorToString(dest1.createResponse()), vectorToString(src.createResponse()));
    ASSERT_EQ(vectorToString(dest2.createResponse()), vectorToString(src.createResponse()));
}

// =============================================================================
// 2. HTTP Status Codes & Reason Phrases (getStatusMessage)
// =============================================================================

TEST(HttpResponse_TestGetStatusMessageSuccess2xx) {
    HttpResponse resp;

    ASSERT_EQ(resp.getStatusMessage(200), "OK");
    ASSERT_EQ(resp.getStatusMessage(201), "Created");
    ASSERT_EQ(resp.getStatusMessage(202), "Accepted");
    ASSERT_EQ(resp.getStatusMessage(204), "No Content");
}

TEST(HttpResponse_TestGetStatusMessageRedirection3xx) {
    HttpResponse resp;

    ASSERT_EQ(resp.getStatusMessage(301), "Moved Permanently");
    ASSERT_EQ(resp.getStatusMessage(302), "Found");
    ASSERT_EQ(resp.getStatusMessage(304), "Not Modified");
    ASSERT_EQ(resp.getStatusMessage(307), "Temporary Redirect");
    ASSERT_EQ(resp.getStatusMessage(308), "Permanent Redirect");
}

TEST(HttpResponse_TestGetStatusMessageClientError4xx) {
    HttpResponse resp;

    ASSERT_EQ(resp.getStatusMessage(400), "Bad Request");
    ASSERT_EQ(resp.getStatusMessage(401), "Unauthorized");
    ASSERT_EQ(resp.getStatusMessage(403), "Forbidden");
    ASSERT_EQ(resp.getStatusMessage(404), "Not Found");
    ASSERT_EQ(resp.getStatusMessage(405), "Method Not Allowed");
    ASSERT_EQ(resp.getStatusMessage(408), "Request Timeout");
    ASSERT_EQ(resp.getStatusMessage(411), "Length Required");
    ASSERT_EQ(resp.getStatusMessage(413), "Payload Too Large");
    ASSERT_EQ(resp.getStatusMessage(414), "URI Too Long");
}

TEST(HttpResponse_TestGetStatusMessageServerError5xx) {
    HttpResponse resp;

    ASSERT_EQ(resp.getStatusMessage(500), "Internal Server Error");
    ASSERT_EQ(resp.getStatusMessage(501), "Not Implemented");
    ASSERT_EQ(resp.getStatusMessage(502), "Bad Gateway");
    ASSERT_EQ(resp.getStatusMessage(503), "Service Unavailable");
    ASSERT_EQ(resp.getStatusMessage(505), "HTTP Version Not Supported");
}

TEST(HttpResponse_TestGetStatusMessageUnknownCodes) {
    HttpResponse resp;

    // Codes not defined in the switch statement must return "Unknown Status"
    ASSERT_EQ(resp.getStatusMessage(0), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(100), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(101), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(203), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(299), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(300), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(303), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(402), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(418), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(499), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(504), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(599), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(999), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(-1), "Unknown Status");
    ASSERT_EQ(resp.getStatusMessage(-404), "Unknown Status");
}

TEST(HttpResponse_TestGetStatusMessageConstMethod) {
    const HttpResponse resp;

    // getStatusMessage should be callable on a const instance without prior modification
    ASSERT_EQ(resp.getStatusMessage(200), "OK");
    ASSERT_EQ(resp.getStatusMessage(404), "Not Found");
    ASSERT_EQ(resp.getStatusMessage(500), "Internal Server Error");
}

// =============================================================================
// 3. Status Code Mutation (setStatusCode & getStatusCode)
// =============================================================================

TEST(HttpResponse_TestSetStatusCodeValid) {
    HttpResponse resp;

    resp.setStatusCode(200);
    ASSERT_EQ(resp.getStatusCode(), 200);

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 200 OK\r\n") == 0);
}

TEST(HttpResponse_TestSetStatusCodeSuccessiveUpdates) {
    HttpResponse resp;

    resp.setStatusCode(200);
    ASSERT_EQ(resp.getStatusCode(), 200);
    ASSERT_TRUE(vectorToString(resp.createResponse()).find("HTTP/1.0 200 OK\r\n") == 0);

    resp.setStatusCode(404);
    ASSERT_EQ(resp.getStatusCode(), 404);
    ASSERT_TRUE(vectorToString(resp.createResponse()).find("HTTP/1.0 404 Not Found\r\n") == 0);

    resp.setStatusCode(500);
    ASSERT_EQ(resp.getStatusCode(), 500);
    ASSERT_TRUE(vectorToString(resp.createResponse()).find("HTTP/1.0 500 Internal Server Error\r\n") == 0);

    resp.setStatusCode(201);
    ASSERT_EQ(resp.getStatusCode(), 201);
    ASSERT_TRUE(vectorToString(resp.createResponse()).find("HTTP/1.0 201 Created\r\n") == 0);
}

TEST(HttpResponse_TestSetStatusCodeUnknownCode) {
    HttpResponse resp;

    resp.setStatusCode(418);
    ASSERT_EQ(resp.getStatusCode(), 418);

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 418 Unknown Status\r\n") == 0);
}

// =============================================================================
// 4. Header Management (setHeader)
// =============================================================================

TEST(HttpResponse_TestSetHeaderSingle) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Content-Type", "text/html; charset=UTF-8");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("Content-Type: text/html; charset=UTF-8\r\n") != std::string::npos);
}

TEST(HttpResponse_TestSetHeaderMultipleDistinct) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Server", "webserv/1.0");
    resp.setHeader("Content-Type", "application/json");
    resp.setHeader("Content-Length", "42");
    resp.setHeader("Connection", "keep-alive");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("Server: webserv/1.0\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Content-Type: application/json\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Content-Length: 42\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Connection: keep-alive\r\n") != std::string::npos);
}

TEST(HttpResponse_TestSetHeaderOverwrite) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Content-Length", "100");
    resp.setHeader("Content-Length", "250");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("Content-Length: 250\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Content-Length: 100\r\n") == std::string::npos);
}

TEST(HttpResponse_TestSetHeaderLexicographicalSorting) {
    // std::map sorts keys alphabetically by ASCII value
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Server", "webserv");
    resp.setHeader("Accept-Ranges", "bytes");
    resp.setHeader("Content-Type", "text/plain");
    resp.setHeader("Cache-Control", "no-cache");

    std::string responseStr = vectorToString(resp.createResponse());

    size_t posAccept = responseStr.find("Accept-Ranges: bytes\r\n");
    size_t posCache = responseStr.find("Cache-Control: no-cache\r\n");
    size_t posContent = responseStr.find("Content-Type: text/plain\r\n");
    size_t posServer = responseStr.find("Server: webserv\r\n");

    ASSERT_TRUE(posAccept != std::string::npos);
    ASSERT_TRUE(posCache != std::string::npos);
    ASSERT_TRUE(posContent != std::string::npos);
    ASSERT_TRUE(posServer != std::string::npos);

    ASSERT_TRUE(posAccept < posCache);
    ASSERT_TRUE(posCache < posContent);
    ASSERT_TRUE(posContent < posServer);
}

TEST(HttpResponse_TestSetHeaderEmptyValue) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("X-Empty-Header", "");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("X-Empty-Header: \r\n") != std::string::npos);
}

TEST(HttpResponse_TestSetHeaderSpecialCharacters) {
    HttpResponse resp;
    resp.setStatusCode(301);
    resp.setHeader("Location", "http://localhost:8080/path/to/page?user=42&view=full");
    resp.setHeader("Set-Cookie", "sessionId=abc123xyz; Path=/; HttpOnly");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("Location: http://localhost:8080/path/to/page?user=42&view=full\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Set-Cookie: sessionId=abc123xyz; Path=/; HttpOnly\r\n") != std::string::npos);
}

// =============================================================================
// 5. Body Management (setBody & getBody)
// =============================================================================

TEST(HttpResponse_TestSetBodyStringStandard) {
    HttpResponse resp;
    std::string body = "<html><body><h1>Hello World</h1></body></html>";
    resp.setBody(body);

    ASSERT_EQ(resp.getBody().size(), body.length());
    ASSERT_EQ(vectorToString(resp.getBody()), body);
}

TEST(HttpResponse_TestSetBodyStringEmpty) {
    HttpResponse resp;
    resp.setBody("some initial data");
    ASSERT_FALSE(resp.getBody().empty());

    resp.setBody("");
    ASSERT_TRUE(resp.getBody().empty());
    ASSERT_EQ(resp.getBody().size(), 0u);
}

TEST(HttpResponse_TestSetBodyVectorChar) {
    HttpResponse resp;
    const char data[] = "vector body content";
    std::vector<char> vec(data, data + std::strlen(data));

    resp.setBody(vec);
    ASSERT_EQ(resp.getBody().size(), vec.size());
    ASSERT_EQ(resp.getBody(), vec);
}

TEST(HttpResponse_TestSetBodyVectorEmpty) {
    HttpResponse resp;
    resp.setBody("pre-existing body");

    std::vector<char> emptyVec;
    resp.setBody(emptyVec);

    ASSERT_TRUE(resp.getBody().empty());
    ASSERT_EQ(resp.getBody().size(), 0u);
}

TEST(HttpResponse_TestSetBodyBinaryWithEmbeddedNulls) {
    HttpResponse resp;
    char rawBytes[] = {'\x00', 'A', '\x00', 'B', '\xFF', '\x7F', '\x00'};
    size_t numBytes = sizeof(rawBytes);
    std::vector<char> binaryData(rawBytes, rawBytes + numBytes);

    resp.setBody(binaryData);

    ASSERT_EQ(resp.getBody().size(), numBytes);
    ASSERT_EQ(resp.getBody()[0], '\x00');
    ASSERT_EQ(resp.getBody()[1], 'A');
    ASSERT_EQ(resp.getBody()[2], '\x00');
    ASSERT_EQ(resp.getBody()[3], 'B');
    ASSERT_EQ(resp.getBody()[4], '\xFF');
    ASSERT_EQ(resp.getBody()[5], '\x7F');
    ASSERT_EQ(resp.getBody()[6], '\x00');
}

TEST(HttpResponse_TestSetBodyOverwriteStringWithString) {
    HttpResponse resp;
    resp.setBody("First body content");
    ASSERT_EQ(vectorToString(resp.getBody()), "First body content");

    resp.setBody("Second body replacement");
    ASSERT_EQ(vectorToString(resp.getBody()), "Second body replacement");
}

TEST(HttpResponse_TestSetBodyOverwriteStringWithVector) {
    HttpResponse resp;
    resp.setBody("String body");

    const char rawData[] = "Vector replacement";
    std::vector<char> vec(rawData, rawData + std::strlen(rawData));
    resp.setBody(vec);

    ASSERT_EQ(resp.getBody().size(), vec.size());
    ASSERT_EQ(vectorToString(resp.getBody()), "Vector replacement");
}

TEST(HttpResponse_TestSetBodyOverwriteVectorWithString) {
    HttpResponse resp;
    const char rawData[] = "Initial vector";
    std::vector<char> vec(rawData, rawData + std::strlen(rawData));
    resp.setBody(vec);

    resp.setBody("String replacement");
    ASSERT_EQ(vectorToString(resp.getBody()), "String replacement");
}

// =============================================================================
// 6. Response Serialization (createResponse)
// =============================================================================

TEST(HttpResponse_TestCreateResponseMinimal) {
    HttpResponse resp;
    resp.setStatusCode(200);

    std::vector<char> raw = resp.createResponse();
    std::string responseStr = vectorToString(raw);

    // Status line followed immediately by empty line separator
    std::string expected = "HTTP/1.0 200 OK\r\n\r\n";
    ASSERT_EQ(responseStr, expected);
    ASSERT_EQ(raw.size(), expected.length());
}

TEST(HttpResponse_TestCreateResponseHeadersOnly) {
    HttpResponse resp;
    resp.setStatusCode(204);
    resp.setHeader("Connection", "close");
    resp.setHeader("Server", "webserv");

    std::vector<char> raw = resp.createResponse();
    std::string responseStr = vectorToString(raw);

    std::string expected = "HTTP/1.0 204 No Content\r\n"
                           "Connection: close\r\n"
                           "Server: webserv\r\n"
                           "\r\n";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestCreateResponseNoHeadersWithBody) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setBody("Direct body without headers");

    std::vector<char> raw = resp.createResponse();
    std::string responseStr = vectorToString(raw);

    std::string expected = "HTTP/1.0 200 OK\r\n\r\nDirect body without headers";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestCreateResponseFull) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Content-Length", "12");
    resp.setHeader("Content-Type", "text/plain");
    resp.setBody("Hello world!");

    std::vector<char> raw = resp.createResponse();
    std::string responseStr = vectorToString(raw);

    std::string expected = "HTTP/1.0 200 OK\r\n"
                           "Content-Length: 12\r\n"
                           "Content-Type: text/plain\r\n"
                           "\r\n"
                           "Hello world!";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestCreateResponseBinaryBodyPreservation) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Content-Type", "application/octet-stream");

    char binaryBytes[] = {'\x00', 'B', 'I', 'N', '\x00', '\xFF', '\x00', '\x42'};
    size_t binSize = sizeof(binaryBytes);
    std::vector<char> binVec(binaryBytes, binaryBytes + binSize);
    resp.setBody(binVec);

    std::vector<char> raw = resp.createResponse();

    // Find the header-body boundary "\r\n\r\n"
    const char delimiter[] = "\r\n\r\n";
    size_t headerEndPos = 0;
    bool foundDelimiter = false;
    for (size_t i = 0; i + 4 <= raw.size(); ++i) {
        if (std::memcmp(&raw[i], delimiter, 4) == 0) {
            headerEndPos = i + 4;
            foundDelimiter = true;
            break;
        }
    }
    ASSERT_TRUE(foundDelimiter);

    // Verify header portion
    std::string headerPart(raw.begin(), raw.begin() + headerEndPos);
    ASSERT_TRUE(headerPart.find("HTTP/1.0 200 OK\r\n") == 0);
    ASSERT_TRUE(headerPart.find("Content-Type: application/octet-stream\r\n") != std::string::npos);

    // Verify binary body portion
    std::vector<char> bodyPart(raw.begin() + headerEndPos, raw.end());
    ASSERT_EQ(bodyPart.size(), binSize);
    ASSERT_EQ(std::memcmp(&bodyPart[0], binaryBytes, binSize), 0);
}

TEST(HttpResponse_TestCreateResponseIdempotency) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setHeader("Content-Type", "text/html");
    resp.setBody("<h3>Idempotent test</h3>");

    std::vector<char> res1 = resp.createResponse();
    std::vector<char> res2 = resp.createResponse();
    std::vector<char> res3 = resp.createResponse();

    ASSERT_EQ(res1, res2);
    ASSERT_EQ(res2, res3);
}

// =============================================================================
// 7. Standard Webserv Scenarios
// =============================================================================

TEST(HttpResponse_TestScenario200OkHtmlServing) {
    HttpResponse resp;
    resp.setStatusCode(200);
    std::string html = "<!DOCTYPE html><html><body><h1>Welcome</h1></body></html>";
    resp.setBody(html);

    std::ostringstream oss;
    oss << html.length();
    resp.setHeader("Content-Length", oss.str());
    resp.setHeader("Content-Type", "text/html");
    resp.setHeader("Connection", "keep-alive");

    std::string responseStr = vectorToString(resp.createResponse());

    std::string expected = "HTTP/1.0 200 OK\r\n"
                           "Connection: keep-alive\r\n"
                           "Content-Length: 57\r\n"
                           "Content-Type: text/html\r\n"
                           "\r\n"
                           "<!DOCTYPE html><html><body><h1>Welcome</h1></body></html>";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestScenario201CreatedUpload) {
    HttpResponse resp;
    resp.setStatusCode(201);
    resp.setHeader("Content-Length", "0");
    resp.setHeader("Location", "/uploads/photo.jpg");

    std::string responseStr = vectorToString(resp.createResponse());

    std::string expected = "HTTP/1.0 201 Created\r\n"
                           "Content-Length: 0\r\n"
                           "Location: /uploads/photo.jpg\r\n"
                           "\r\n";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestScenario204NoContentDelete) {
    HttpResponse resp;
    resp.setStatusCode(204);
    resp.setHeader("Content-Length", "0");

    std::string responseStr = vectorToString(resp.createResponse());

    std::string expected = "HTTP/1.0 204 No Content\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestScenario301MovedPermanently) {
    HttpResponse resp;
    resp.setStatusCode(301);
    resp.setHeader("Location", "/directory/");

    std::string responseStr = vectorToString(resp.createResponse());

    std::string expected = "HTTP/1.0 301 Moved Permanently\r\n"
                           "Location: /directory/\r\n"
                           "\r\n";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestScenario400BadRequest) {
    HttpResponse resp;
    resp.setStatusCode(400);
    std::string body = "<html><body><h1>400 Bad Request</h1></body></html>";
    resp.setBody(body);

    std::ostringstream oss;
    oss << body.length();
    resp.setHeader("Content-Length", oss.str());
    resp.setHeader("Content-Type", "text/html");
    resp.setHeader("Connection", "close");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 400 Bad Request\r\n") == 0);
    ASSERT_TRUE(responseStr.find("Connection: close\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find(body) != std::string::npos);
}

TEST(HttpResponse_TestScenario403Forbidden) {
    HttpResponse resp;
    resp.setStatusCode(403);
    std::string body = "<html><body><h1>403 Forbidden</h1></body></html>";
    resp.setBody(body);
    resp.setHeader("Content-Type", "text/html");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 403 Forbidden\r\n") == 0);
    ASSERT_TRUE(responseStr.find(body) != std::string::npos);
}

TEST(HttpResponse_TestScenario404NotFound) {
    HttpResponse resp;
    resp.setStatusCode(404);
    std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
    resp.setBody(body);
    resp.setHeader("Content-Type", "text/html");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 404 Not Found\r\n") == 0);
    ASSERT_TRUE(responseStr.find(body) != std::string::npos);
}

TEST(HttpResponse_TestScenario405MethodNotAllowed) {
    HttpResponse resp;
    resp.setStatusCode(405);
    resp.setHeader("Allow", "GET, POST");
    resp.setBody("Method Not Allowed");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 405 Method Not Allowed\r\n") == 0);
    ASSERT_TRUE(responseStr.find("Allow: GET, POST\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Method Not Allowed") != std::string::npos);
}

TEST(HttpResponse_TestScenario413PayloadTooLarge) {
    HttpResponse resp;
    resp.setStatusCode(413);
    resp.setHeader("Connection", "close");
    resp.setBody("Payload Too Large");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 413 Payload Too Large\r\n") == 0);
    ASSERT_TRUE(responseStr.find("Connection: close\r\n") != std::string::npos);
}

TEST(HttpResponse_TestScenario500InternalServerError) {
    HttpResponse resp;
    resp.setStatusCode(500);
    resp.setHeader("Connection", "close");
    resp.setBody("Internal Server Error");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 500 Internal Server Error\r\n") == 0);
}

TEST(HttpResponse_TestScenario505HttpVersionNotSupported) {
    HttpResponse resp;
    resp.setStatusCode(505);
    resp.setHeader("Connection", "close");
    resp.setBody("HTTP Version Not Supported");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("HTTP/1.0 505 HTTP Version Not Supported\r\n") == 0);
}

// =============================================================================
// 8. Edge Cases & Robustness
// =============================================================================

TEST(HttpResponse_TestEdgeCaseLargePayload) {
    HttpResponse resp;
    resp.setStatusCode(200);

    // 64 KB payload
    const size_t payloadSize = 65536;
    std::vector<char> largeBody(payloadSize);
    for (size_t i = 0; i < payloadSize; ++i) {
        largeBody[i] = static_cast<char>(i % 256);
    }
    resp.setBody(largeBody);

    std::ostringstream oss;
    oss << payloadSize;
    resp.setHeader("Content-Length", oss.str());

    std::vector<char> raw = resp.createResponse();

    // Verify status line
    std::string headerPrefix = "HTTP/1.0 200 OK\r\nContent-Length: 65536\r\n\r\n";
    ASSERT_TRUE(raw.size() == headerPrefix.length() + payloadSize);

    // Verify body integrity at start, middle, and end
    size_t bodyOffset = headerPrefix.length();
    ASSERT_EQ(raw[bodyOffset], static_cast<char>(0));
    ASSERT_EQ(raw[bodyOffset + 1], static_cast<char>(1));
    ASSERT_EQ(raw[bodyOffset + 32768], static_cast<char>(32768 % 256));
    ASSERT_EQ(raw[bodyOffset + payloadSize - 1], static_cast<char>((payloadSize - 1) % 256));
}

TEST(HttpResponse_TestEdgeCaseBodyWithCRLFsOnly) {
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setBody("\r\n\r\n\r\n");

    std::vector<char> raw = resp.createResponse();
    std::string responseStr = vectorToString(raw);

    // Header separator \r\n\r\n followed by body \r\n\r\n\r\n
    std::string expected = "HTTP/1.0 200 OK\r\n\r\n\r\n\r\n\r\n";
    ASSERT_EQ(responseStr, expected);
}

TEST(HttpResponse_TestEdgeCaseZeroAndNegativeStatusCode) {
    HttpResponse resp1;
    resp1.setStatusCode(0);
    ASSERT_EQ(resp1.getStatusCode(), 0);
    ASSERT_EQ(resp1.getStatusMessage(0), "Unknown Status");
    ASSERT_TRUE(vectorToString(resp1.createResponse()).find("HTTP/1.0 0 Unknown Status\r\n") == 0);

    HttpResponse resp2;
    resp2.setStatusCode(-1);
    ASSERT_EQ(resp2.getStatusCode(), -1);
    ASSERT_EQ(resp2.getStatusMessage(-1), "Unknown Status");
    ASSERT_TRUE(vectorToString(resp2.createResponse()).find("HTTP/1.0 -1 Unknown Status\r\n") == 0);
}

TEST(HttpResponse_TestEdgeCaseMultipleHeaderOverwritesAndMutations) {
    HttpResponse resp;
    resp.setStatusCode(200);

    resp.setHeader("Key", "Value1");
    resp.setHeader("Key", "Value2");
    resp.setHeader("Key", "Value3");
    resp.setHeader("AnotherKey", "AnotherValue");

    resp.setBody("body1");
    resp.setBody("body2");

    std::string responseStr = vectorToString(resp.createResponse());
    ASSERT_TRUE(responseStr.find("Key: Value3\r\n") != std::string::npos);
    ASSERT_TRUE(responseStr.find("Key: Value1") == std::string::npos);
    ASSERT_TRUE(responseStr.find("Key: Value2") == std::string::npos);
    ASSERT_TRUE(responseStr.find("body2") != std::string::npos);
    ASSERT_TRUE(responseStr.find("body1") == std::string::npos);
}

TEST(HttpResponse_TestEdgeCaseSpecialCharactersInBody) {
    HttpResponse resp;
    resp.setStatusCode(200);
    std::string specialStr = "Line 1\tTabbed\r\nLine 2 \"Quoted\" & <Escaped>\nLine 3 \0 Null";
    // Construct string with embedded null
    std::string bodyWithNull(specialStr.c_str(), specialStr.length() + 7);
    resp.setBody(bodyWithNull);

    ASSERT_EQ(resp.getBody().size(), bodyWithNull.length());
    ASSERT_EQ(resp.getBody(), std::vector<char>(bodyWithNull.begin(), bodyWithNull.end()));
}
