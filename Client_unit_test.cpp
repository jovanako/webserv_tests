#include "tiny_test.hpp"
#include "Client.hpp"
#include "ServerConfig.hpp"
#include <vector>
#include <string>

// =============================================================================
// 1. Initial State & Constructors
// =============================================================================

TEST(ClientUnit_TestDefaultConstructor) {
    Client client;

    ASSERT_EQ(client.getSocketFd(), -1);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
    ASSERT_TRUE(client.getVirtualHosts().empty());
}

TEST(ClientUnit_TestParameterizedConstructor) {
    Client client(42);

    ASSERT_EQ(client.getSocketFd(), 42);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
    ASSERT_TRUE(client.getVirtualHosts().empty());
}

TEST(ClientUnit_TestCopyConstructorBasic) {
    Client original(10);
    original.setClientState(Client::PROCESSING);

    ServerConfig server;
    server.setPort(9090);
    server.setHost("127.0.0.1");
    original.setServer(server);

    std::vector<ServerConfig> vhosts;
    ServerConfig vhost1;
    vhost1.setPort(9090);
    vhost1.addServerName("example.com");
    vhosts.push_back(vhost1);
    original.setVirtualHosts(vhosts);

    Client copy(original);

    ASSERT_EQ(copy.getSocketFd(), 10);
    ASSERT_EQ(copy.getClientState(), Client::PROCESSING);
    ASSERT_EQ(copy.getServer().getPort(), 9090);
    ASSERT_EQ(copy.getServer().getHost(), "127.0.0.1");
    ASSERT_EQ(copy.getVirtualHosts().size(), 1u);
    ASSERT_EQ(copy.getVirtualHosts()[0].getServerNames()[0], "example.com");
}

TEST(ClientUnit_TestCopyConstructorIndependence) {
    Client original(10);
    original.setClientState(Client::READING_HEADER);

    ServerConfig s1;
    s1.setPort(8080);
    original.setServer(s1);

    Client copy(original);

    // Mutate original - copy must remain unaffected
    original.setClientState(Client::DONE);
    ServerConfig s2;
    s2.setPort(9000);
    original.setServer(s2);

    ASSERT_EQ(copy.getClientState(), Client::READING_HEADER);
    ASSERT_EQ(copy.getServer().getPort(), 8080);

    // Mutate copy - original must remain unaffected
    copy.setClientState(Client::WRITING_RESPONSE);
    ASSERT_EQ(original.getClientState(), Client::DONE);
    ASSERT_EQ(original.getServer().getPort(), 9000);
}

TEST(ClientUnit_TestAssignmentOperatorBasic) {
    Client original(25);
    original.setClientState(Client::WRITING_RESPONSE);

    ServerConfig sc;
    sc.setPort(7070);
    sc.setHost("10.0.0.1");
    original.setServer(sc);

    Client assigned;
    assigned = original;

    ASSERT_EQ(assigned.getSocketFd(), 25);
    ASSERT_EQ(assigned.getClientState(), Client::WRITING_RESPONSE);
    ASSERT_EQ(assigned.getServer().getPort(), 7070);
    ASSERT_EQ(assigned.getServer().getHost(), "10.0.0.1");
}

TEST(ClientUnit_TestAssignmentOperatorIndependence) {
    Client original(5);
    original.setClientState(Client::READING_BODY);

    Client assigned;
    assigned = original;

    // Mutate original
    original.setClientState(Client::DONE);

    ASSERT_EQ(assigned.getClientState(), Client::READING_BODY);
}

TEST(ClientUnit_TestAssignmentOperatorSelfAssignment) {
    Client client(7);
    client.setClientState(Client::PROCESSING);

    ServerConfig sc;
    sc.setPort(8088);
    client.setServer(sc);

    client = client;

    ASSERT_EQ(client.getSocketFd(), 7);
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(client.getServer().getPort(), 8088);
}

TEST(ClientUnit_TestAssignmentOperatorChaining) {
    Client c1(12);
    c1.setClientState(Client::CGI_PIPE_WAIT);

    Client c2;
    Client c3;
    c3 = c2 = c1;

    ASSERT_EQ(c2.getSocketFd(), 12);
    ASSERT_EQ(c2.getClientState(), Client::CGI_PIPE_WAIT);

    ASSERT_EQ(c3.getSocketFd(), 12);
    ASSERT_EQ(c3.getClientState(), Client::CGI_PIPE_WAIT);
}

// =============================================================================
// 2. State Management & Configuration Accessors
// =============================================================================

TEST(ClientUnit_TestStateTransitions) {
    Client client;

    client.setClientState(Client::READING_HEADER);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);

    client.setClientState(Client::READING_BODY);
    ASSERT_EQ(client.getClientState(), Client::READING_BODY);

    client.setClientState(Client::PROCESSING);
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);

    client.setClientState(Client::WRITING_RESPONSE);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);

    client.setClientState(Client::CGI_PIPE_WAIT);
    ASSERT_EQ(client.getClientState(), Client::CGI_PIPE_WAIT);

    client.setClientState(Client::DONE);
    ASSERT_EQ(client.getClientState(), Client::DONE);
}

TEST(ClientUnit_TestServerConfigAccessors) {
    Client client;
    ServerConfig sc;
    sc.setHost("192.168.1.1");
    sc.setPort(4242);
    sc.setClientMaxBodySize(2048);

    client.setServer(sc);

    ASSERT_EQ(client.getServer().getHost(), "192.168.1.1");
    ASSERT_EQ(client.getServer().getPort(), 4242);
    ASSERT_EQ(client.getServer().getClientMaxBodySize(), 2048u);
}

TEST(ClientUnit_TestVirtualHostsAccessors) {
    Client client;
    std::vector<ServerConfig> vhosts;

    ServerConfig host1;
    host1.setPort(8080);
    host1.addServerName("site1.com");
    vhosts.push_back(host1);

    ServerConfig host2;
    host2.setPort(8080);
    host2.addServerName("site2.org");
    vhosts.push_back(host2);

    client.setVirtualHosts(vhosts);

    ASSERT_EQ(client.getVirtualHosts().size(), 2u);
    ASSERT_EQ(client.getVirtualHosts()[0].getServerNames()[0], "site1.com");
    ASSERT_EQ(client.getVirtualHosts()[1].getServerNames()[0], "site2.org");
}

TEST(ClientUnit_TestClientIpAndLifecycleHelpers) {
    Client client(15);
    ASSERT_EQ(client.getClientIp(), "");
    ASSERT_EQ(client.getCgiPipeFd(), -1);
    ASSERT_FALSE(client.hasCgiTimedOut());

    // Calling CGI wait/timeout/cleanup when not in CGI_PIPE_WAIT is a safe no-op
    client.handleCgiPipeWait();
    client.handleCgiTimeout();
    client.cleanupCgi();
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);

    client.setClientIp("10.42.0.99");
    ASSERT_EQ(client.getClientIp(), "10.42.0.99");

    client.setupErrorResponse(404);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);

    client.handleDone();
    ASSERT_EQ(client.getClientIp(), "10.42.0.99");
}

// =============================================================================
// 3. RequestParser Unit Tests
// =============================================================================

TEST(ClientUnit_TestRequestParserCanonicalFormAndBufferOps) {
    RequestParser parser;
    ASSERT_EQ(parser.getReadBuffer(), "");

    const char raw[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    parser.appendData(raw, sizeof(raw) - 1);
    ASSERT_EQ(parser.getReadBuffer(), std::string(raw, sizeof(raw) - 1));

    size_t headerEnd = 0;
    ASSERT_TRUE(parser.hasHeaderEnd(headerEnd));
    ASSERT_EQ(headerEnd, 31u);

    // Copy constructor & const getRequest()
    RequestParser copyParser(parser);
    const RequestParser& constRef = copyParser;
    ASSERT_EQ(constRef.getReadBuffer(), parser.getReadBuffer());
    ASSERT_EQ(constRef.getRequest().getRequestState(), HttpRequest::PARSE_REQUEST_LINE);

    // Assignment operator & self-assignment
    RequestParser assigned;
    assigned = parser;
    assigned = assigned;
    ASSERT_EQ(assigned.getReadBuffer(), parser.getReadBuffer());

    // eraseBuffer and reset
    assigned.eraseBuffer(0, 4);
    ASSERT_EQ(assigned.getReadBuffer(), "/ HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assigned.getRequest().setMethod("POST");
    assigned.reset();
    ASSERT_EQ(assigned.getRequest().getMethod(), "");
}

TEST(ClientUnit_TestRequestParserHeaderEdgeCases) {
    // 1. Extra token on request line -> 400 Bad Request
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string req = "GET /index.html HTTP/1.1 EXTRA\r\nHost: localhost\r\n\r\n";
        parser.appendData(req.c_str(), req.size());
        ASSERT_FALSE(parser.parseHeaders(resp));
        ASSERT_EQ(resp.getStatusCode(), 400);
    }

    // 2. Unsupported method -> 501 Not Implemented
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string req = "PUT /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
        parser.appendData(req.c_str(), req.size());
        ASSERT_FALSE(parser.parseHeaders(resp));
        ASSERT_EQ(resp.getStatusCode(), 501);
    }

    // 3. Path traversal URI -> 403 Forbidden
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string req = "GET /../etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n";
        parser.appendData(req.c_str(), req.size());
        ASSERT_FALSE(parser.parseHeaders(resp));
        ASSERT_EQ(resp.getStatusCode(), 403);
    }

    // 4. Header line without colon (ignored) and whitespace-only header value
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string req =
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "MalformedLineWithoutColon\r\n"
            "X-Blank:    \t   \r\n\r\n";
        parser.appendData(req.c_str(), req.size());
        ASSERT_TRUE(parser.parseHeaders(resp));
        ASSERT_EQ(parser.getRequest().getHeaders().find("x-blank")->second, "");
    }

    // 5. Invalid header key or duplicate Host -> 400 Bad Request
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string req =
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Host: duplicate.com\r\n\r\n";
        parser.appendData(req.c_str(), req.size());
        ASSERT_FALSE(parser.parseHeaders(resp));
        ASSERT_EQ(resp.getStatusCode(), 400);
    }
}

TEST(ClientUnit_TestRequestParserChunkedBody) {
    // 1. Complete multi-chunk body with chunk extension
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string chunks =
            "5;ext=val\r\n"
            "Hello\r\n"
            "7\r\n"
            ", World\r\n"
            "0\r\n\r\n";
        parser.appendData(chunks.c_str(), chunks.size());
        ASSERT_TRUE(parser.parseChunkedBody(resp));
        ASSERT_EQ(parser.getRequest().getContentLength(), 12u);
        ASSERT_EQ(parser.getRequest().getBody().getSize(), 12u);
        ASSERT_EQ(parser.getReadBuffer(), "");
    }

    // 2. Incremental / fragmented chunked body
    {
        RequestParser parser;
        HttpResponse resp;

        // Incomplete size line (no \r\n)
        parser.appendData("4", 1);
        ASSERT_FALSE(parser.parseChunkedBody(resp));
        ASSERT_EQ(resp.getStatusCode(), 0);

        // Size line complete, but chunk data incomplete
        parser.appendData("\r\nTe", 4);
        ASSERT_FALSE(parser.parseChunkedBody(resp));
        ASSERT_EQ(resp.getStatusCode(), 0);

        // Complete first chunk, followed by 0\r\n without final \r\n
        parser.appendData("st\r\n0\r\n", 7);
        ASSERT_FALSE(parser.parseChunkedBody(resp));
        ASSERT_EQ(resp.getStatusCode(), 0);

        // Final \r\n arrives
        parser.appendData("\r\n", 2);
        ASSERT_TRUE(parser.parseChunkedBody(resp));
        ASSERT_EQ(parser.getRequest().getContentLength(), 4u);
    }

    // 3. Invalid hex chunk size -> 400 Bad Request
    {
        RequestParser parser;
        HttpResponse resp;
        const std::string badChunk = "ZZZ\r\nhello\r\n0\r\n\r\n";
        parser.appendData(badChunk.c_str(), badChunk.size());
        ASSERT_FALSE(parser.parseChunkedBody(resp));
        ASSERT_EQ(resp.getStatusCode(), 400);
    }
}

TEST(ClientUnit_TestRequestParserFixedLengthBody) {
    ServerConfig server;
    server.setClientMaxBodySize(10);

    // 1. Partial body waits for more bytes
    {
        RequestParser parser;
        HttpResponse resp;
        parser.getRequest().setContentLength(8);
        parser.appendData("1234", 4);
        ASSERT_FALSE(parser.processFixedLengthBody(server, resp));
        ASSERT_EQ(resp.getStatusCode(), 0);

        parser.appendData("5678", 4);
        ASSERT_TRUE(parser.processFixedLengthBody(server, resp));
        ASSERT_EQ(parser.getRequest().getBody().getSize(), 8u);
        ASSERT_EQ(parser.getReadBuffer(), "");
    }

    // 2. Buffer exceeds client_max_body_size -> 413 Payload Too Large
    {
        RequestParser parser;
        HttpResponse resp;
        parser.getRequest().setContentLength(15);
        parser.appendData("0123456789ABCDEF", 16);
        ASSERT_FALSE(parser.processFixedLengthBody(server, resp));
        ASSERT_EQ(resp.getStatusCode(), 413);
    }
}

// =============================================================================
// 4. RequestHandler & ResponseBuilder Unit Tests
// =============================================================================

TEST(ClientUnit_TestRequestHandlerMimeTypesAndCanonicalForm) {
    RequestHandler h1;
    RequestHandler h2(h1);
    h1 = h2;

    ASSERT_EQ(RequestHandler::getMimeType("/index.html"), "text/html");
    ASSERT_EQ(RequestHandler::getMimeType("/page.htm"), "text/html");
    ASSERT_EQ(RequestHandler::getMimeType("/style.css"), "text/css");
    ASSERT_EQ(RequestHandler::getMimeType("/app.js"), "application/javascript");
    ASSERT_EQ(RequestHandler::getMimeType("/data.json"), "application/json");
    ASSERT_EQ(RequestHandler::getMimeType("/image.png"), "image/png");
    ASSERT_EQ(RequestHandler::getMimeType("/photo.jpg"), "image/jpeg");
    ASSERT_EQ(RequestHandler::getMimeType("/photo.jpeg"), "image/jpeg");
    ASSERT_EQ(RequestHandler::getMimeType("/anim.gif"), "image/gif");
    ASSERT_EQ(RequestHandler::getMimeType("/favicon.ico"), "image/x-icon");
    ASSERT_EQ(RequestHandler::getMimeType("/readme.txt"), "text/plain");
    ASSERT_EQ(RequestHandler::getMimeType("/manual.pdf"), "application/pdf");

    // Case-insensitivity and fallback
    ASSERT_EQ(RequestHandler::getMimeType("/PHOTO.JPG"), "image/jpeg");
    ASSERT_EQ(RequestHandler::getMimeType("/DOC.PDF"), "application/pdf");
    ASSERT_EQ(RequestHandler::getMimeType("/archive.tar.gz"), "application/octet-stream");
    ASSERT_EQ(RequestHandler::getMimeType("/dir.with.dot/Makefile"), "application/octet-stream");
    ASSERT_EQ(RequestHandler::getMimeType("/no_extension"), "application/octet-stream");
}

TEST(ClientUnit_TestResponseBuilderCanonicalFormAndKeepAliveFallback) {
    ResponseBuilder rb1;
    ResponseBuilder rb2(rb1);
    rb1 = rb2;

    // Request with unset HTTP version defaults to Connection: close
    HttpRequest req;
    HttpResponse resp;
    resp.setStatusCode(200);
    resp.setBody("ok");
    rb1.buildResponse(resp, req);
    ASSERT_EQ(resp.getHeader("Connection"), "close");
    ASSERT_EQ(resp.getHeader("Content-Length"), "2");
}

