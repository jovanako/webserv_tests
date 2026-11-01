#include "tiny_test.hpp"
#include "CgiExecutor.hpp"
#include "CgiRequest.hpp"
#include "CgiResponse.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>

namespace {

class TempDir {
public:
    std::string path;

    TempDir() {
        char tmpl[] = "/tmp/webserv_cgi_exec_XXXXXX";
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

    std::string createFile(const std::string& relativePath, const std::string& content, mode_t mode = 0755) {
        std::string full = path + "/" + relativePath;
        std::ofstream ofs(full.c_str(), std::ios::out | std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(content.data(), content.size());
            ofs.close();
        }
        chmod(full.c_str(), mode);
        return full;
    }

    std::string createSubDir(const std::string& relativePath) {
        std::string full = path + "/" + relativePath;
        mkdir(full.c_str(), 0777);
        return full;
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
                    chmod(fullPath.c_str(), 0700);
                    std::remove(fullPath.c_str());
                }
            }
        }
        closedir(dir);
        rmdir(dirPath.c_str());
    }
};

ServerConfig makeServerConfig() {
    ServerConfig srv;
    srv.setHost("127.0.0.1");
    srv.setPort(8080);
    srv.addServerName("localhost");
    return srv;
}

LocationConfig makeCgiLocation(const std::string& rootPath) {
    LocationConfig loc;
    loc.setPath("/cgi-bin");
    loc.setRoot(rootPath);
    loc.addAllowedMethod("GET");
    loc.addAllowedMethod("POST");
    loc.addCgiHandler(".py", "/usr/bin/python3");
    loc.addCgiHandler(".sh", "/bin/sh");
    return loc;
}

// Drive CgiExecutor via poll() on its non-blocking pipe until completion or error,
// strictly adhering to the webserv subject requirement that all pipe reads go through poll().
bool driveExecutorWithPoll(CgiExecutor& executor, int timeoutMs = 5000) {
    while (executor.getState() == CgiExecutor::CGI_RUNNING) {
        int pipeFd = executor.getPipeFd();
        if (pipeFd < 0) {
            return false;
        }
        struct pollfd pfd;
        pfd.fd = pipeFd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ready = poll(&pfd, 1, timeoutMs);
        if (ready <= 0) {
            return false;
        }
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            executor.readOutput();
        }
    }
    return executor.getState() == CgiExecutor::CGI_COMPLETE;
}

std::string vecToStr(const std::vector<char>& vec) {
    if (vec.empty()) return "";
    return std::string(vec.begin(), vec.end());
}

} // namespace

// =============================================================================
// 1. Initial State, Canonical Form & Safe Cleanup
// =============================================================================

TEST(CgiExecutor_TestInitialStateAndCanonicalForm) {
    CgiExecutor exec1;
    ASSERT_EQ(exec1.getState(), CgiExecutor::CGI_IDLE);
    ASSERT_EQ(exec1.getPipeFd(), -1);
    ASSERT_EQ(exec1.getPid(), -1);
    ASSERT_EQ(exec1.getErrorCode(), 0);
    ASSERT_FALSE(exec1.hasTimedOut());
    ASSERT_TRUE(exec1.getOutputBuffer().empty());
    ASSERT_FALSE(exec1.readOutput());

    CgiExecutor exec2(exec1);
    ASSERT_EQ(exec2.getState(), CgiExecutor::CGI_IDLE);
    ASSERT_EQ(exec2.getPipeFd(), -1);
    ASSERT_EQ(exec2.getPid(), -1);

    CgiExecutor exec3;
    exec3 = exec1;
    exec3 = exec3;
    ASSERT_EQ(exec3.getState(), CgiExecutor::CGI_IDLE);

    exec3.cleanup();
    ASSERT_EQ(exec3.getState(), CgiExecutor::CGI_IDLE);
}

// =============================================================================
// 2. Script & Interpreter Pre-Execution Validation (404, 403, 500)
// =============================================================================

TEST(CgiExecutor_TestMissingScriptReturns404) {
    TempDir tmp;
    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/nonexistent.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_FALSE(executor.start(cgiReq));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 404);
    ASSERT_EQ(executor.getPipeFd(), -1);
}

TEST(CgiExecutor_TestDirectoryAsScriptReturns403) {
    TempDir tmp;
    tmp.createSubDir("folder.py");

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/folder.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_FALSE(executor.start(cgiReq));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 403);
}

TEST(CgiExecutor_TestUnreadableScriptReturns403) {
    if (geteuid() == 0) {
        return; // Root bypasses R_OK file mode checks
    }

    TempDir tmp;
    tmp.createFile("unreadable.py", "print('Content-Type: text/plain\\r\\n\\r\\nok')\n", 0000);

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/unreadable.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_FALSE(executor.start(cgiReq));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 403);
}

TEST(CgiExecutor_TestInvalidOrNonExecutableInterpreterReturns500) {
    TempDir tmp;
    tmp.createFile("script.py", "print('Content-Type: text/plain\\r\\n\\r\\nok')\n", 0644);
    std::string nonExecBin = tmp.createFile("fake_interp", "#!/bin/sh\n", 0644); // No execute permission

    ServerConfig srv = makeServerConfig();
    LocationConfig loc;
    loc.setPath("/cgi-bin");
    loc.setRoot(tmp.path);
    loc.addAllowedMethod("GET");
    loc.addCgiHandler(".py", "/nonexistent/interpreter/python3");

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/script.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    // 1. Non-existent interpreter binary -> 500
    {
        CgiRequest cgiReq(srv, loc, req);
        CgiExecutor executor;
        ASSERT_FALSE(executor.start(cgiReq));
        ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
        ASSERT_EQ(executor.getErrorCode(), 500);
    }

    // 2. Interpreter file exists but lacks execute permission (X_OK) -> 500
    if (geteuid() != 0) {
        LocationConfig locNoExec;
        locNoExec.setPath("/cgi-bin");
        locNoExec.setRoot(tmp.path);
        locNoExec.addAllowedMethod("GET");
        locNoExec.addCgiHandler(".py", nonExecBin);

        CgiRequest cgiReq(srv, locNoExec, req);
        CgiExecutor executor;
        ASSERT_FALSE(executor.start(cgiReq));
        ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
        ASSERT_EQ(executor.getErrorCode(), 500);
    }
}

// =============================================================================
// 3. Non-Blocking Pipe & Poll-Driven Execution (Subject IV.1)
// =============================================================================

TEST(CgiExecutor_TestPipeIsNonBlockingAndReapsChildCleanly) {
    TempDir tmp;
    tmp.createFile("hello.py",
        "import sys\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\nHello from Python CGI!')\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/hello.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_RUNNING);
    ASSERT_TRUE(executor.getPipeFd() >= 0);
    ASSERT_TRUE(executor.getPid() > 0);

    // Subject IV.1: Pipe FD must be configured non-blocking (O_NONBLOCK)
    int flags = fcntl(executor.getPipeFd(), F_GETFL, 0);
    ASSERT_NE(flags, -1);
    ASSERT_TRUE((flags & O_NONBLOCK) != 0);

    pid_t childPid = executor.getPid();
    ASSERT_TRUE(driveExecutorWithPoll(executor));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_COMPLETE);
    ASSERT_EQ(executor.getPipeFd(), -1);
    ASSERT_EQ(executor.getPid(), -1);

    // Child process must already be reaped (no zombie process left behind)
    int status = 0;
    pid_t waitRes = waitpid(childPid, &status, WNOHANG);
    ASSERT_EQ(waitRes, -1);
    ASSERT_EQ(errno, ECHILD);

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(cgiResp.getStatusCode(), 200);
    ASSERT_EQ(vecToStr(cgiResp.getBody()), "Hello from Python CGI!");
}

// =============================================================================
// 4. Working Directory for Relative Path File Access (Subject IV.3)
// =============================================================================

TEST(CgiExecutor_TestRunsInScriptDirectoryForRelativeFileAccess) {
    // Subject IV.3: "The CGI should be run in the correct directory for relative path file access."
    TempDir tmp;
    tmp.createSubDir("nested");
    tmp.createSubDir("nested/app");
    tmp.createFile("nested/app/config_data.txt", "secret_relative_value=42", 0644);
    tmp.createFile("nested/app/reader.py",
        "import sys\n"
        "with open('./config_data.txt', 'r') as f:\n"
        "    data = f.read()\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\n' + data)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/nested/app/reader.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(vecToStr(cgiResp.getBody()), "secret_relative_value=42");
}

// =============================================================================
// 5. Environment Variables & Client Arguments in CGI Process (Subject IV.3 & RFC 3875)
// =============================================================================

TEST(CgiExecutor_TestEnvironmentVariablesAndArgumentsPassedToChild) {
    // Subject IV.3: "Have a careful look at the environment variables involved in the web
    // server-CGI communication. The full request and arguments provided by the client must
    // be available to the CGI."
    TempDir tmp;
    tmp.createFile("env_dump.py",
        "import os, sys\n"
        "keys = [\n"
        "    'GATEWAY_INTERFACE', 'REQUEST_METHOD', 'QUERY_STRING',\n"
        "    'SCRIPT_NAME', 'PATH_INFO', 'SERVER_NAME', 'SERVER_PORT',\n"
        "    'SERVER_PROTOCOL', 'REMOTE_ADDR', 'HTTP_USER_AGENT',\n"
        "    'HTTP_COOKIE', 'HTTP_X_CUSTOM_TOKEN'\n"
        "]\n"
        "lines = [k + '=' + os.environ.get(k, '<MISSING>') for k in keys]\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\n' + '\\n'.join(lines))\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/env_dump.py/extra/path?search=cgi&page=2");
    req.setVersion("HTTP/1.1");
    req.setRemoteAddr("10.0.0.42");
    req.addHeader("Host", "localhost:8080");
    req.addHeader("User-Agent", "WebservTester/2.0");
    req.addHeader("Cookie", "sid=xyz987");
    req.addHeader("X-Custom-Token", "alpha-beta");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    std::string body = vecToStr(cgiResp.getBody());

    ASSERT_TRUE(body.find("GATEWAY_INTERFACE=CGI/1.1") != std::string::npos);
    ASSERT_TRUE(body.find("REQUEST_METHOD=GET") != std::string::npos);
    ASSERT_TRUE(body.find("QUERY_STRING=search=cgi&page=2") != std::string::npos);
    ASSERT_TRUE(body.find("SCRIPT_NAME=/cgi-bin/env_dump.py") != std::string::npos);
    ASSERT_TRUE(body.find("PATH_INFO=/extra/path") != std::string::npos);
    ASSERT_TRUE(body.find("SERVER_NAME=localhost") != std::string::npos);
    ASSERT_TRUE(body.find("SERVER_PORT=8080") != std::string::npos);
    ASSERT_TRUE(body.find("SERVER_PROTOCOL=HTTP/1.1") != std::string::npos);
    ASSERT_TRUE(body.find("REMOTE_ADDR=10.0.0.42") != std::string::npos);
    ASSERT_TRUE(body.find("HTTP_USER_AGENT=WebservTester/2.0") != std::string::npos);
    ASSERT_TRUE(body.find("HTTP_COOKIE=sid=xyz987") != std::string::npos);
    ASSERT_TRUE(body.find("HTTP_X_CUSTOM_TOKEN=alpha-beta") != std::string::npos);
}

// =============================================================================
// 6. Request Body via STDIN & EOF Semantics (Subject IV.3 & RFC 3875)
// =============================================================================

TEST(CgiExecutor_TestPostBodyDeliveredOnStdinUntilEOF) {
    TempDir tmp;
    tmp.createFile("echo_body.py",
        "import os, sys\n"
        "body = sys.stdin.read()\n"
        "cl = os.environ.get('CONTENT_LENGTH', '')\n"
        "ct = os.environ.get('CONTENT_TYPE', '')\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\n')\n"
        "sys.stdout.write('CL=' + cl + ';CT=' + ct + ';BODY=' + body)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("POST");
    req.setUri("/cgi-bin/echo_body.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");
    req.addHeader("Content-Type", "application/x-www-form-urlencoded");
    std::string payload = "field1=value1&field2=value2";
    req.addHeader("Content-Length", "27");
    req.appendBody(payload.c_str(), payload.size());

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(vecToStr(cgiResp.getBody()),
              "CL=27;CT=application/x-www-form-urlencoded;BODY=field1=value1&field2=value2");
}

TEST(CgiExecutor_TestGetWithEmptyBodyProvidesImmediateEOFOnStdin) {
    // A CGI script reading stdin until EOF on a GET request must immediately see EOF (0 bytes)
    TempDir tmp;
    tmp.createFile("read_stdin_eof.py",
        "import sys\n"
        "data = sys.stdin.read()\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\nLEN=' + str(len(data)))\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/read_stdin_eof.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(vecToStr(cgiResp.getBody()), "LEN=0");
}

TEST(CgiExecutor_TestBinaryStdinAndBinaryStdoutPreserved) {
    TempDir tmp;
    tmp.createFile("binary_echo.py",
        "import sys\n"
        "raw = sys.stdin.buffer.read()\n"
        "sys.stdout.buffer.write(b'Content-Type: application/octet-stream\\r\\n\\r\\n' + raw)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("POST");
    req.setUri("/cgi-bin/binary_echo.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");
    req.addHeader("Content-Type", "application/octet-stream");
    const char binaryData[] = {'\x00', '\x01', '\x02', 'W', 'E', 'B', '\x00', '\xFF', '\x80', '\x00'};
    req.addHeader("Content-Length", "10");
    req.appendBody(binaryData, sizeof(binaryData));

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(cgiResp.getBody().size(), sizeof(binaryData));
    ASSERT_EQ(std::memcmp(&cgiResp.getBody()[0], binaryData, sizeof(binaryData)), 0);
}

// =============================================================================
// 7. Large CGI Output Delimited by EOF Without Content-Length (Subject IV.3)
// =============================================================================

TEST(CgiExecutor_TestLargeOutputDelimitedByEOFMultiRead) {
    // Subject IV.3: "If no content_length is returned from the CGI, EOF will mark the end of the returned data."
    TempDir tmp;
    tmp.createFile("large_output.py",
        "import sys\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\n')\n"
        "sys.stdout.write('A' * 100000)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/large_output.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(cgiResp.getBody().size(), 100000u);
    ASSERT_EQ(cgiResp.getBody()[0], 'A');
    ASSERT_EQ(cgiResp.getBody()[99999], 'A');
}

// =============================================================================
// 8. Multiple CGI Types Support (Subject IV.3 & Bonus VI)
// =============================================================================

TEST(CgiExecutor_TestShellScriptCgiExecution) {
    TempDir tmp;
    tmp.createFile("info.sh",
        "#!/bin/sh\n"
        "printf 'Content-Type: text/plain\\r\\n\\r\\n'\n"
        "printf 'SHELL_CGI:%s:%s' \"$REQUEST_METHOD\" \"$QUERY_STRING\"\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/info.sh?mode=bonus");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_TRUE(driveExecutorWithPoll(executor));

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(vecToStr(cgiResp.getBody()), "SHELL_CGI:GET:mode=bonus");
}

// =============================================================================
// 9. Error Handling: Non-Zero Exit, Signal Crash, Timeout & Cleanup (502, 504)
// =============================================================================

TEST(CgiExecutor_TestScriptNonZeroExitCodeReturns502) {
    TempDir tmp;
    tmp.createFile("fail.py",
        "import sys\n"
        "sys.exit(1)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/fail.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_FALSE(driveExecutorWithPoll(executor));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 502);
}

TEST(CgiExecutor_TestScriptKilledBySignalReturns502) {
    TempDir tmp;
    tmp.createFile("crash.py",
        "import os, signal\n"
        "os.kill(os.getpid(), signal.SIGSEGV)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/crash.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    ASSERT_FALSE(driveExecutorWithPoll(executor));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 502);
}

TEST(CgiExecutor_TestTimeoutDetectionAndHandleTimeoutReturns504) {
    // Subject IV.1: "A request to your server should never hang indefinitely."
    TempDir tmp;
    tmp.createFile("hang.py",
        "import time\n"
        "time.sleep(30)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/hang.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    pid_t childPid = executor.getPid();
    ASSERT_TRUE(childPid > 0);

    // With a large threshold (60s), timeout is not yet reached
    ASSERT_FALSE(executor.hasTimedOut(60));
    // With a 0-second threshold, timeout is immediately detected
    ASSERT_TRUE(executor.hasTimedOut(0));

    executor.handleTimeout();
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 504);
    ASSERT_EQ(executor.getPipeFd(), -1);
    ASSERT_EQ(executor.getPid(), -1);

    // Verify hanging child was killed and reaped (no zombie left behind)
    int status = 0;
    ASSERT_EQ(waitpid(childPid, &status, WNOHANG), -1);
    ASSERT_EQ(errno, ECHILD);
}

TEST(CgiExecutor_TestChildClosesStdoutBeforeProcessExits) {
    // Edge case: CGI script closes stdout (triggering EOF on the pipe) and performs
    // brief cleanup before exiting. reapChild() must wait for exit status and not leave a zombie.
    TempDir tmp;
    tmp.createFile("early_close.py",
        "import os, sys, time\n"
        "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\nEarlyCloseOK')\n"
        "sys.stdout.flush()\n"
        "os.close(1)\n"
        "time.sleep(0.05)\n"
        "sys.exit(0)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/early_close.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    pid_t childPid = executor.getPid();
    ASSERT_TRUE(driveExecutorWithPoll(executor));
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_COMPLETE);

    CgiResponse cgiResp;
    ASSERT_TRUE(cgiResp.parse(executor.getOutputBuffer()));
    ASSERT_EQ(vecToStr(cgiResp.getBody()), "EarlyCloseOK");

    int status = 0;
    ASSERT_EQ(waitpid(childPid, &status, WNOHANG), -1);
    ASSERT_EQ(errno, ECHILD);
}

TEST(CgiExecutor_TestReadOutputErrorKillsChildAndSets500) {
    TempDir tmp;
    tmp.createFile("silent_wait.py",
        "import time\n"
        "time.sleep(30)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/silent_wait.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    pid_t childPid = executor.getPid();
    ASSERT_TRUE(childPid > 0);

    // Calling readOutput() when no data is available on the non-blocking pipe causes read() < 0,
    // which must clean up the pipe, terminate/reap the child, and transition to CGI_ERROR (500).
    ASSERT_FALSE(executor.readOutput());
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_ERROR);
    ASSERT_EQ(executor.getErrorCode(), 500);
    ASSERT_EQ(executor.getPipeFd(), -1);
    ASSERT_EQ(executor.getPid(), -1);

    int status = 0;
    ASSERT_EQ(waitpid(childPid, &status, WNOHANG), -1);
    ASSERT_EQ(errno, ECHILD);
}

TEST(CgiExecutor_TestCleanupWhileRunningKillsAndReapsChild) {
    TempDir tmp;
    tmp.createFile("slow.py",
        "import time\n"
        "time.sleep(30)\n"
    );

    ServerConfig srv = makeServerConfig();
    LocationConfig loc = makeCgiLocation(tmp.path);

    HttpRequest req;
    req.setMethod("GET");
    req.setUri("/cgi-bin/slow.py");
    req.setVersion("HTTP/1.1");
    req.addHeader("Host", "localhost");

    CgiRequest cgiReq(srv, loc, req);
    CgiExecutor executor;

    ASSERT_TRUE(executor.start(cgiReq));
    pid_t childPid = executor.getPid();
    ASSERT_TRUE(childPid > 0);

    executor.cleanup();
    ASSERT_EQ(executor.getState(), CgiExecutor::CGI_IDLE);
    ASSERT_EQ(executor.getPipeFd(), -1);
    ASSERT_EQ(executor.getPid(), -1);

    int status = 0;
    ASSERT_EQ(waitpid(childPid, &status, WNOHANG), -1);
    ASSERT_EQ(errno, ECHILD);
}

