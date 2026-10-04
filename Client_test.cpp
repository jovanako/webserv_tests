#include "tiny_test.hpp"
#include "Client.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

namespace {

std::string vectorToString(const std::vector<char>& vec) {
    if (vec.empty()) {
        return std::string();
    }
    return std::string(vec.begin(), vec.end());
}

// Temporary Directory RAII Helper for File Serving/Upload/Delete Tests
class TempDir {
public:
    std::string path;

    TempDir() {
        char tmpl[] = "/tmp/webserv_client_test_XXXXXX";
        char* dir = mkdtemp(tmpl);
        if (dir) {
            path = dir;
        }
    }

    ~TempDir() {
        if (!path.empty()) {
            removeDirRecursive(path);
        }
    }

    void createFile(const std::string& relativePath, const std::string& content) {
        std::string full = path + "/" + relativePath;
        std::ofstream ofs(full.c_str(), std::ios::out | std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(content.data(), content.size());
            ofs.close();
        }
    }

    void createSubDir(const std::string& relativePath) {
        std::string full = path + "/" + relativePath;
        mkdir(full.c_str(), 0777);
    }

    bool fileExists(const std::string& relativePath) const {
        std::string full = path + "/" + relativePath;
        struct stat st;
        return (stat(full.c_str(), &st) == 0);
    }

    std::string readFile(const std::string& relativePath) const {
        std::string full = path + "/" + relativePath;
        std::ifstream ifs(full.c_str(), std::ios::in | std::ios::binary);
        if (!ifs.is_open()) return "";
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

private:
    static void removeDirRecursive(const std::string& dirPath) {
        DIR* dir = opendir(dirPath.c_str());
        if (!dir) return;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            std::string fullPath = dirPath + "/" + name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    removeDirRecursive(fullPath);
                } else {
                    std::remove(fullPath.c_str());
                }
            }
        }
        closedir(dir);
        rmdir(dirPath.c_str());
    }
};

// Connected socketpair RAII Helper for socket-level read/write testing
struct SocketPair {
    int sv[2];
    bool valid;

    SocketPair() : valid(false) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            valid = true;
            fcntl(sv[0], F_SETFL, O_NONBLOCK);
            fcntl(sv[1], F_SETFL, O_NONBLOCK);
        }
    }

    ~SocketPair() {
        if (valid) {
            close(sv[0]);
            close(sv[1]);
        }
    }

    int clientFd() const { return sv[0]; }
    int peerFd() const { return sv[1]; }

    ssize_t sendToClient(const std::string& data) {
        return send(sv[1], data.data(), data.size(), 0);
    }

    std::string readFromClient() {
        char buf[4096];
        std::string result;
        while (true) {
            ssize_t n = recv(sv[1], buf, sizeof(buf), 0);
            if (n > 0) {
                result.append(buf, n);
            } else {
                break;
            }
        }
        return result;
    }
};

} // namespace

// =============================================================================
// 1. Initial State & Orthodox Canonical Form
// =============================================================================

TEST(Client_TestDefaultConstructor) {
    Client client;

    ASSERT_EQ(client.getSocketFd(), -1);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
    ASSERT_EQ(client.getBytesSent(), 0u);
    ASSERT_TRUE(client.getReadBuffer().empty());
    ASSERT_TRUE(client.getWriteBuffer().empty());
    ASSERT_TRUE(client.getVirtualHosts().empty());
    ASSERT_EQ(client.getRequest().getMethod(), "");
    ASSERT_EQ(client.getResponse().getStatusCode(), 0);
}

TEST(Client_TestParameterizedConstructor) {
    Client client(42);

    ASSERT_EQ(client.getSocketFd(), 42);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
    ASSERT_EQ(client.getBytesSent(), 0u);
    ASSERT_TRUE(client.getReadBuffer().empty());
    ASSERT_TRUE(client.getWriteBuffer().empty());
}

TEST(Client_TestCopyConstructorBasic) {
    Client original(10);
    original.setClientState(Client::PROCESSING);
    original.setReadBuffer("GET / HTTP/1.1\r\n\r\n");

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
    ASSERT_EQ(copy.getReadBuffer(), "GET / HTTP/1.1\r\n\r\n");
    ASSERT_EQ(copy.getServer().getPort(), 9090);
    ASSERT_EQ(copy.getServer().getHost(), "127.0.0.1");
    ASSERT_EQ(copy.getVirtualHosts().size(), 1u);
    ASSERT_EQ(copy.getVirtualHosts()[0].getServerNames()[0], "example.com");
}

TEST(Client_TestCopyConstructorIndependence) {
    Client original(10);
    original.setClientState(Client::READING_HEADER);
    original.setReadBuffer("initial buffer");

    Client copy(original);

    // Mutate original - copy must remain unaffected
    original.setClientState(Client::DONE);
    original.setReadBuffer("modified buffer");

    ASSERT_EQ(copy.getClientState(), Client::READING_HEADER);
    ASSERT_EQ(copy.getReadBuffer(), "initial buffer");

    // Mutate copy - original must remain unaffected
    copy.setClientState(Client::WRITING_RESPONSE);
    copy.setReadBuffer("copy buffer");

    ASSERT_EQ(original.getClientState(), Client::DONE);
    ASSERT_EQ(original.getReadBuffer(), "modified buffer");
}

TEST(Client_TestAssignmentOperatorBasic) {
    Client original(25);
    original.setClientState(Client::WRITING_RESPONSE);
    original.setReadBuffer("data");
    std::vector<char> wb;
    wb.push_back('H');
    wb.push_back('i');
    original.setWriteBuffer(wb);

    Client assigned;
    assigned = original;

    ASSERT_EQ(assigned.getSocketFd(), 25);
    ASSERT_EQ(assigned.getClientState(), Client::WRITING_RESPONSE);
    ASSERT_EQ(assigned.getReadBuffer(), "data");
    ASSERT_EQ(assigned.getWriteBuffer().size(), 2u);
    ASSERT_EQ(vectorToString(assigned.getWriteBuffer()), "Hi");
}

TEST(Client_TestAssignmentOperatorIndependence) {
    Client original(5);
    original.setClientState(Client::READING_BODY);
    original.setReadBuffer("body chunk");

    Client assigned;
    assigned = original;

    // Mutate original
    original.setClientState(Client::DONE);
    original.setReadBuffer("changed chunk");

    ASSERT_EQ(assigned.getClientState(), Client::READING_BODY);
    ASSERT_EQ(assigned.getReadBuffer(), "body chunk");
}

TEST(Client_TestAssignmentOperatorSelfAssignment) {
    Client client(7);
    client.setClientState(Client::PROCESSING);
    client.setReadBuffer("self test");

    client = client;

    ASSERT_EQ(client.getSocketFd(), 7);
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(client.getReadBuffer(), "self test");
}

TEST(Client_TestAssignmentOperatorChaining) {
    Client c1(12);
    c1.setClientState(Client::CGI_PIPE_WAIT);
    c1.setReadBuffer("chained");

    Client c2;
    Client c3;
    c3 = c2 = c1;

    ASSERT_EQ(c2.getSocketFd(), 12);
    ASSERT_EQ(c2.getClientState(), Client::CGI_PIPE_WAIT);
    ASSERT_EQ(c2.getReadBuffer(), "chained");

    ASSERT_EQ(c3.getSocketFd(), 12);
    ASSERT_EQ(c3.getClientState(), Client::CGI_PIPE_WAIT);
    ASSERT_EQ(c3.getReadBuffer(), "chained");
}

// =============================================================================
// 2. State Management & Accessors
// =============================================================================

TEST(Client_TestStateTransitions) {
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

TEST(Client_TestServerConfigAccessors) {
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

TEST(Client_TestVirtualHostsAccessors) {
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

TEST(Client_TestBufferAccessors) {
    Client client;
    client.setReadBuffer("raw incoming bytes");
    ASSERT_EQ(client.getReadBuffer(), "raw incoming bytes");

    std::vector<char> wb;
    wb.push_back('A');
    wb.push_back('B');
    wb.push_back('C');
    client.setWriteBuffer(wb);
    ASSERT_EQ(client.getWriteBuffer().size(), 3u);
    ASSERT_EQ(vectorToString(client.getWriteBuffer()), "ABC");
}

TEST(Client_TestResetForNextRequest) {
    Client client(15);
    client.setClientState(Client::WRITING_RESPONSE);

    std::vector<char> wb;
    wb.push_back('X');
    client.setWriteBuffer(wb);
    client.setReadBuffer("pipelined GET /next HTTP/1.1\r\n\r\n");

    client.resetForNextRequest();

    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
    ASSERT_EQ(client.getBytesSent(), 0u);
    ASSERT_TRUE(client.getWriteBuffer().empty());
    ASSERT_EQ(client.getRequest().getMethod(), "");
    ASSERT_EQ(client.getResponse().getStatusCode(), 0);
    ASSERT_EQ(client.getSocketFd(), 15);
    // Unconsumed readBuffer remains for pipelining
    ASSERT_EQ(client.getReadBuffer(), "pipelined GET /next HTTP/1.1\r\n\r\n");
}

// =============================================================================
// 3. Request Header Parsing (parseHeaders)
// =============================================================================

TEST(Client_TestParseHeadersValidGet) {
    Client client;
    client.setReadBuffer("GET /test.html HTTP/1.1\r\nHost: localhost:8080\r\nAccept: text/plain\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getRequest().getMethod(), "GET");
    ASSERT_EQ(client.getRequest().getUri(), "/test.html");
    ASSERT_EQ(client.getRequest().getVersion(), "HTTP/1.1");
    ASSERT_EQ(client.getRequest().getHeaders().find("host")->second, "localhost:8080");
    ASSERT_EQ(client.getRequest().getHeaders().find("accept")->second, "text/plain");
    ASSERT_EQ(client.getRequest().getRequestState(), HttpRequest::PARSE_HEADERS);
}

TEST(Client_TestParseHeadersValidPost) {
    Client client;
    client.setReadBuffer("POST /upload HTTP/1.1\r\nHost: example.com\r\nContent-Length: 42\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getRequest().getMethod(), "POST");
    ASSERT_EQ(client.getRequest().getUri(), "/upload");
    ASSERT_EQ(client.getRequest().getVersion(), "HTTP/1.1");
    ASSERT_EQ(client.getRequest().getContentLength(), 42u);
}

TEST(Client_TestParseHeadersValidDelete) {
    Client client;
    client.setReadBuffer("DELETE /item/123 HTTP/1.1\r\nHost: api.local\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getRequest().getMethod(), "DELETE");
    ASSERT_EQ(client.getRequest().getUri(), "/item/123");
    ASSERT_EQ(client.getRequest().getVersion(), "HTTP/1.1");
}

TEST(Client_TestParseHeadersHttp10) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.0\r\nHost: legacy.com\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getRequest().getVersion(), "HTTP/1.0");
    ASSERT_EQ(client.getRequest().getMethod(), "GET");
}

TEST(Client_TestParseHeadersWhitespaceTrimming) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost:    \t spaces.org \t  \r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getRequest().getHeaders().find("host")->second, "spaces.org");
}

TEST(Client_TestParseHeadersEmptyHeaderValue) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\nX-Empty-Value:\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getRequest().getHeaders().find("x-empty-value")->second, "");
}

TEST(Client_TestParseHeadersMalformedRequestLineFewTokens) {
    Client client;
    client.setReadBuffer("GET /\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getResponse().getStatusCode(), 400);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestParseHeadersMalformedRequestLineExtraTokens) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1 SPURIOUS_TOKEN\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getResponse().getStatusCode(), 400);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestParseHeadersUnsupportedHttpVersion) {
    Client client;
    client.setReadBuffer("GET / HTTP/2.0\r\nHost: localhost\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getResponse().getStatusCode(), 505);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestParseHeadersInvalidHeaderKey) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nBad Header Key: value\r\n\r\n");

    client.parseHeaders();

    ASSERT_EQ(client.getResponse().getStatusCode(), 400);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

// =============================================================================
// 4. Error Response Generation (buildErrorResponse)
// =============================================================================

TEST(Client_TestBuildErrorResponse400BadRequest) {
    Client client;
    client.buildErrorResponse(400);

    ASSERT_EQ(client.getResponse().getStatusCode(), 400);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);

    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("400 Bad Request") != std::string::npos);
    ASSERT_TRUE(body.find("webserv") != std::string::npos);

    std::string respStr = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(respStr.find("Content-Type: text/html\r\n") != std::string::npos);
    ASSERT_TRUE(respStr.find("Content-Length: ") != std::string::npos);
}

TEST(Client_TestBuildErrorResponse403Forbidden) {
    Client client;
    client.buildErrorResponse(403);

    ASSERT_EQ(client.getResponse().getStatusCode(), 403);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("403 Forbidden") != std::string::npos);
}

TEST(Client_TestBuildErrorResponse404NotFound) {
    Client client;
    client.buildErrorResponse(404);

    ASSERT_EQ(client.getResponse().getStatusCode(), 404);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("404 Not Found") != std::string::npos);
}

TEST(Client_TestBuildErrorResponse405MethodNotAllowed) {
    Client client;
    client.buildErrorResponse(405);

    ASSERT_EQ(client.getResponse().getStatusCode(), 405);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("405 Method Not Allowed") != std::string::npos);
}

TEST(Client_TestBuildErrorResponse413PayloadTooLarge) {
    Client client;
    client.buildErrorResponse(413);

    ASSERT_EQ(client.getResponse().getStatusCode(), 413);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("413 Payload Too Large") != std::string::npos);
}

TEST(Client_TestBuildErrorResponse500InternalServerError) {
    Client client;
    client.buildErrorResponse(500);

    ASSERT_EQ(client.getResponse().getStatusCode(), 500);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("500 Internal Server Error") != std::string::npos);
}

TEST(Client_TestBuildErrorResponse505VersionNotSupported) {
    Client client;
    client.buildErrorResponse(505);

    ASSERT_EQ(client.getResponse().getStatusCode(), 505);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("505 HTTP Version Not Supported") != std::string::npos);
}

TEST(Client_TestBuildErrorResponseCustomFile) {
    TempDir tmp;
    tmp.createFile("error404.html", "<h1>Custom 404 Not Found Page</h1>");

    ServerConfig server;
    server.addErrorPage(404, tmp.path + "/error404.html");

    Client client;
    client.setServer(server);
    client.buildErrorResponse(404);

    ASSERT_EQ(client.getResponse().getStatusCode(), 404);
    ASSERT_EQ(vectorToString(client.getResponse().getBody()), "<h1>Custom 404 Not Found Page</h1>");

    std::string respStr = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(respStr.find("Content-Type: text/html\r\n") != std::string::npos);
}

TEST(Client_TestBuildErrorResponseMissingCustomFileFallback) {
    ServerConfig server;
    server.addErrorPage(404, "/non/existent/path/custom404.html");

    Client client;
    client.setServer(server);
    client.buildErrorResponse(404);

    ASSERT_EQ(client.getResponse().getStatusCode(), 404);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("404 Not Found") != std::string::npos);
    ASSERT_TRUE(body.find("webserv") != std::string::npos);
}

// =============================================================================
// 5. Keep-Alive and Response Finalization Logic
// =============================================================================

TEST(Client_TestKeepAliveHttp11Default) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();

    client.buildErrorResponse(200);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: keep-alive\r\n") != std::string::npos);
}

TEST(Client_TestKeepAliveHttp11ExplicitClose) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    client.parseHeaders();

    client.buildErrorResponse(200);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: close\r\n") != std::string::npos);
}

TEST(Client_TestKeepAliveHttp10Default) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.0\r\n\r\n");
    client.parseHeaders();

    client.buildErrorResponse(200);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: close\r\n") != std::string::npos);
}

TEST(Client_TestKeepAliveHttp10ExplicitKeepAlive) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
    client.parseHeaders();

    client.buildErrorResponse(200);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: keep-alive\r\n") != std::string::npos);
}

TEST(Client_TestKeepAliveFatalErrorsForceClose) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();

    // 500 error should force Connection: close even on HTTP/1.1
    client.buildErrorResponse(500);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: close\r\n") != std::string::npos);
}

TEST(Client_TestKeepAliveNonFatal404AllowsKeepAlive) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();

    // 404 error allows keep-alive on HTTP/1.1
    client.buildErrorResponse(404);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: keep-alive\r\n") != std::string::npos);
}

TEST(Client_TestKeepAliveNonFatal405AllowsKeepAlive) {
    Client client;
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();

    // 405 error allows keep-alive on HTTP/1.1
    client.buildErrorResponse(405);

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Connection: keep-alive\r\n") != std::string::npos);
}

// =============================================================================
// 6. Route Matching & Allowed Methods (handleProcessing)
// =============================================================================

TEST(Client_TestRoutingExactMatch) {
    ServerConfig server;
    LocationConfig locRoot;
    locRoot.setPath("/");
    locRoot.addAllowedMethod("GET");
    server.addLocation(locRoot);

    LocationConfig locApi;
    locApi.setPath("/api");
    locApi.addAllowedMethod("POST");
    server.addLocation(locApi);

    Client client;
    client.setServer(server);

    // Matches /api exactly and allows POST
    client.setReadBuffer("POST /api HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    // Not rejected with 405 or 404
    ASSERT_NE(client.getResponse().getStatusCode(), 404);
    ASSERT_NE(client.getResponse().getStatusCode(), 405);
}

TEST(Client_TestRoutingLongestPrefix) {
    ServerConfig server;
    LocationConfig loc1;
    loc1.setPath("/files");
    loc1.addAllowedMethod("GET");
    server.addLocation(loc1);

    LocationConfig loc2;
    loc2.setPath("/files/docs");
    loc2.addAllowedMethod("DELETE");
    server.addLocation(loc2);

    Client client;
    client.setServer(server);

    // Request for /files/docs/readme.txt matches /files/docs which only allows DELETE
    // A GET should therefore result in 405 Method Not Allowed!
    client.setReadBuffer("GET /files/docs/readme.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 405);
}

TEST(Client_TestRoutingNoLocation404) {
    ServerConfig server; // No locations defined

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /page.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 404);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestRoutingMethodNotAllowed405) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("DELETE /anything HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 405);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestRoutingMultipleAllowedMethods) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/crud");
    loc.addAllowedMethod("GET");
    loc.addAllowedMethod("POST");
    loc.addAllowedMethod("DELETE");
    server.addLocation(loc);

    Client client;
    client.setServer(server);

    client.setReadBuffer("GET /crud HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_NE(client.getResponse().getStatusCode(), 405);

    client.resetForNextRequest();
    client.setReadBuffer("POST /crud HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_NE(client.getResponse().getStatusCode(), 405);

    client.resetForNextRequest();
    client.setReadBuffer("DELETE /crud HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_NE(client.getResponse().getStatusCode(), 405);
}

TEST(Client_TestRoutingRedirect301) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/old-page");
    loc.addAllowedMethod("GET");
    loc.setRedirect(301, "/new-page");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /old-page HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 301);
    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Location: /new-page\r\n") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestRoutingRedirect302) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/temp-source");
    loc.addAllowedMethod("GET");
    loc.setRedirect(302, "https://example.com/target");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /temp-source HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 302);
    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Location: https://example.com/target\r\n") != std::string::npos);
}

TEST(Client_TestRoutingRedirectInvalidCode500) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/bad-redirect");
    loc.addAllowedMethod("GET");
    loc.setRedirect(500, "/somewhere");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /bad-redirect HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 500);
}

// =============================================================================
// 7. Static File Serving (GET in handleProcessing)
// =============================================================================

TEST(Client_TestHandleGetExistingFile) {
    TempDir tmp;
    tmp.createFile("hello.txt", "Hello from webserv static server!");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /hello.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    ASSERT_EQ(vectorToString(client.getResponse().getBody()), "Hello from webserv static server!");

    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Content-Type: text/plain\r\n") != std::string::npos);
    ASSERT_TRUE(resp.find("Content-Length: 33\r\n") != std::string::npos);
}

TEST(Client_TestHandleGetMimeTypes) {
    TempDir tmp;
    tmp.createFile("style.css", "body { color: red; }");
    tmp.createFile("script.js", "console.log('hi');");
    tmp.createFile("data.json", "{\"key\": 42}");
    tmp.createFile("pic.png", "\x89PNG\r\n\x1a\n");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    Client client;
    client.setServer(server);

    // CSS
    client.setReadBuffer("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    ASSERT_TRUE(vectorToString(client.getResponse().createResponse()).find("Content-Type: text/css\r\n") != std::string::npos);

    // JS
    client.resetForNextRequest();
    client.setReadBuffer("GET /script.js HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    ASSERT_TRUE(vectorToString(client.getResponse().createResponse()).find("Content-Type: application/javascript\r\n") != std::string::npos);

    // JSON
    client.resetForNextRequest();
    client.setReadBuffer("GET /data.json HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    ASSERT_TRUE(vectorToString(client.getResponse().createResponse()).find("Content-Type: application/json\r\n") != std::string::npos);

    // PNG
    client.resetForNextRequest();
    client.setReadBuffer("GET /pic.png HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();
    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    ASSERT_TRUE(vectorToString(client.getResponse().createResponse()).find("Content-Type: image/png\r\n") != std::string::npos);
}

TEST(Client_TestHandleGetNonExistentFile404) {
    TempDir tmp;

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /missing_file.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 404);
}

TEST(Client_TestHandleGetDirectoryWithIndex) {
    TempDir tmp;
    tmp.createFile("index.html", "<html>Welcome Home</html>");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.addIndex("index.html");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    ASSERT_EQ(vectorToString(client.getResponse().getBody()), "<html>Welcome Home</html>");
}

TEST(Client_TestHandleGetDirectoryNoIndexAutoindexOn) {
    TempDir tmp;
    tmp.createFile("document.txt", "doc content");
    tmp.createFile("photo.jpg", "image bytes");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.setAutoindex(true);
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 200);
    std::string body = vectorToString(client.getResponse().getBody());
    ASSERT_TRUE(body.find("Index of /") != std::string::npos);
    ASSERT_TRUE(body.find("document.txt") != std::string::npos);
    ASSERT_TRUE(body.find("photo.jpg") != std::string::npos);
}

TEST(Client_TestHandleGetDirectoryNoIndexAutoindexOff) {
    TempDir tmp;
    tmp.createFile("hidden.txt", "secret");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.setAutoindex(false);
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 403);
}

TEST(Client_TestHandleGetDirectoryMissingTrailingSlash) {
    TempDir tmp;
    tmp.createSubDir("subfolder");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("GET /subfolder HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();
    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 301);
    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Location: /subfolder/\r\n") != std::string::npos);
}

// =============================================================================
// 8. File Upload Handling (POST in handleProcessing)
// =============================================================================

TEST(Client_TestHandlePostValidUpload) {
    TempDir tmp;
    std::string uploadDir = tmp.path + "/uploads";
    mkdir(uploadDir.c_str(), 0777);

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/upload");
    loc.setUploadStore(uploadDir);
    loc.addAllowedMethod("POST");
    server.addLocation(loc);

    Client client;
    client.setServer(server);

    client.setReadBuffer("POST /upload/uploaded_file.txt HTTP/1.1\r\nHost: localhost\r\nContent-Length: 18\r\n\r\n");
    client.parseHeaders();
    const char uploadBody[] = "Uploaded File Data";
    client.getRequest().appendBody(uploadBody, 18);

    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 201);
    std::string resp = vectorToString(client.getResponse().createResponse());
    ASSERT_TRUE(resp.find("Location: /upload/uploaded_file.txt\r\n") != std::string::npos);

    // Verify file exists on disk with correct contents
    ASSERT_TRUE(tmp.fileExists("uploads/uploaded_file.txt"));
    ASSERT_EQ(tmp.readFile("uploads/uploaded_file.txt"), "Uploaded File Data");
}

TEST(Client_TestHandlePostBinaryUpload) {
    TempDir tmp;
    std::string uploadDir = tmp.path + "/uploads_bin";
    mkdir(uploadDir.c_str(), 0777);

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/upload");
    loc.setUploadStore(uploadDir);
    loc.addAllowedMethod("POST");
    server.addLocation(loc);

    Client client;
    client.setServer(server);

    client.setReadBuffer("POST /upload/binary.bin HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\n");
    client.parseHeaders();
    const char binData[] = {'a', '\0', 'b', '\0', 'c', '\n'};
    client.getRequest().appendBody(binData, 6);

    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 201);
    ASSERT_TRUE(tmp.fileExists("uploads_bin/binary.bin"));
    std::string readBack = tmp.readFile("uploads_bin/binary.bin");
    ASSERT_EQ(readBack.size(), 6u);
    ASSERT_EQ(std::memcmp(readBack.data(), binData, 6), 0);
}

TEST(Client_TestHandlePostNoUploadStore403) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/post_only");
    loc.addAllowedMethod("POST");
    // No upload store configured
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("POST /post_only/test.txt HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n");
    client.parseHeaders();
    const char body[] = "data";
    client.getRequest().appendBody(body, 4);

    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 403);
}

// =============================================================================
// 9. File Deletion Handling (DELETE in handleProcessing)
// =============================================================================

TEST(Client_TestHandleDeleteExistingFile) {
    TempDir tmp;
    tmp.createFile("file_to_delete.txt", "delete me");
    ASSERT_TRUE(tmp.fileExists("file_to_delete.txt"));

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("DELETE");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("DELETE /file_to_delete.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();

    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 204);
    // File must have been unlinked from disk
    ASSERT_FALSE(tmp.fileExists("file_to_delete.txt"));
}

TEST(Client_TestHandleDeleteNonExistentFile403) {
    TempDir tmp;

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("DELETE");
    server.addLocation(loc);

    Client client;
    client.setServer(server);
    client.setReadBuffer("DELETE /ghost_file.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.parseHeaders();

    client.handleProcessing();

    ASSERT_EQ(client.getResponse().getStatusCode(), 403);
}

// =============================================================================
// 10. Virtual Host Resolution & Host Header Handling
// =============================================================================

TEST(Client_TestVirtualHostMatching) {
    SocketPair sp;

    ServerConfig defaultServer;
    defaultServer.setPort(8080);
    defaultServer.addServerName("default.com");
    defaultServer.setHost("0.0.0.0");

    ServerConfig virtualServer;
    virtualServer.setPort(8080);
    virtualServer.addServerName("vhost.com");
    virtualServer.setHost("0.0.0.0");

    std::vector<ServerConfig> allServers;
    allServers.push_back(defaultServer);
    allServers.push_back(virtualServer);

    Client client(sp.clientFd());
    client.setServer(defaultServer);
    client.setVirtualHosts(allServers);

    // Send HTTP/1.1 request targeted at vhost.com
    sp.sendToClient("GET / HTTP/1.1\r\nHost: vhost.com\r\n\r\n");
    client.handleRead();

    // Verify virtual host switch occurred
    ASSERT_EQ(client.getServer().getServerNames()[0], "vhost.com");
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
}

TEST(Client_TestVirtualHostWithPortInHostHeader) {
    SocketPair sp;

    ServerConfig defaultServer;
    defaultServer.setPort(8080);
    defaultServer.addServerName("default.com");

    ServerConfig virtualServer;
    virtualServer.setPort(8080);
    virtualServer.addServerName("domain.org");

    std::vector<ServerConfig> allServers;
    allServers.push_back(defaultServer);
    allServers.push_back(virtualServer);

    Client client(sp.clientFd());
    client.setServer(defaultServer);
    client.setVirtualHosts(allServers);

    // Host header contains port: "domain.org:8080"
    sp.sendToClient("GET / HTTP/1.1\r\nHost: domain.org:8080\r\n\r\n");
    client.handleRead();

    // Port is stripped to match domain.org
    ASSERT_EQ(client.getServer().getServerNames()[0], "domain.org");
}

TEST(Client_TestVirtualHostNoMatchFallsBackToDefault) {
    SocketPair sp;

    ServerConfig defaultServer;
    defaultServer.setPort(8080);
    defaultServer.addServerName("default.com");

    ServerConfig virtualServer;
    virtualServer.setPort(8080);
    virtualServer.addServerName("domain.org");

    std::vector<ServerConfig> allServers;
    allServers.push_back(defaultServer);
    allServers.push_back(virtualServer);

    Client client(sp.clientFd());
    client.setServer(defaultServer);
    client.setVirtualHosts(allServers);

    sp.sendToClient("GET / HTTP/1.1\r\nHost: unknown.net\r\n\r\n");
    client.handleRead();

    ASSERT_EQ(client.getServer().getServerNames()[0], "default.com");
}

TEST(Client_TestHttp11MissingHostHeader400) {
    SocketPair sp;
    Client client(sp.clientFd());

    // HTTP/1.1 request without Host header is invalid
    sp.sendToClient("GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n");
    client.handleRead();

    ASSERT_EQ(client.getResponse().getStatusCode(), 400);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

// =============================================================================
// 11. Client Max Body Size Limits
// =============================================================================

TEST(Client_TestMaxBodySizeHeaderExceeded413) {
    SocketPair sp;
    ServerConfig server;
    server.setClientMaxBodySize(10); // 10 bytes limit

    Client client(sp.clientFd());
    client.setServer(server);

    // Request announces 50 bytes body
    sp.sendToClient("POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 50\r\n\r\n");
    client.handleRead();

    ASSERT_EQ(client.getResponse().getStatusCode(), 413);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

TEST(Client_TestMaxBodySizeBodyExceeded413) {
    SocketPair sp;
    ServerConfig server;
    server.setClientMaxBodySize(5); // 5 bytes limit

    Client client(sp.clientFd());
    client.setServer(server);

    // Request announces 4 bytes, but body read will exceed 5 bytes
    sp.sendToClient("POST /data HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::READING_BODY);

    // Simulate extra bytes in read buffer exceeding limit
    client.setReadBuffer("1234567890");
    client.handleRead();

    ASSERT_EQ(client.getResponse().getStatusCode(), 413);
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
}

// =============================================================================
// 12. Socket Communication via socketpair (handleRead & handleWrite)
// =============================================================================

TEST(Client_TestSocketReadCompleteGetRequest) {
    SocketPair sp;
    Client client(sp.clientFd());

    sp.sendToClient("GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.handleRead();

    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(client.getRequest().getMethod(), "GET");
    ASSERT_EQ(client.getRequest().getUri(), "/hello");
}

TEST(Client_TestSocketReadSplitHeaders) {
    SocketPair sp;
    Client client(sp.clientFd());

    // Send first part of headers
    sp.sendToClient("GET /split");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);

    // Send second part
    sp.sendToClient(" HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.handleRead();

    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(client.getRequest().getUri(), "/split");
}

TEST(Client_TestSocketReadPostWithBody) {
    SocketPair sp;
    Client client(sp.clientFd());

    // Send headers with Content-Length: 11
    sp.sendToClient("POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: 11\r\n\r\n");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::READING_BODY);

    // Send the 11-byte body
    sp.sendToClient("hello world");
    client.handleRead();

    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(vectorToString(client.getRequest().getBody()), "hello world");
}

TEST(Client_TestSocketReadClientDisconnectEOF) {
    SocketPair sp;
    Client client(sp.clientFd());

    // Peer closes connection
    close(sp.sv[1]);
    sp.valid = false; // avoid double close in destructor
    close(sp.sv[0]);

    // Client reads EOF
    SocketPair sp2;
    Client client2(sp2.clientFd());
    close(sp2.sv[1]); // Close peer socket immediately

    client2.handleRead();
    ASSERT_EQ(client2.getClientState(), Client::DONE);
}

TEST(Client_TestSocketWriteResponseComplete) {
    SocketPair sp;
    Client client(sp.clientFd());

    client.buildErrorResponse(404);
    // Explicitly set Connection: close
    client.getResponse().setHeader("Connection", "close");

    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);

    client.handleWrite();

    std::string sent = sp.readFromClient();
    ASSERT_TRUE(sent.find("HTTP/1.0 404 Not Found\r\n") != std::string::npos);
    ASSERT_TRUE(sent.find("404 Not Found") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::DONE);
}

TEST(Client_TestSocketWriteKeepAliveReset) {
    SocketPair sp;
    Client client(sp.clientFd());

    sp.sendToClient("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);

    client.buildErrorResponse(200);

    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);

    client.handleWrite();

    std::string sent = sp.readFromClient();
    ASSERT_TRUE(sent.find("HTTP/1.0 200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(sent.find("Connection: keep-alive\r\n") != std::string::npos);

    // Keep-alive should reset client back to READING_HEADER
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
    ASSERT_EQ(client.getBytesSent(), 0u);
}

TEST(Client_TestSocketPipelinedRequests) {
    SocketPair sp;
    Client client(sp.clientFd());

    // Two pipelined GET requests
    std::string pipelined = 
        "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n";

    sp.sendToClient(pipelined);

    // First request read
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(client.getRequest().getUri(), "/first");

    // Complete first request response
    client.buildErrorResponse(200);
    client.handleWrite();

    // After write finishes, keep-alive resets and immediately parses the second request from readBuffer!
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    ASSERT_EQ(client.getRequest().getUri(), "/second");
}
