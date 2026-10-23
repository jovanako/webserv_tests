#include "tiny_test.hpp"
#include "CgiRequest.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "HttpRequest.hpp"
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <new>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <cstdlib>

namespace {

ServerConfig createDefaultServerConfig() {
    ServerConfig srv;
    srv.setHost("127.0.0.1");
    srv.setPort(8080);
    srv.addServerName("localhost");
    return srv;
}

LocationConfig createDefaultCgiLocation() {
    LocationConfig loc;
    loc.setPath("/cgi-bin");
    loc.setRoot("/var/www/cgi");
    loc.addAllowedMethod("GET");
    loc.addAllowedMethod("POST");
    loc.addCgiHandler(".py", "/usr/bin/python3");
    loc.addCgiHandler(".php", "/usr/bin/php-cgi");
    loc.addCgiHandler(".sh", "/bin/bash");
    return loc;
}

HttpRequest createDefaultGetCgiRequest(const std::string& uri = "/cgi-bin/script.py") {
    HttpRequest req;
    req.setMethod("GET");
    req.setUri(uri);
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost:8080");
    return req;
}

// Helper to parse "KEY=VALUE" vector into a map for order-independent assertions
std::map<std::string, std::string> parseEnvVars(const std::vector<std::string>& envVars) {
    std::map<std::string, std::string> result;
    for (size_t i = 0; i < envVars.size(); ++i) {
        size_t eqPos = envVars[i].find('=');
        if (eqPos != std::string::npos) {
            std::string key = envVars[i].substr(0, eqPos);
            std::string val = envVars[i].substr(eqPos + 1);
            result[key] = val;
        }
    }
    return result;
}

bool hasEnvKey(const std::vector<std::string>& envVars, const std::string& key) {
    std::string prefix = key + "=";
    for (size_t i = 0; i < envVars.size(); ++i) {
        if (envVars[i].compare(0, prefix.length(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

std::string getEnvValue(const std::vector<std::string>& envVars, const std::string& key) {
    std::map<std::string, std::string> envMap = parseEnvVars(envVars);
    std::map<std::string, std::string>::const_iterator it = envMap.find(key);
    if (it == envMap.end()) {
        return "";
    }
    return it->second;
}

// Helper that safely verifies CgiRequest construction throws a meaningful std::exception
// (rejecting accidental std::bad_alloc or SIGSEGV caused by dereferencing map::end())
void assertCgiConstructorThrows(const ServerConfig& srv, const LocationConfig& loc,
                                const HttpRequest& req, const char* file, int line) {
    pid_t pid = fork();
    if (pid == 0) {
        try {
            CgiRequest cgi(srv, loc, req);
            _exit(0);
        } catch (const std::bad_alloc&) {
            _exit(44);
        } catch (const std::exception&) {
            _exit(42);
        } catch (...) {
            _exit(43);
        }
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            throw TinyTest::AssertionFailure(
                file, line,
                "Expected std::exception to be thrown, but CgiRequest terminated with a signal (e.g. SIGSEGV)");
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 44) {
            throw TinyTest::AssertionFailure(
                file, line,
                "Expected a validation exception, but caught std::bad_alloc (likely from dereferencing cgiHandlers.end())");
        }
    }

    // Run in-process so coverage tools record the exception path once implemented
    bool caught = false;
    try {
        CgiRequest cgi(srv, loc, req);
    } catch (const std::bad_alloc&) {
        throw TinyTest::AssertionFailure(
            file, line,
            "Expected a validation exception, but caught std::bad_alloc (likely from dereferencing cgiHandlers.end())");
    } catch (const std::exception&) {
        caught = true;
    } catch (...) {
        throw TinyTest::AssertionFailure(
            file, line,
            "Expected std::exception, but caught a non-standard exception");
    }
    if (!caught) {
        throw TinyTest::AssertionFailure(
            file, line,
            "Expected std::exception to be thrown, but nothing was thrown");
    }
}

#define ASSERT_CGI_THROWS(srv, loc, req) \
    assertCgiConstructorThrows((srv), (loc), (req), __FILE__, __LINE__)

bool isValidBodyFd(int fd) {
    if (fd <= STDERR_FILENO) {
        return false;
    }
    int flags = fcntl(fd, F_GETFL);
    return flags != -1;
}

std::string readAllFromFd(int fd) {
    if (!isValidBodyFd(fd)) {
        return "";
    }
    lseek(fd, 0, SEEK_SET);
    std::string content;
    char buf[1024];
    while (true) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ready = poll(&pfd, 1, 100);
        if (ready <= 0 || !(pfd.revents & POLLIN)) {
            break;
        }
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        content.append(buf, static_cast<size_t>(n));
    }
    return content;
}

} // namespace

// =============================================================================
// 1. Orthodox Canonical Form (Copy Constructor & Assignment Operator)
// =============================================================================

TEST(CgiRequest_TestCopyConstructorBasic) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/hello.py?user=42");
    req.addHeader("User-Agent", "TestAgent/1.0");
    req.addHeader("Accept", "text/html");

    CgiRequest original(srv, loc, req);
    CgiRequest copy(original);

    ASSERT_EQ(copy.getExecutable(), original.getExecutable());
    ASSERT_EQ(copy.getScriptPath(), original.getScriptPath());
    ASSERT_EQ(copy.getWorkingDirectory(), original.getWorkingDirectory());
    ASSERT_EQ(copy.getBodyFileDescriptor(), original.getBodyFileDescriptor());
    ASSERT_EQ(copy.getEnvVars().size(), original.getEnvVars().size());
    for (size_t i = 0; i < original.getEnvVars().size(); ++i) {
        ASSERT_EQ(copy.getEnvVars()[i], original.getEnvVars()[i]);
    }
}

TEST(CgiRequest_TestAssignmentOperatorBasic) {
    ServerConfig srv1 = createDefaultServerConfig();
    LocationConfig loc1 = createDefaultCgiLocation();
    HttpRequest req1 = createDefaultGetCgiRequest("/cgi-bin/first.py");
    req1.addHeader("User-Agent", "AgentOne");

    ServerConfig srv2 = createDefaultServerConfig();
    srv2.setPort(9090);
    LocationConfig loc2 = createDefaultCgiLocation();
    loc2.setRoot("/var/www/other_cgi");
    HttpRequest req2;
    req2.setMethod("POST");
    req2.setUri("/cgi-bin/second.php");
    req2.setVersion("HTTP/1.0");
    req2.addHeader("Host", "example.com");
    req2.addHeader("Content-Type", "text/plain");
    req2.addHeader("Content-Length", "4");
    req2.appendBody("test", 4);

    CgiRequest original(srv1, loc1, req1);
    CgiRequest assigned(srv2, loc2, req2);

    assigned = original;

    ASSERT_EQ(assigned.getExecutable(), original.getExecutable());
    ASSERT_EQ(assigned.getScriptPath(), original.getScriptPath());
    ASSERT_EQ(assigned.getWorkingDirectory(), original.getWorkingDirectory());
    ASSERT_EQ(assigned.getBodyFileDescriptor(), original.getBodyFileDescriptor());
    ASSERT_EQ(assigned.getEnvVars().size(), original.getEnvVars().size());
    for (size_t i = 0; i < original.getEnvVars().size(); ++i) {
        ASSERT_EQ(assigned.getEnvVars()[i], original.getEnvVars()[i]);
    }
}

TEST(CgiRequest_TestAssignmentOperatorSelfAssignment) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/self.py");

    CgiRequest cgi(srv, loc, req);
    std::string expectedExec = cgi.getExecutable();
    std::string expectedScript = cgi.getScriptPath();
    std::string expectedWorkDir = cgi.getWorkingDirectory();
    size_t expectedEnvCount = cgi.getEnvVars().size();

    cgi = cgi;

    ASSERT_EQ(cgi.getExecutable(), expectedExec);
    ASSERT_EQ(cgi.getScriptPath(), expectedScript);
    ASSERT_EQ(cgi.getWorkingDirectory(), expectedWorkDir);
    ASSERT_EQ(cgi.getEnvVars().size(), expectedEnvCount);
}

TEST(CgiRequest_TestAssignmentOperatorChaining) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req1 = createDefaultGetCgiRequest("/cgi-bin/source.py");
    HttpRequest req2 = createDefaultGetCgiRequest("/cgi-bin/other.php");
    HttpRequest req3 = createDefaultGetCgiRequest("/cgi-bin/third.sh");

    CgiRequest src(srv, loc, req1);
    CgiRequest dest1(srv, loc, req2);
    CgiRequest dest2(srv, loc, req3);

    dest2 = dest1 = src;

    ASSERT_EQ(dest1.getExecutable(), "/usr/bin/python3");
    ASSERT_EQ(dest2.getExecutable(), "/usr/bin/python3");
    ASSERT_EQ(dest1.getScriptPath(), src.getScriptPath());
    ASSERT_EQ(dest2.getScriptPath(), src.getScriptPath());
}

// =============================================================================
// 2. Executable Resolution (getExecutable)
// =============================================================================

TEST(CgiRequest_TestExecutableSingleAndMultipleHandlers) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();

    HttpRequest pyReq = createDefaultGetCgiRequest("/cgi-bin/script.py");
    CgiRequest pyCgi(srv, loc, pyReq);
    ASSERT_EQ(pyCgi.getExecutable(), "/usr/bin/python3");

    HttpRequest phpReq = createDefaultGetCgiRequest("/cgi-bin/index.php");
    CgiRequest phpCgi(srv, loc, phpReq);
    ASSERT_EQ(phpCgi.getExecutable(), "/usr/bin/php-cgi");

    HttpRequest shReq = createDefaultGetCgiRequest("/cgi-bin/deploy.sh");
    CgiRequest shCgi(srv, loc, shReq);
    ASSERT_EQ(shCgi.getExecutable(), "/bin/bash");
}

TEST(CgiRequest_TestExecutableMultipleDotsInFilename) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/app.v2.prod.py");

    CgiRequest cgi(srv, loc, req);
    ASSERT_EQ(cgi.getExecutable(), "/usr/bin/python3");
}

TEST(CgiRequest_TestExecutableWithQueryStringContainingDot) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/run.py?template=page.php&v=1.0");

    CgiRequest cgi(srv, loc, req);
    ASSERT_EQ(cgi.getExecutable(), "/usr/bin/python3");
}

TEST(CgiRequest_TestExecutableUnregisteredExtensionThrows) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script.rb");

    ASSERT_CGI_THROWS(srv, loc, req);
}

TEST(CgiRequest_TestExecutableNoExtensionThrows) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script_without_ext");

    ASSERT_CGI_THROWS(srv, loc, req);
}

TEST(CgiRequest_TestExecutableDotInDirectoryButNotFilenameThrows) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/v1.0/binary");

    ASSERT_CGI_THROWS(srv, loc, req);
}

TEST(CgiRequest_TestExecutableEmptyCgiHandlersMapThrows) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc;
    loc.setPath("/cgi-bin");
    loc.setRoot("/var/www/cgi");
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script.py");

    ASSERT_CGI_THROWS(srv, loc, req);
}

// =============================================================================
// 3. Script Path & Working Directory Resolution
// =============================================================================

TEST(CgiRequest_TestPathsDirectInLocationRoot) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation(); // path: /cgi-bin, root: /var/www/cgi
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/hello.py");

    CgiRequest cgi(srv, loc, req);

    ASSERT_EQ(cgi.getScriptPath(), "/var/www/cgi/hello.py");
    ASSERT_EQ(cgi.getWorkingDirectory(), "/var/www/cgi");
}

TEST(CgiRequest_TestPathsNestedSubdirectory) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation(); // path: /cgi-bin, root: /var/www/cgi
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/admin/tools/report.py");

    CgiRequest cgi(srv, loc, req);

    ASSERT_EQ(cgi.getScriptPath(), "/var/www/cgi/admin/tools/report.py");
    ASSERT_EQ(cgi.getWorkingDirectory(), "/var/www/cgi/admin/tools");
}

TEST(CgiRequest_TestPathsWithTrailingSlashInLocationRoot) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    loc.setRoot("/var/www/cgi/");
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script.py");

    CgiRequest cgi(srv, loc, req);

    ASSERT_EQ(cgi.getScriptPath(), "/var/www/cgi/script.py");
    ASSERT_EQ(cgi.getWorkingDirectory(), "/var/www/cgi");
}

TEST(CgiRequest_TestPathsRootLocation) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot("/var/www/html");
    loc.addCgiHandler(".php", "/usr/bin/php-cgi");

    HttpRequest req = createDefaultGetCgiRequest("/blog/post.php");

    CgiRequest cgi(srv, loc, req);

    ASSERT_EQ(cgi.getScriptPath(), "/var/www/html/blog/post.php");
    ASSERT_EQ(cgi.getWorkingDirectory(), "/var/www/html/blog");
}

TEST(CgiRequest_TestPathInfoResolutionAndEnvVars) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation(); // path: /cgi-bin, root: /var/www/cgi
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script.py/users/profile?tab=settings");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_EQ(cgi.getExecutable(), "/usr/bin/python3");
    ASSERT_EQ(cgi.getScriptPath(), "/var/www/cgi/script.py");
    ASSERT_EQ(cgi.getWorkingDirectory(), "/var/www/cgi");

    ASSERT_TRUE(hasEnvKey(envVars, "SCRIPT_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SCRIPT_NAME"), "/cgi-bin/script.py");

    ASSERT_TRUE(hasEnvKey(envVars, "PATH_INFO"));
    ASSERT_EQ(getEnvValue(envVars, "PATH_INFO"), "/users/profile");

    ASSERT_TRUE(hasEnvKey(envVars, "PATH_TRANSLATED"));
    ASSERT_EQ(getEnvValue(envVars, "PATH_TRANSLATED"), "/var/www/cgi/users/profile");

    ASSERT_TRUE(hasEnvKey(envVars, "QUERY_STRING"));
    ASSERT_EQ(getEnvValue(envVars, "QUERY_STRING"), "tab=settings");
}

TEST(CgiRequest_TestPathInfoWithDotsInExtraPath) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation(); // path: /cgi-bin, root: /var/www/cgi
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/api.py/v1.0/item.json");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_EQ(cgi.getExecutable(), "/usr/bin/python3");
    ASSERT_EQ(cgi.getScriptPath(), "/var/www/cgi/api.py");
    ASSERT_EQ(cgi.getWorkingDirectory(), "/var/www/cgi");

    ASSERT_EQ(getEnvValue(envVars, "SCRIPT_NAME"), "/cgi-bin/api.py");
    ASSERT_EQ(getEnvValue(envVars, "PATH_INFO"), "/v1.0/item.json");
    ASSERT_EQ(getEnvValue(envVars, "PATH_TRANSLATED"), "/var/www/cgi/v1.0/item.json");
}

TEST(CgiRequest_TestPathInfoAbsentWhenNoExtraPath) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script.py");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_EQ(getEnvValue(envVars, "PATH_INFO"), "");
    ASSERT_FALSE(hasEnvKey(envVars, "PATH_TRANSLATED"));
}

// =============================================================================
// 4. CGI Environment Variables (getEnvVars)
// =============================================================================

TEST(CgiRequest_TestEnvVarsStandardGetMetaVariables) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/search.py?q=webserv&lang=en");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "example.com:8080");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "GATEWAY_INTERFACE"));
    ASSERT_EQ(getEnvValue(envVars, "GATEWAY_INTERFACE"), "CGI/1.1");

    ASSERT_TRUE(hasEnvKey(envVars, "REQUEST_METHOD"));
    ASSERT_EQ(getEnvValue(envVars, "REQUEST_METHOD"), "GET");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PROTOCOL"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PROTOCOL"), "HTTP/1.1");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_SOFTWARE"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_SOFTWARE"), "webserv/1.0");

    ASSERT_TRUE(hasEnvKey(envVars, "QUERY_STRING"));
    ASSERT_EQ(getEnvValue(envVars, "QUERY_STRING"), "q=webserv&lang=en");

    ASSERT_TRUE(hasEnvKey(envVars, "SCRIPT_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SCRIPT_NAME"), "/cgi-bin/search.py");
}

TEST(CgiRequest_TestEnvVarsEmptyQueryString) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/status.py");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "QUERY_STRING"));
    ASSERT_EQ(getEnvValue(envVars, "QUERY_STRING"), "");
}

TEST(CgiRequest_TestEnvVarsServerNameMatchesHostHeaderWithPort) {
    ServerConfig srv;
    srv.setHost("127.0.0.1");
    srv.setPort(9000);
    srv.addServerName("example.org");
    srv.addServerName("api.example.org");

    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/info.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "api.example.org:9000");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_HOST"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_HOST"), "api.example.org:9000");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_NAME"), "api.example.org");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PORT"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PORT"), "9000");
}

TEST(CgiRequest_TestEnvVarsServerNameMatchesHostHeaderWithoutPort) {
    ServerConfig srv;
    srv.setHost("127.0.0.1");
    srv.setPort(80);
    srv.addServerName("default.org");
    srv.addServerName("example.org");

    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/info.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "example.org");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_HOST"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_HOST"), "example.org");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_NAME"), "example.org");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PORT"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PORT"), "80");
}

TEST(CgiRequest_TestEnvVarsServerNameFallbackToFirstServerNameOnMismatch) {
    ServerConfig srv;
    srv.setHost("127.0.0.1");
    srv.setPort(8080);
    srv.addServerName("primary.example.org");
    srv.addServerName("secondary.example.org");

    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/info.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "unknown.example.org:8080");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_NAME"), "primary.example.org");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PORT"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PORT"), "8080");
}

TEST(CgiRequest_TestEnvVarsServerNameFallbackToServerHostWhenNoServerNames) {
    ServerConfig srv;
    srv.setHost("10.0.0.42");
    srv.setPort(3000);

    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/info.py");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_NAME"), "10.0.0.42");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PORT"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PORT"), "3000");
}

TEST(CgiRequest_TestEnvVarsRequiredServerMetaVarsWithoutHostHeader) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/legacy.py");
    req.setVersion("HTTP/1.0");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "GATEWAY_INTERFACE"));
    ASSERT_EQ(getEnvValue(envVars, "GATEWAY_INTERFACE"), "CGI/1.1");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_NAME"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_NAME"), "localhost");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PORT"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PORT"), "8080");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_PROTOCOL"));
    ASSERT_EQ(getEnvValue(envVars, "SERVER_PROTOCOL"), "HTTP/1.0");

    ASSERT_TRUE(hasEnvKey(envVars, "SERVER_SOFTWARE"));
    ASSERT_FALSE(getEnvValue(envVars, "SERVER_SOFTWARE").empty());
}

TEST(CgiRequest_TestEnvVarsPostWithContentHeaders) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("POST");
    req.setUri("/cgi-bin/submit.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost:8080");
    req.addHeader("Content-Type", "application/x-www-form-urlencoded");
    req.addHeader("Content-Length", "15");
    req.appendBody("name=42&city=be", 15);

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_EQ(getEnvValue(envVars, "REQUEST_METHOD"), "POST");
    ASSERT_TRUE(hasEnvKey(envVars, "CONTENT_TYPE"));
    ASSERT_EQ(getEnvValue(envVars, "CONTENT_TYPE"), "application/x-www-form-urlencoded");
    ASSERT_TRUE(hasEnvKey(envVars, "CONTENT_LENGTH"));
    ASSERT_EQ(getEnvValue(envVars, "CONTENT_LENGTH"), "15");

    // RFC 3875 Section 4.1.18: Content-Type and Content-Length should not be duplicated as HTTP_*
    ASSERT_FALSE(hasEnvKey(envVars, "HTTP_CONTENT_TYPE"));
    ASSERT_FALSE(hasEnvKey(envVars, "HTTP_CONTENT_LENGTH"));
}

TEST(CgiRequest_TestEnvVarsOptionalHeadersOmittedWhenAbsent) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/minimal.py");
    req.setVersion("HTTP/1.0");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_FALSE(hasEnvKey(envVars, "CONTENT_LENGTH"));
    ASSERT_FALSE(hasEnvKey(envVars, "CONTENT_TYPE"));
    ASSERT_FALSE(hasEnvKey(envVars, "HTTP_HOST"));
    ASSERT_FALSE(hasEnvKey(envVars, "HTTP_USER_AGENT"));
    ASSERT_FALSE(hasEnvKey(envVars, "HTTP_ACCEPT"));
}

TEST(CgiRequest_TestEnvVarsHttpHeadersMappedCorrectly) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/headers.py");
    req.addHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64)");
    req.addHeader("Accept", "text/html,application/xhtml+xml");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_HOST"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_HOST"), "localhost:8080");

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_USER_AGENT"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_USER_AGENT"), "Mozilla/5.0 (X11; Linux x86_64)");

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_ACCEPT"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_ACCEPT"), "text/html,application/xhtml+xml");
}

TEST(CgiRequest_TestEnvVarsArbitraryHttpHeadersMappedAndNormalized) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/custom_headers.py");
    req.addHeader("Cookie", "session_id=abc123; theme=dark");
    req.addHeader("Accept-Language", "en-US,en;q=0.9");
    req.addHeader("X-Forwarded-For", "192.168.1.10");
    req.addHeader("X-Custom-Token", "secret-42");

    CgiRequest cgi(srv, loc, req);
    const std::vector<std::string>& envVars = cgi.getEnvVars();

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_COOKIE"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_COOKIE"), "session_id=abc123; theme=dark");

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_ACCEPT_LANGUAGE"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_ACCEPT_LANGUAGE"), "en-US,en;q=0.9");

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_X_FORWARDED_FOR"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_X_FORWARDED_FOR"), "192.168.1.10");

    ASSERT_TRUE(hasEnvKey(envVars, "HTTP_X_CUSTOM_TOKEN"));
    ASSERT_EQ(getEnvValue(envVars, "HTTP_X_CUSTOM_TOKEN"), "secret-42");
}

// =============================================================================
// 5. Request Body File Descriptor (getBodyFileDescriptor)
// =============================================================================

TEST(CgiRequest_TestBodyFdEmptyBodyReturnsMinusOne) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/script.py");

    CgiRequest cgi(srv, loc, req);

    ASSERT_EQ(cgi.getBodyFileDescriptor(), -1);
}

TEST(CgiRequest_TestBodyFdNonEmptyBodyProvidesReadableFd) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("POST");
    req.setUri("/cgi-bin/upload.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost:8080");
    const std::string payload = "username=jovana&project=webserv";
    req.addHeader("Content-Length", "31");
    req.appendBody(payload.c_str(), payload.size());

    CgiRequest cgi(srv, loc, req);

    int fd = cgi.getBodyFileDescriptor();
    ASSERT_TRUE(isValidBodyFd(fd));

    std::string readBack = readAllFromFd(fd);
    ASSERT_EQ(readBack, payload);
}

TEST(CgiRequest_TestBodyFdBinaryBodyPreservesNullBytes) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req;
    req.setMethod("POST");
    req.setUri("/cgi-bin/binary.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost:8080");
    const char rawBytes[] = {'\x00', 'A', '\x00', '\xFF', 'B', '\x7F'};
    req.addHeader("Content-Length", "6");
    req.appendBody(rawBytes, sizeof(rawBytes));

    CgiRequest cgi(srv, loc, req);

    int fd = cgi.getBodyFileDescriptor();
    ASSERT_TRUE(isValidBodyFd(fd));

    std::string readBack = readAllFromFd(fd);
    ASSERT_EQ(readBack.size(), sizeof(rawBytes));
    for (size_t i = 0; i < sizeof(rawBytes); ++i) {
        ASSERT_EQ(readBack[i], rawBytes[i]);
    }
}

// =============================================================================
// 6. Const-Correctness
// =============================================================================

TEST(CgiRequest_TestConstGetters) {
    ServerConfig srv = createDefaultServerConfig();
    LocationConfig loc = createDefaultCgiLocation();
    HttpRequest req = createDefaultGetCgiRequest("/cgi-bin/const_check.py");

    const CgiRequest constCgi(srv, loc, req);

    ASSERT_EQ(constCgi.getExecutable(), "/usr/bin/python3");
    ASSERT_EQ(constCgi.getScriptPath(), "/var/www/cgi/const_check.py");
    ASSERT_EQ(constCgi.getWorkingDirectory(), "/var/www/cgi");
    ASSERT_FALSE(constCgi.getEnvVars().empty());
    ASSERT_EQ(constCgi.getBodyFileDescriptor(), -1);
}
