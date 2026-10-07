#include "tiny_test.hpp"
#include "ConfigParser.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {

// RAII helper that creates a temporary configuration file and removes it on destruction
class TempConfigFile {
private:
    std::string _path;

    TempConfigFile(const TempConfigFile&);
    TempConfigFile& operator=(const TempConfigFile&);

public:
    explicit TempConfigFile(const std::string& content) {
        char tmpl[] = "/tmp/webserv_config_test_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd != -1) {
            _path = tmpl;
            close(fd);
            std::ofstream ofs(_path.c_str(), std::ios::out | std::ios::trunc);
            if (ofs.is_open()) {
                ofs << content;
                ofs.close();
            }
        }
    }

    ~TempConfigFile() {
        if (!_path.empty()) {
            std::remove(_path.c_str());
        }
    }

    const std::string& path() const {
        return _path;
    }
};

std::vector<ServerConfig> parseConfigString(const std::string& content) {
    TempConfigFile tmp(content);
    ConfigParser parser(tmp.path());
    return parser.parse();
}

} // namespace

// =============================================================================
// 1. Constructors, Orthodox Canonical Form & File I/O
// =============================================================================

TEST(ConfigParser_TestDefaultConstructorThrowsOnParse) {
    ConfigParser parser;

    // Default constructor leaves file path empty, so parse() must fail to open file
    ASSERT_THROWS(parser.parse(), std::runtime_error);
}

TEST(ConfigParser_TestParameterizedConstructorAndParse) {
    TempConfigFile tmp(
        "server {\n"
        "    listen 8080;\n"
        "}\n"
    );

    ConfigParser parser(tmp.path());
    std::vector<ServerConfig> servers = parser.parse();

    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getHost(), "0.0.0.0");
    ASSERT_EQ(servers[0].getPort(), 8080);
}

TEST(ConfigParser_TestCopyConstructor) {
    TempConfigFile tmp(
        "server {\n"
        "    listen 127.0.0.1:9090;\n"
        "    server_name copy.local;\n"
        "}\n"
    );

    ConfigParser original(tmp.path());
    std::vector<ServerConfig> origServers = original.parse();

    ConfigParser copy(original);
    ASSERT_EQ(origServers.size(), 1u);
    ASSERT_EQ(origServers[0].getHost(), "127.0.0.1");
    ASSERT_EQ(origServers[0].getPort(), 9090);

    // Also test copying before parse() is invoked
    ConfigParser freshOriginal(tmp.path());
    ConfigParser freshCopy(freshOriginal);
    std::vector<ServerConfig> copyServers = freshCopy.parse();

    ASSERT_EQ(copyServers.size(), 1u);
    ASSERT_EQ(copyServers[0].getHost(), "127.0.0.1");
    ASSERT_EQ(copyServers[0].getPort(), 9090);
    ASSERT_EQ(copyServers[0].getServerNames().size(), 1u);
    ASSERT_EQ(copyServers[0].getServerNames()[0], "copy.local");
}

TEST(ConfigParser_TestAssignmentOperator) {
    TempConfigFile tmp1(
        "server {\n"
        "    listen 8081;\n"
        "    server_name first.local;\n"
        "}\n"
    );
    TempConfigFile tmp2(
        "server {\n"
        "    listen 8082;\n"
        "    server_name second.local;\n"
        "}\n"
    );

    ConfigParser parser1(tmp1.path());
    ConfigParser parser2(tmp2.path());

    parser2 = parser1;

    // Self-assignment safety check
    parser2 = parser2;

    std::vector<ServerConfig> servers = parser2.parse();
    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getPort(), 8081);
    ASSERT_EQ(servers[0].getServerNames().size(), 1u);
    ASSERT_EQ(servers[0].getServerNames()[0], "first.local");
}

TEST(ConfigParser_TestFileNotFoundThrows) {
    ConfigParser parser("/tmp/non_existent_webserv_config_file_424242.conf");
    ASSERT_THROWS(parser.parse(), std::runtime_error);
}

TEST(ConfigParser_TestEmptyFileThrows) {
    ASSERT_THROWS(parseConfigString(""), std::runtime_error);
}

TEST(ConfigParser_TestWhitespaceOnlyFileThrows) {
    ASSERT_THROWS(parseConfigString("   \n\t  \n  \t \n"), std::runtime_error);
}

TEST(ConfigParser_TestCommentsOnlyFileThrows) {
    std::string config =
        "# Full line comment\n"
        "   # Indented comment\n"
        "# server { listen 8080; }\n";
    ASSERT_THROWS(parseConfigString(config), std::runtime_error);
}

// =============================================================================
// 2. Comments, Whitespace & Tokenization
// =============================================================================

TEST(ConfigParser_TestCommentsInlineAndFullLine) {
    std::string config =
        "# Leading configuration comment\n"
        "server { # comment after server brace\n"
        "    # comment before listen\n"
        "    listen 127.0.0.1:8080; # inline comment after semicolon\n"
        "    server_name example.com; # another comment\n"
        "    location / { # location comment\n"
        "        root /var/www;# comment touching semicolon\n"
        "    } # end of location\n"
        "} # end of server\n"
        "# Trailing comment\n";

    std::vector<ServerConfig> servers = parseConfigString(config);
    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getHost(), "127.0.0.1");
    ASSERT_EQ(servers[0].getPort(), 8080);
    ASSERT_EQ(servers[0].getServerNames().size(), 1u);
    ASSERT_EQ(servers[0].getServerNames()[0], "example.com");
    ASSERT_EQ(servers[0].getLocations().size(), 1u);
    ASSERT_EQ(servers[0].getLocations()[0].getRoot(), "/var/www");
}

TEST(ConfigParser_TestCommentHidingSemicolonThrows) {
    // The semicolon is commented out, so verification must fail
    std::string config =
        "server {\n"
        "    listen 8080 # ; semicolon is inside comment\n"
        "}\n";
    ASSERT_THROWS(parseConfigString(config), std::runtime_error);
}

TEST(ConfigParser_TestNoSpacesAroundBracesAndSemicolons) {
    // Tokenizer must pad '{', '}', and ';' even when tightly packed
    std::string config =
        "server{listen 8085;server_name tight.com;client_max_body_size 2M;"
        "location /{root /www;index main.html;autoindex on;}}";

    std::vector<ServerConfig> servers = parseConfigString(config);
    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getPort(), 8085);
    ASSERT_EQ(servers[0].getServerNames().size(), 1u);
    ASSERT_EQ(servers[0].getServerNames()[0], "tight.com");
    ASSERT_EQ(servers[0].getClientMaxBodySize(), 2097152u);
    ASSERT_EQ(servers[0].getLocations().size(), 1u);
    ASSERT_EQ(servers[0].getLocations()[0].getPath(), "/");
    ASSERT_EQ(servers[0].getLocations()[0].getRoot(), "/www");
    ASSERT_EQ(servers[0].getLocations()[0].getIndex().size(), 1u);
    ASSERT_EQ(servers[0].getLocations()[0].getIndex()[0], "main.html");
    ASSERT_TRUE(servers[0].getLocations()[0].getAutoindex());
}

TEST(ConfigParser_TestTabsAndMultiLineDirectives) {
    std::string config =
        "\t\tserver\n"
        "{\n"
        "\tlisten\n"
        "\t\t9000\n"
        "\t\t;\n"
        "\tserver_name\n"
        "\t\ta.com\n"
        "\t\tb.com\n"
        "\t\t;\n"
        "}\n";

    std::vector<ServerConfig> servers = parseConfigString(config);
    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getPort(), 9000);
    ASSERT_EQ(servers[0].getServerNames().size(), 2u);
    ASSERT_EQ(servers[0].getServerNames()[0], "a.com");
    ASSERT_EQ(servers[0].getServerNames()[1], "b.com");
}

// =============================================================================
// 3. Server Block Structure & Multiple Servers
// =============================================================================

TEST(ConfigParser_TestEmptyServerBlockDefaults) {
    std::vector<ServerConfig> servers = parseConfigString("server { }");

    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getHost(), "0.0.0.0");
    ASSERT_EQ(servers[0].getPort(), 8080);
    ASSERT_EQ(servers[0].getPortString(), "8080");
    ASSERT_EQ(servers[0].getClientMaxBodySize(), 1048576u);
    ASSERT_TRUE(servers[0].getServerNames().empty());
    ASSERT_TRUE(servers[0].getErrorPages().empty());
    ASSERT_TRUE(servers[0].getLocations().empty());
}

TEST(ConfigParser_TestMultipleServerBlocks) {
    std::string config =
        "server {\n"
        "    listen 127.0.0.1:8080;\n"
        "    server_name one.example.com;\n"
        "    client_max_body_size 1K;\n"
        "}\n"
        "server {\n"
        "    listen 127.0.0.1:8081;\n"
        "    server_name two.example.com;\n"
        "    client_max_body_size 2M;\n"
        "}\n"
        "server {\n"
        "    listen 0.0.0.0:9090;\n"
        "    server_name three.example.com;\n"
        "}\n";

    std::vector<ServerConfig> servers = parseConfigString(config);
    ASSERT_EQ(servers.size(), 3u);

    ASSERT_EQ(servers[0].getHost(), "127.0.0.1");
    ASSERT_EQ(servers[0].getPort(), 8080);
    ASSERT_EQ(servers[0].getServerNames()[0], "one.example.com");
    ASSERT_EQ(servers[0].getClientMaxBodySize(), 1024u);

    ASSERT_EQ(servers[1].getHost(), "127.0.0.1");
    ASSERT_EQ(servers[1].getPort(), 8081);
    ASSERT_EQ(servers[1].getServerNames()[0], "two.example.com");
    ASSERT_EQ(servers[1].getClientMaxBodySize(), 2097152u);

    ASSERT_EQ(servers[2].getHost(), "0.0.0.0");
    ASSERT_EQ(servers[2].getPort(), 9090);
    ASSERT_EQ(servers[2].getServerNames()[0], "three.example.com");
    ASSERT_EQ(servers[2].getClientMaxBodySize(), 1048576u);
}

TEST(ConfigParser_TestTopLevelInvalidTokenThrows) {
    ASSERT_THROWS(parseConfigString("http { server { listen 8080; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("listen 8080;"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("{ listen 8080; }"), std::runtime_error);
}

TEST(ConfigParser_TestTrailingTokensAfterServerBlockThrows) {
    ASSERT_THROWS(parseConfigString("server { listen 8080; } extra_token"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 8080; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 8080; } ;"), std::runtime_error);
}

TEST(ConfigParser_TestServerBlockMissingBracesThrows) {
    // Missing opening brace
    ASSERT_THROWS(parseConfigString("server"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server listen 8080; }"), std::runtime_error);

    // Missing closing brace
    ASSERT_THROWS(parseConfigString("server {"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 8080;"), std::runtime_error);
}

TEST(ConfigParser_TestUnknownServerDirectiveThrows) {
    ASSERT_THROWS(parseConfigString("server { root /var/www; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { autoindex on; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { unknown_directive 123; }"), std::runtime_error);
}

// =============================================================================
// 4. Listen Directive (Host & Port Parsing and Validation)
// =============================================================================

TEST(ConfigParser_TestListenPortOnly) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    listen 4242;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getHost(), "0.0.0.0");
    ASSERT_EQ(servers[0].getPort(), 4242);
    ASSERT_EQ(servers[0].getPortString(), "4242");
}

TEST(ConfigParser_TestListenHostAndPort) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    listen 192.168.1.50:8000;\n"
        "}\n"
        "server {\n"
        "    listen localhost:3000;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 2u);
    ASSERT_EQ(servers[0].getHost(), "192.168.1.50");
    ASSERT_EQ(servers[0].getPort(), 8000);
    ASSERT_EQ(servers[0].getPortString(), "8000");

    ASSERT_EQ(servers[1].getHost(), "localhost");
    ASSERT_EQ(servers[1].getPort(), 3000);
    ASSERT_EQ(servers[1].getPortString(), "3000");
}

TEST(ConfigParser_TestListenHostOnlyDefaultPort80) {
    // IP address with dots and no colon defaults port to 80
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    listen 127.0.0.1;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getHost(), "127.0.0.1");
    ASSERT_EQ(servers[0].getPort(), 80);
    ASSERT_EQ(servers[0].getPortString(), "80");
}

TEST(ConfigParser_TestListenPortBoundaries) {
    // Minimum valid port (1) and maximum valid port (65535)
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    listen 1;\n"
        "}\n"
        "server {\n"
        "    listen 65535;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 2u);
    ASSERT_EQ(servers[0].getPort(), 1);
    ASSERT_EQ(servers[1].getPort(), 65535);
}

TEST(ConfigParser_TestListenInvalidPortThrows) {
    // Port 0 (out of range)
    ASSERT_THROWS(parseConfigString("server { listen 0; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 127.0.0.1:0; }"), std::runtime_error);

    // Port > 65535 (out of range)
    ASSERT_THROWS(parseConfigString("server { listen 65536; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 70000; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 127.0.0.1:99999; }"), std::runtime_error);

    // Negative port
    ASSERT_THROWS(parseConfigString("server { listen -80; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 127.0.0.1:-8080; }"), std::runtime_error);

    // Non-numeric characters in port
    ASSERT_THROWS(parseConfigString("server { listen abc; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 8080a; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 127.0.0.1:http; }"), std::runtime_error);

    // Empty port after colon
    ASSERT_THROWS(parseConfigString("server { listen 127.0.0.1:; }"), std::runtime_error);

    // Missing argument
    ASSERT_THROWS(parseConfigString("server { listen ; }"), std::runtime_error);
}

TEST(ConfigParser_TestListenMissingSemicolonOrExtraArgsThrows) {
    ASSERT_THROWS(parseConfigString("server { listen 8080 }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen 8080 8081; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { listen"), std::runtime_error);
}

// =============================================================================
// 5. Server Name Directive (server_name)
// =============================================================================

TEST(ConfigParser_TestServerNameSingleAndMultiple) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    server_name example.com www.example.com api.example.com;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    const std::vector<std::string>& names = servers[0].getServerNames();
    ASSERT_EQ(names.size(), 3u);
    ASSERT_EQ(names[0], "example.com");
    ASSERT_EQ(names[1], "www.example.com");
    ASSERT_EQ(names[2], "api.example.com");
}

TEST(ConfigParser_TestServerNameMultipleDirectivesAccumulate) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    server_name first.org;\n"
        "    server_name second.org third.org;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    const std::vector<std::string>& names = servers[0].getServerNames();
    ASSERT_EQ(names.size(), 3u);
    ASSERT_EQ(names[0], "first.org");
    ASSERT_EQ(names[1], "second.org");
    ASSERT_EQ(names[2], "third.org");
}

TEST(ConfigParser_TestServerNameInvalidThrows) {
    // Empty server_name directive without arguments
    ASSERT_THROWS(parseConfigString("server { server_name ; }"), std::runtime_error);

    // Missing semicolon (hits '}' and EOF)
    ASSERT_THROWS(parseConfigString("server { server_name example.com }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { server_name"), std::runtime_error);
}

// =============================================================================
// 6. Client Max Body Size Directive (client_max_body_size & parseSize)
// =============================================================================

TEST(ConfigParser_TestClientMaxBodySizeBytesAndUnits) {
    // Raw bytes
    std::vector<ServerConfig> s0 = parseConfigString("server { client_max_body_size 0; }");
    ASSERT_EQ(s0[0].getClientMaxBodySize(), 0u);

    std::vector<ServerConfig> sBytes = parseConfigString("server { client_max_body_size 4096; }");
    ASSERT_EQ(sBytes[0].getClientMaxBodySize(), 4096u);

    // Kilobytes (K and k)
    std::vector<ServerConfig> sUpperK = parseConfigString("server { client_max_body_size 16K; }");
    ASSERT_EQ(sUpperK[0].getClientMaxBodySize(), 16u * 1024u);

    std::vector<ServerConfig> sLowerK = parseConfigString("server { client_max_body_size 32k; }");
    ASSERT_EQ(sLowerK[0].getClientMaxBodySize(), 32u * 1024u);

    // Megabytes (M and m)
    std::vector<ServerConfig> sUpperM = parseConfigString("server { client_max_body_size 10M; }");
    ASSERT_EQ(sUpperM[0].getClientMaxBodySize(), 10u * 1048576u);

    std::vector<ServerConfig> sLowerM = parseConfigString("server { client_max_body_size 5m; }");
    ASSERT_EQ(sLowerM[0].getClientMaxBodySize(), 5u * 1048576u);

    // Gigabytes (G and g)
    std::vector<ServerConfig> sUpperG = parseConfigString("server { client_max_body_size 1G; }");
    ASSERT_EQ(sUpperG[0].getClientMaxBodySize(), 1073741824u);

    std::vector<ServerConfig> sLowerG = parseConfigString("server { client_max_body_size 2g; }");
    ASSERT_EQ(sLowerG[0].getClientMaxBodySize(), 2u * 1073741824u);
}

TEST(ConfigParser_TestClientMaxBodySizeInvalidThrows) {
    // Non-numeric strings
    ASSERT_THROWS(parseConfigString("server { client_max_body_size abc; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 100abc; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 12abcM; }"), std::runtime_error);

    // Unit suffix without numeric prefix
    ASSERT_THROWS(parseConfigString("server { client_max_body_size K; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size m; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size G; }"), std::runtime_error);

    // Unsupported unit suffixes
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 10B; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 10MB; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 1T; }"), std::runtime_error);

    // Missing value or semicolon
    ASSERT_THROWS(parseConfigString("server { client_max_body_size ; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 10M }"), std::runtime_error);
}

TEST(ConfigParser_TestClientMaxBodySizeOverflowThrows) {
    // Multiplying huge number by G/M/K exceeds size_t maximum
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 18446744073709551615G; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 18446744073709551615M; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { client_max_body_size 18446744073709551615K; }"), std::runtime_error);
}

// =============================================================================
// 7. Error Page Directive (error_page)
// =============================================================================

TEST(ConfigParser_TestErrorPageSingleCode) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    error_page 404 /errors/404.html;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    const std::map<int, std::string>& errorPages = servers[0].getErrorPages();
    ASSERT_EQ(errorPages.size(), 1u);
    std::map<int, std::string>::const_iterator it = errorPages.find(404);
    ASSERT_TRUE(it != errorPages.end());
    ASSERT_EQ(it->second, "/errors/404.html");
}

TEST(ConfigParser_TestErrorPageMultipleCodesSingleLine) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    error_page 500 502 503 504 /errors/50x.html;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    const std::map<int, std::string>& errorPages = servers[0].getErrorPages();
    ASSERT_EQ(errorPages.size(), 4u);
    ASSERT_EQ(errorPages.find(500)->second, "/errors/50x.html");
    ASSERT_EQ(errorPages.find(502)->second, "/errors/50x.html");
    ASSERT_EQ(errorPages.find(503)->second, "/errors/50x.html");
    ASSERT_EQ(errorPages.find(504)->second, "/errors/50x.html");
}

TEST(ConfigParser_TestErrorPageMultipleDirectivesAndOverwrite) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    error_page 400 /errors/400.html;\n"
        "    error_page 404 /errors/old_404.html;\n"
        "    error_page 404 /errors/new_404.html;\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    const std::map<int, std::string>& errorPages = servers[0].getErrorPages();
    ASSERT_EQ(errorPages.size(), 2u);
    ASSERT_EQ(errorPages.find(400)->second, "/errors/400.html");
    ASSERT_EQ(errorPages.find(404)->second, "/errors/new_404.html");
}

TEST(ConfigParser_TestErrorPageInvalidThrows) {
    // No arguments
    ASSERT_THROWS(parseConfigString("server { error_page ; }"), std::runtime_error);

    // Only one argument (missing path or missing status code)
    ASSERT_THROWS(parseConfigString("server { error_page 404; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { error_page /404.html; }"), std::runtime_error);

    // Non-numeric status codes
    ASSERT_THROWS(parseConfigString("server { error_page abc /404.html; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { error_page 404a /404.html; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { error_page -404 /404.html; }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { error_page 400 /first.html /second.html; }"), std::runtime_error);

    // Missing semicolon
    ASSERT_THROWS(parseConfigString("server { error_page 404 /404.html }"), std::runtime_error);
}

// =============================================================================
// 8. Location Block & Directives
// =============================================================================

TEST(ConfigParser_TestLocationDefaultValues) {
    // When no 'index' directive is provided, LocationConfig gets "index.html" by default
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location / {\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers.size(), 1u);
    ASSERT_EQ(servers[0].getLocations().size(), 1u);

    const LocationConfig& loc = servers[0].getLocations()[0];
    ASSERT_EQ(loc.getPath(), "/");
    ASSERT_EQ(loc.getRoot(), "");
    ASSERT_EQ(loc.getIndex().size(), 1u);
    ASSERT_EQ(loc.getIndex()[0], "index.html");
    ASSERT_TRUE(loc.getAllowedMethods().empty());
    ASSERT_FALSE(loc.getAutoindex());
    ASSERT_TRUE(loc.getCgiHandlers().empty());
    ASSERT_EQ(loc.getUploadStore(), "");
    ASSERT_EQ(loc.getRedirect().first, 0);
    ASSERT_EQ(loc.getRedirect().second, "");
}

TEST(ConfigParser_TestLocationRootDirective) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /static {\n"
        "        root /var/www/static;\n"
        "    }\n"
        "}\n"
    );

    const LocationConfig& loc = servers[0].getLocations()[0];
    ASSERT_EQ(loc.getPath(), "/static");
    ASSERT_EQ(loc.getRoot(), "/var/www/static");

    // Missing semicolon after root
    ASSERT_THROWS(parseConfigString("server { location / { root /var/www } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationIndexSingleAndMultiple) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /single {\n"
        "        index home.html;\n"
        "    }\n"
        "    location /multi {\n"
        "        index index.php index.html default.htm;\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers[0].getLocations().size(), 2u);

    // Explicit index replaces default "index.html"
    const LocationConfig& locSingle = servers[0].getLocations()[0];
    ASSERT_EQ(locSingle.getIndex().size(), 1u);
    ASSERT_EQ(locSingle.getIndex()[0], "home.html");

    const LocationConfig& locMulti = servers[0].getLocations()[1];
    ASSERT_EQ(locMulti.getIndex().size(), 3u);
    ASSERT_EQ(locMulti.getIndex()[0], "index.php");
    ASSERT_EQ(locMulti.getIndex()[1], "index.html");
    ASSERT_EQ(locMulti.getIndex()[2], "default.htm");

    // Empty index directive must throw
    ASSERT_THROWS(parseConfigString("server { location / { index ; } }"), std::runtime_error);
    // Missing semicolon
    ASSERT_THROWS(parseConfigString("server { location / { index home.html } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationAllowMethodsValid) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /readonly {\n"
        "        allow_methods GET;\n"
        "    }\n"
        "    location /full {\n"
        "        allow_methods GET POST DELETE;\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers[0].getLocations().size(), 2u);

    const std::vector<std::string>& methodsRead = servers[0].getLocations()[0].getAllowedMethods();
    ASSERT_EQ(methodsRead.size(), 1u);
    ASSERT_EQ(methodsRead[0], "GET");

    const std::vector<std::string>& methodsFull = servers[0].getLocations()[1].getAllowedMethods();
    ASSERT_EQ(methodsFull.size(), 3u);
    ASSERT_EQ(methodsFull[0], "GET");
    ASSERT_EQ(methodsFull[1], "POST");
    ASSERT_EQ(methodsFull[2], "DELETE");
}

TEST(ConfigParser_TestLocationAllowMethodsInvalidThrows) {
    // Empty allow_methods
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods ; } }"), std::runtime_error);

    // Unsupported or lowercase HTTP methods
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods PUT; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods HEAD; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods OPTIONS; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods PATCH; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods get; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods GET INVALID; } }"), std::runtime_error);

    // Missing semicolon
    ASSERT_THROWS(parseConfigString("server { location / { allow_methods GET POST } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationAutoindexOnAndOff) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /files {\n"
        "        autoindex on;\n"
        "    }\n"
        "    location /private {\n"
        "        autoindex off;\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers[0].getLocations().size(), 2u);
    ASSERT_TRUE(servers[0].getLocations()[0].getAutoindex());
    ASSERT_FALSE(servers[0].getLocations()[1].getAutoindex());
}

TEST(ConfigParser_TestLocationAutoindexInvalidThrows) {
    ASSERT_THROWS(parseConfigString("server { location / { autoindex true; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { autoindex false; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { autoindex yes; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { autoindex 1; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { autoindex ON; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { autoindex ; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { autoindex on } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationUploadStore) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /upload {\n"
        "        upload_store /var/www/uploads;\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers[0].getLocations().size(), 1u);
    ASSERT_EQ(servers[0].getLocations()[0].getUploadStore(), "/var/www/uploads");

    // Missing semicolon
    ASSERT_THROWS(parseConfigString("server { location / { upload_store /tmp/uploads } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationCgiPassSingleAndMultiple) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /cgi-bin {\n"
        "        cgi_pass .py /usr/bin/python3;\n"
        "        cgi_pass .php /usr/bin/php-cgi;\n"
        "        cgi_pass .sh /bin/bash;\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers[0].getLocations().size(), 1u);
    const std::map<std::string, std::string>& cgi = servers[0].getLocations()[0].getCgiHandlers();
    ASSERT_EQ(cgi.size(), 3u);
    ASSERT_EQ(cgi.find(".py")->second, "/usr/bin/python3");
    ASSERT_EQ(cgi.find(".php")->second, "/usr/bin/php-cgi");
    ASSERT_EQ(cgi.find(".sh")->second, "/bin/bash");
}

TEST(ConfigParser_TestLocationCgiPassInvalidThrows) {
    // Missing both arguments
    ASSERT_THROWS(parseConfigString("server { location / { cgi_pass ; } }"), std::runtime_error);

    // Missing executable path argument
    ASSERT_THROWS(parseConfigString("server { location / { cgi_pass .py ; } }"), std::runtime_error);

    // Missing semicolon or extra arguments
    ASSERT_THROWS(parseConfigString("server { location / { cgi_pass .py /usr/bin/python3 } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { cgi_pass .py /usr/bin/python3 extra; } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationReturnValid) {
    std::vector<ServerConfig> servers = parseConfigString(
        "server {\n"
        "    location /old-page {\n"
        "        return 301 /new-page;\n"
        "    }\n"
        "    location /external {\n"
        "        return 302 https://example.org/landing;\n"
        "    }\n"
        "}\n"
    );

    ASSERT_EQ(servers[0].getLocations().size(), 2u);
    ASSERT_EQ(servers[0].getLocations()[0].getRedirect().first, 301);
    ASSERT_EQ(servers[0].getLocations()[0].getRedirect().second, "/new-page");

    ASSERT_EQ(servers[0].getLocations()[1].getRedirect().first, 302);
    ASSERT_EQ(servers[0].getLocations()[1].getRedirect().second, "https://example.org/landing");
}

TEST(ConfigParser_TestLocationReturnInvalidThrows) {
    // Non-numeric status code
    ASSERT_THROWS(parseConfigString("server { location / { return abc /new; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { return 301a /new; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { return -301 /new; } }"), std::runtime_error);

    // Missing arguments
    ASSERT_THROWS(parseConfigString("server { location / { return ; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { return 301 ; } }"), std::runtime_error);

    // Missing semicolon
    ASSERT_THROWS(parseConfigString("server { location / { return 301 /new } }"), std::runtime_error);
}

TEST(ConfigParser_TestLocationSyntaxAndUnknownDirectiveThrows) {
    // Missing location path (opening brace read as path, next token is not '{')
    ASSERT_THROWS(parseConfigString("server { location { root /var/www; } }"), std::runtime_error);

    // Missing opening brace after location path
    ASSERT_THROWS(parseConfigString("server { location / root /var/www; } }"), std::runtime_error);

    // Missing closing brace for location block
    ASSERT_THROWS(parseConfigString("server { location / { root /var/www; }"), std::runtime_error);

    // Server-level or unknown directive inside location block
    ASSERT_THROWS(parseConfigString("server { location / { listen 8080; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { server_name test; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { client_max_body_size 1M; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { error_page 404 /404.html; } }"), std::runtime_error);
    ASSERT_THROWS(parseConfigString("server { location / { unknown_loc_directive val; } }"), std::runtime_error);
}

// =============================================================================
// 9. Complex / Full Realistic Webserv Configurations
// =============================================================================

TEST(ConfigParser_TestFullRealisticMultiServerConfig) {
    std::string config =
        "# Main production virtual host\n"
        "server {\n"
        "    listen 127.0.0.1:8080;\n"
        "    server_name webserv.42.fr www.webserv.42.fr;\n"
        "    client_max_body_size 10M;\n"
        "    error_page 400 403 404 /error_pages/4xx.html;\n"
        "    error_page 500 502 503 504 /error_pages/5xx.html;\n"
        "\n"
        "    location / {\n"
        "        root /var/www/html;\n"
        "        index index.html index.htm;\n"
        "        allow_methods GET;\n"
        "        autoindex off;\n"
        "    }\n"
        "\n"
        "    location /uploads {\n"
        "        root /var/www;\n"
        "        allow_methods GET POST DELETE;\n"
        "        upload_store /var/www/uploads;\n"
        "        autoindex on;\n"
        "    }\n"
        "\n"
        "    location /cgi-bin {\n"
        "        root /var/www/cgi-bin;\n"
        "        allow_methods GET POST;\n"
        "        cgi_pass .py /usr/bin/python3;\n"
        "        cgi_pass .php /usr/bin/php-cgi;\n"
        "    }\n"
        "\n"
        "    location /legacy {\n"
        "        return 301 /;\n"
        "    }\n"
        "}\n"
        "\n"
        "# Secondary API server\n"
        "server {\n"
        "    listen 9090;\n"
        "    server_name api.webserv.42.fr;\n"
        "    client_max_body_size 512K;\n"
        "\n"
        "    location /v1 {\n"
        "        root /var/www/api;\n"
        "        allow_methods GET POST;\n"
        "    }\n"
        "}\n";

    std::vector<ServerConfig> servers = parseConfigString(config);
    ASSERT_EQ(servers.size(), 2u);

    // Verify Server 1
    const ServerConfig& s1 = servers[0];
    ASSERT_EQ(s1.getHost(), "127.0.0.1");
    ASSERT_EQ(s1.getPort(), 8080);
    ASSERT_EQ(s1.getPortString(), "8080");
    ASSERT_EQ(s1.getClientMaxBodySize(), 10u * 1048576u);
    ASSERT_EQ(s1.getServerNames().size(), 2u);
    ASSERT_EQ(s1.getServerNames()[0], "webserv.42.fr");
    ASSERT_EQ(s1.getServerNames()[1], "www.webserv.42.fr");
    ASSERT_EQ(s1.getErrorPages().size(), 7u);
    ASSERT_EQ(s1.getErrorPages().find(404)->second, "/error_pages/4xx.html");
    ASSERT_EQ(s1.getErrorPages().find(502)->second, "/error_pages/5xx.html");
    ASSERT_EQ(s1.getLocations().size(), 4u);

    // Location 0: /
    const LocationConfig& loc0 = s1.getLocations()[0];
    ASSERT_EQ(loc0.getPath(), "/");
    ASSERT_EQ(loc0.getRoot(), "/var/www/html");
    ASSERT_EQ(loc0.getIndex().size(), 2u);
    ASSERT_EQ(loc0.getIndex()[0], "index.html");
    ASSERT_EQ(loc0.getIndex()[1], "index.htm");
    ASSERT_EQ(loc0.getAllowedMethods().size(), 1u);
    ASSERT_EQ(loc0.getAllowedMethods()[0], "GET");
    ASSERT_FALSE(loc0.getAutoindex());

    // Location 1: /uploads (no explicit index -> defaults to "index.html")
    const LocationConfig& loc1 = s1.getLocations()[1];
    ASSERT_EQ(loc1.getPath(), "/uploads");
    ASSERT_EQ(loc1.getRoot(), "/var/www");
    ASSERT_EQ(loc1.getUploadStore(), "/var/www/uploads");
    ASSERT_TRUE(loc1.getAutoindex());
    ASSERT_EQ(loc1.getAllowedMethods().size(), 3u);
    ASSERT_EQ(loc1.getIndex().size(), 1u);
    ASSERT_EQ(loc1.getIndex()[0], "index.html");

    // Location 2: /cgi-bin
    const LocationConfig& loc2 = s1.getLocations()[2];
    ASSERT_EQ(loc2.getPath(), "/cgi-bin");
    ASSERT_EQ(loc2.getCgiHandlers().size(), 2u);
    ASSERT_EQ(loc2.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
    ASSERT_EQ(loc2.getCgiHandlers().find(".php")->second, "/usr/bin/php-cgi");

    // Location 3: /legacy
    const LocationConfig& loc3 = s1.getLocations()[3];
    ASSERT_EQ(loc3.getPath(), "/legacy");
    ASSERT_EQ(loc3.getRedirect().first, 301);
    ASSERT_EQ(loc3.getRedirect().second, "/");

    // Verify Server 2
    const ServerConfig& s2 = servers[1];
    ASSERT_EQ(s2.getHost(), "0.0.0.0");
    ASSERT_EQ(s2.getPort(), 9090);
    ASSERT_EQ(s2.getClientMaxBodySize(), 512u * 1024u);
    ASSERT_EQ(s2.getServerNames().size(), 1u);
    ASSERT_EQ(s2.getServerNames()[0], "api.webserv.42.fr");
    ASSERT_EQ(s2.getLocations().size(), 1u);
    ASSERT_EQ(s2.getLocations()[0].getPath(), "/v1");
    ASSERT_EQ(s2.getLocations()[0].getRoot(), "/var/www/api");
}

