#include "tiny_test.hpp"
#include "Client.hpp"
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

// Temporary Directory RAII Helper for File Serving/Upload/Delete Tests
class TempDir {
public:
    std::string path;

    TempDir() {
        char tmpl[] = "/tmp/webserv_client_integ_XXXXXX";
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

// Connected socketpair RAII Helper for socket-level black-box testing
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

// Helper to run a standard read -> processing -> write cycle on a Client
std::string runTransaction(Client& client, SocketPair& sp, const std::string& requestData) {
    sp.sendToClient(requestData);
    client.handleRead();
    if (client.getClientState() == Client::PROCESSING) {
        client.handleProcessing();
    }
    if (client.getClientState() == Client::WRITING_RESPONSE) {
        client.handleWrite();
    }
    return sp.readFromClient();
}

} // namespace

// =============================================================================
// 1. Static File Serving & MIME Types (GET)
// =============================================================================

TEST(ClientIntegration_TestGetExistingFile) {
    TempDir tmp;
    tmp.createFile("index.html", "<h1>Hello from webserv!</h1>");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Content-Type: text/html\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Content-Length: 28\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("<h1>Hello from webserv!</h1>") != std::string::npos);
}

TEST(ClientIntegration_TestGetMimeTypes) {
    TempDir tmp;
    tmp.createFile("app.css", "body { margin: 0; }");
    tmp.createFile("app.js", "let x = 10;");
    tmp.createFile("data.json", "{\"status\": \"ok\"}");
    tmp.createFile("logo.png", "\x89PNG\r\n\x1a\n");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    // CSS
    std::string resCss = runTransaction(client, sp, "GET /app.css HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_TRUE(resCss.find("200 OK") != std::string::npos);
    ASSERT_TRUE(resCss.find("Content-Type: text/css\r\n") != std::string::npos);

    // JS
    std::string resJs = runTransaction(client, sp, "GET /app.js HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_TRUE(resJs.find("200 OK") != std::string::npos);
    ASSERT_TRUE(resJs.find("Content-Type: application/javascript\r\n") != std::string::npos);

    // JSON
    std::string resJson = runTransaction(client, sp, "GET /data.json HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_TRUE(resJson.find("200 OK") != std::string::npos);
    ASSERT_TRUE(resJson.find("Content-Type: application/json\r\n") != std::string::npos);

    // PNG
    std::string resPng = runTransaction(client, sp, "GET /logo.png HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_TRUE(resPng.find("200 OK") != std::string::npos);
    ASSERT_TRUE(resPng.find("Content-Type: image/png\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestGetNonExistentFile404) {
    TempDir tmp;

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /nonexistent.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("404 Not Found\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("404 Not Found") != std::string::npos);
}

TEST(ClientIntegration_TestGetDirectoryWithIndex) {
    TempDir tmp;
    tmp.createFile("default.html", "<h1>Default Index Page</h1>");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.addIndex("default.html");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("<h1>Default Index Page</h1>") != std::string::npos);
}

TEST(ClientIntegration_TestGetDirectoryNoIndexAutoindexOn) {
    TempDir tmp;
    tmp.createFile("doc1.txt", "content1");
    tmp.createFile("doc2.txt", "content2");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.setAutoindex(true);
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Index of /") != std::string::npos);
    ASSERT_TRUE(response.find("doc1.txt") != std::string::npos);
    ASSERT_TRUE(response.find("doc2.txt") != std::string::npos);
}

TEST(ClientIntegration_TestGetDirectoryNoIndexAutoindexOff) {
    TempDir tmp;
    tmp.createFile("secret.txt", "secret");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.setAutoindex(false);
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("403 Forbidden\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestGetDirectoryMissingTrailingSlash) {
    TempDir tmp;
    tmp.createSubDir("downloads");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /downloads HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("301 Moved Permanently\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Location: /downloads/\r\n") != std::string::npos);
}

// =============================================================================
// 2. HTTP Redirections (301, 302, 500)
// =============================================================================

TEST(ClientIntegration_TestHttpRedirect301) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/old-path");
    loc.addAllowedMethod("GET");
    loc.setRedirect(301, "/new-path");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /old-path HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("301 Moved Permanently\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Location: /new-path\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestHttpRedirect302) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/temp-redirect");
    loc.addAllowedMethod("GET");
    loc.setRedirect(302, "https://example.com/");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /temp-redirect HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("302 Found\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Location: https://example.com/\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestHttpRedirectInvalid500) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/bad-redirect");
    loc.addAllowedMethod("GET");
    loc.setRedirect(500, "/somewhere");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /bad-redirect HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("500 Internal Server Error\r\n") != std::string::npos);
}

// =============================================================================
// 3. Routing & Method Enforcement
// =============================================================================

TEST(ClientIntegration_TestRouteLongestPrefixMatch) {
    ServerConfig server;
    LocationConfig locGeneral;
    locGeneral.setPath("/files");
    locGeneral.addAllowedMethod("GET");
    server.addLocation(locGeneral);

    LocationConfig locRestricted;
    locRestricted.setPath("/files/secure");
    locRestricted.addAllowedMethod("DELETE");
    server.addLocation(locRestricted);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    // Request to /files/secure/data.txt matches /files/secure (allows only DELETE)
    // A GET must be rejected with 405 Method Not Allowed!
    std::string response = runTransaction(client, sp, "GET /files/secure/data.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("405 Method Not Allowed\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestRouteNoMatch404) {
    ServerConfig server; // No routes

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /anywhere HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("404 Not Found\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestDisallowedMethod405) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n");

    ASSERT_TRUE(response.find("405 Method Not Allowed\r\n") != std::string::npos);
}

// =============================================================================
// 4. Custom Error Pages
// =============================================================================

TEST(ClientIntegration_TestCustomErrorPageFile) {
    TempDir tmp;
    tmp.createFile("err404.html", "<h1>Custom Brand 404 Page</h1>");

    ServerConfig server;
    server.addErrorPage(404, tmp.path + "/err404.html");
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /missing.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("404 Not Found\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("<h1>Custom Brand 404 Page</h1>") != std::string::npos);
}

TEST(ClientIntegration_TestMissingCustomErrorPageFallback) {
    ServerConfig server;
    server.addErrorPage(404, "/tmp/nonexistent_error_page_12345.html");
    LocationConfig loc;
    loc.setPath("/");
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /missing.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("404 Not Found\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("webserv") != std::string::npos);
}

// =============================================================================
// 5. File Uploads (POST)
// =============================================================================

TEST(ClientIntegration_TestPostValidUpload) {
    TempDir tmp;
    std::string uploadDir = tmp.path + "/uploads";
    mkdir(uploadDir.c_str(), 0777);

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/upload");
    loc.setUploadStore(uploadDir);
    loc.addAllowedMethod("POST");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string request = 
        "POST /upload/sample.txt HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 16\r\n\r\n"
        "uploaded content";

    std::string response = runTransaction(client, sp, request);

    ASSERT_TRUE(response.find("201 Created\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Location: /upload/sample.txt\r\n") != std::string::npos);
    ASSERT_TRUE(tmp.fileExists("uploads/sample.txt"));
    ASSERT_EQ(tmp.readFile("uploads/sample.txt"), "uploaded content");
}

TEST(ClientIntegration_TestPostBinaryUpload) {
    TempDir tmp;
    std::string uploadDir = tmp.path + "/uploads_bin";
    mkdir(uploadDir.c_str(), 0777);

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/upload");
    loc.setUploadStore(uploadDir);
    loc.addAllowedMethod("POST");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string reqHeader = 
        "POST /upload/data.bin HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 6\r\n\r\n";
    const char binBody[] = {'\x00', '\x01', '\x02', '\x00', '\xFF', '\x00'};
    std::string fullRequest = reqHeader + std::string(binBody, 6);

    std::string response = runTransaction(client, sp, fullRequest);

    ASSERT_TRUE(response.find("201 Created\r\n") != std::string::npos);
    ASSERT_TRUE(tmp.fileExists("uploads_bin/data.bin"));
    std::string readBack = tmp.readFile("uploads_bin/data.bin");
    ASSERT_EQ(readBack.size(), 6u);
    ASSERT_EQ(std::memcmp(readBack.data(), binBody, 6), 0);
}

TEST(ClientIntegration_TestPostNoUploadStore403) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/post_no_store");
    loc.addAllowedMethod("POST");
    // No upload store configured
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string request = 
        "POST /post_no_store/file.txt HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 4\r\n\r\n"
        "test";

    std::string response = runTransaction(client, sp, request);

    ASSERT_TRUE(response.find("403 Forbidden\r\n") != std::string::npos);
}

// =============================================================================
// 6. File Deletion (DELETE)
// =============================================================================

TEST(ClientIntegration_TestDeleteExistingFile) {
    TempDir tmp;
    tmp.createFile("remove_me.txt", "ephemeral");
    ASSERT_TRUE(tmp.fileExists("remove_me.txt"));

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("DELETE");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "DELETE /remove_me.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("204 No Content\r\n") != std::string::npos);
    ASSERT_FALSE(tmp.fileExists("remove_me.txt"));
}

TEST(ClientIntegration_TestDeleteNonExistentFile403) {
    TempDir tmp;

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("DELETE");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "DELETE /already_gone.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("403 Forbidden\r\n") != std::string::npos);
}

// =============================================================================
// 7. Virtual Hosts Resolution
// =============================================================================

TEST(ClientIntegration_TestVirtualHostMatching) {
    TempDir tmpDefault;
    tmpDefault.createFile("site.txt", "Default Site");

    TempDir tmpSpecial;
    tmpSpecial.createFile("site.txt", "Special VHost Site");

    ServerConfig serverDefault;
    serverDefault.setPort(8080);
    serverDefault.addServerName("default.com");
    LocationConfig locDefault;
    locDefault.setPath("/");
    locDefault.setRoot(tmpDefault.path);
    locDefault.addAllowedMethod("GET");
    serverDefault.addLocation(locDefault);

    ServerConfig serverSpecial;
    serverSpecial.setPort(8080);
    serverSpecial.addServerName("special.org");
    LocationConfig locSpecial;
    locSpecial.setPath("/");
    locSpecial.setRoot(tmpSpecial.path);
    locSpecial.addAllowedMethod("GET");
    serverSpecial.addLocation(locSpecial);

    std::vector<ServerConfig> vhosts;
    vhosts.push_back(serverDefault);
    vhosts.push_back(serverSpecial);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(serverDefault);
    client.setVirtualHosts(vhosts);

    // Request targetting special.org should route to serverSpecial
    std::string response = runTransaction(client, sp, "GET /site.txt HTTP/1.1\r\nHost: special.org\r\n\r\n");

    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Special VHost Site") != std::string::npos);
}

TEST(ClientIntegration_TestVirtualHostPortStripping) {
    TempDir tmp;
    tmp.createFile("info.txt", "Port Stripped Host Match");

    ServerConfig s1;
    s1.setPort(8080);
    s1.addServerName("test.com");
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    s1.addLocation(loc);

    std::vector<ServerConfig> vhosts;
    vhosts.push_back(s1);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(s1);
    client.setVirtualHosts(vhosts);

    // "Host: test.com:8080" has port attached
    std::string response = runTransaction(client, sp, "GET /info.txt HTTP/1.1\r\nHost: test.com:8080\r\n\r\n");

    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Port Stripped Host Match") != std::string::npos);
}

TEST(ClientIntegration_TestVirtualHostFallbackDefault) {
    TempDir tmp;
    tmp.createFile("fallback.txt", "Fallback Content");

    ServerConfig def;
    def.setPort(8080);
    def.addServerName("default.com");
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    def.addLocation(loc);

    std::vector<ServerConfig> vhosts;
    vhosts.push_back(def);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(def);
    client.setVirtualHosts(vhosts);

    std::string response = runTransaction(client, sp, "GET /fallback.txt HTTP/1.1\r\nHost: unrecognized.net\r\n\r\n");

    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("Fallback Content") != std::string::npos);
}

// =============================================================================
// 8. Protocol Validation & Payload Limits
// =============================================================================

TEST(ClientIntegration_TestHttp11MissingHostHeader400) {
    SocketPair sp;
    Client client(sp.clientFd());

    std::string response = runTransaction(client, sp, "GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n");

    ASSERT_TRUE(response.find("400 Bad Request\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestMaxBodySizeHeaderExceeded413) {
    ServerConfig server;
    server.setClientMaxBodySize(10); // 10 bytes limit

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string request = 
        "POST /submit HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 50\r\n\r\n";

    std::string response = runTransaction(client, sp, request);

    ASSERT_TRUE(response.find("413 Payload Too Large\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestMalformedRequestLine400) {
    SocketPair sp;
    Client client(sp.clientFd());

    std::string response = runTransaction(client, sp, "GET /\r\n\r\n");

    ASSERT_TRUE(response.find("400 Bad Request\r\n") != std::string::npos);
}

TEST(ClientIntegration_TestUnsupportedHttpVersion505) {
    SocketPair sp;
    Client client(sp.clientFd());

    std::string response = runTransaction(client, sp, "GET / HTTP/2.0\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("505 HTTP Version Not Supported\r\n") != std::string::npos);
}

// =============================================================================
// 9. Keep-Alive & Connection Lifecycle
// =============================================================================

TEST(ClientIntegration_TestKeepAliveHttp11Default) {
    TempDir tmp;
    tmp.createFile("test.txt", "data");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /test.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("Connection: keep-alive\r\n") != std::string::npos);
    // After keep-alive write, client must reset to READING_HEADER for next request
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
}

TEST(ClientIntegration_TestKeepAliveHttp11ExplicitClose) {
    TempDir tmp;
    tmp.createFile("test.txt", "data");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /test.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    ASSERT_TRUE(response.find("Connection: close\r\n") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::DONE);
}

TEST(ClientIntegration_TestKeepAliveHttp10Default) {
    TempDir tmp;
    tmp.createFile("test.txt", "data");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /test.txt HTTP/1.0\r\n\r\n");

    ASSERT_TRUE(response.find("Connection: close\r\n") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::DONE);
}

TEST(ClientIntegration_TestKeepAliveHttp10ExplicitKeepAlive) {
    TempDir tmp;
    tmp.createFile("test.txt", "data");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string response = runTransaction(client, sp, "GET /test.txt HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");

    ASSERT_TRUE(response.find("Connection: keep-alive\r\n") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
}

TEST(ClientIntegration_TestFatalErrorClosesConnection) {
    SocketPair sp;
    Client client(sp.clientFd());

    // 400 Bad Request forces connection close
    std::string response = runTransaction(client, sp, "GET /\r\n\r\n");

    ASSERT_TRUE(response.find("Connection: close\r\n") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::DONE);
}

TEST(ClientIntegration_TestNonFatalErrorAllowsKeepAlive) {
    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    // 404 allows keep-alive on HTTP/1.1
    std::string response = runTransaction(client, sp, "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");

    ASSERT_TRUE(response.find("Connection: keep-alive\r\n") != std::string::npos);
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);
}

// =============================================================================
// 10. Socket Streaming & Fragmentation
// =============================================================================

TEST(ClientIntegration_TestSocketSplitHeaderRead) {
    TempDir tmp;
    tmp.createFile("data.txt", "payload");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    // Send first piece of header
    sp.sendToClient("GET /data.txt");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);

    // Send second piece
    sp.sendToClient(" HTTP/1.1\r\nHost:");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::READING_HEADER);

    // Send final piece with double CRLF
    sp.sendToClient(" localhost\r\n\r\n");
    client.handleRead();

    ASSERT_EQ(client.getClientState(), Client::PROCESSING);
    client.handleProcessing();
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
    client.handleWrite();

    std::string response = sp.readFromClient();
    ASSERT_TRUE(response.find("200 OK\r\n") != std::string::npos);
    ASSERT_TRUE(response.find("payload") != std::string::npos);
}

TEST(ClientIntegration_TestSocketPostWithBody) {
    TempDir tmp;
    std::string uploadDir = tmp.path + "/uploads";
    mkdir(uploadDir.c_str(), 0777);

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/upload");
    loc.setUploadStore(uploadDir);
    loc.addAllowedMethod("POST");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    // Send headers first
    sp.sendToClient("POST /upload/stream.txt HTTP/1.1\r\nHost: localhost\r\nContent-Length: 12\r\n\r\n");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::READING_BODY);

    // Send body
    sp.sendToClient("streamedData");
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);

    client.handleProcessing();
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
    client.handleWrite();

    std::string response = sp.readFromClient();
    ASSERT_TRUE(response.find("201 Created\r\n") != std::string::npos);
    ASSERT_TRUE(tmp.fileExists("uploads/stream.txt"));
    ASSERT_EQ(tmp.readFile("uploads/stream.txt"), "streamedData");
}

TEST(ClientIntegration_TestSocketClientDisconnectEOF) {
    SocketPair sp;
    Client client(sp.clientFd());

    // Peer closes connection
    close(sp.sv[1]);
    sp.valid = false;
    close(sp.sv[0]);

    SocketPair sp2;
    Client client2(sp2.clientFd());
    close(sp2.sv[1]); // Close peer socket immediately

    client2.handleRead();
    ASSERT_EQ(client2.getClientState(), Client::DONE);
}

TEST(ClientIntegration_TestSocketPipelinedRequests) {
    TempDir tmp;
    tmp.createFile("first.txt", "First Response");
    tmp.createFile("second.txt", "Second Response");

    ServerConfig server;
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    server.addLocation(loc);

    SocketPair sp;
    Client client(sp.clientFd());
    client.setServer(server);

    std::string pipelined = 
        "GET /first.txt HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /second.txt HTTP/1.1\r\nHost: localhost\r\n\r\n";

    sp.sendToClient(pipelined);

    // Read first request
    client.handleRead();
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);

    // Process and send first response
    client.handleProcessing();
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
    client.handleWrite();

    std::string firstResponse = sp.readFromClient();
    ASSERT_TRUE(firstResponse.find("First Response") != std::string::npos);

    // After keep-alive reset, client must automatically parse second request from buffered bytes
    ASSERT_EQ(client.getClientState(), Client::PROCESSING);

    // Process and send second response
    client.handleProcessing();
    ASSERT_EQ(client.getClientState(), Client::WRITING_RESPONSE);
    client.handleWrite();

    std::string secondResponse = sp.readFromClient();
    ASSERT_TRUE(secondResponse.find("Second Response") != std::string::npos);
}
