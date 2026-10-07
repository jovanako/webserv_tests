#include "tiny_test.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include <string>
#include <vector>
#include <map>
#include <limits>

// =============================================================================
// 1. Initial State & Orthodox Canonical Form
// =============================================================================

TEST(ServerConfig_TestInitialState) {
    ServerConfig config;

    ASSERT_EQ(config.getHost(), "0.0.0.0");
    ASSERT_EQ(config.getPort(), 8080);
    ASSERT_EQ(config.getPortString(), "8080");
    ASSERT_EQ(config.getClientMaxBodySize(), 1048576u);
    ASSERT_TRUE(config.getServerNames().empty());
    ASSERT_EQ(config.getServerNames().size(), 0u);
    ASSERT_TRUE(config.getErrorPages().empty());
    ASSERT_EQ(config.getErrorPages().size(), 0u);
    ASSERT_TRUE(config.getLocations().empty());
    ASSERT_EQ(config.getLocations().size(), 0u);
}

TEST(ServerConfig_TestCopyConstructorBasic) {
    ServerConfig original;
    original.setHost("127.0.0.1");
    original.setPort(9090);
    original.setClientMaxBodySize(2097152);
    original.addServerName("example.com");
    original.addServerName("www.example.com");
    original.addErrorPage(404, "/errors/404.html");
    original.addErrorPage(500, "/errors/500.html");

    LocationConfig loc;
    loc.setPath("/api");
    loc.setRoot("/var/www/api");
    loc.setAutoindex(true);
    original.addLocation(loc);

    ServerConfig copy(original);

    ASSERT_EQ(copy.getHost(), "127.0.0.1");
    ASSERT_EQ(copy.getPort(), 9090);
    ASSERT_EQ(copy.getPortString(), "9090");
    ASSERT_EQ(copy.getClientMaxBodySize(), 2097152u);

    ASSERT_EQ(copy.getServerNames().size(), 2u);
    ASSERT_EQ(copy.getServerNames()[0], "example.com");
    ASSERT_EQ(copy.getServerNames()[1], "www.example.com");

    ASSERT_EQ(copy.getErrorPages().size(), 2u);
    ASSERT_EQ(copy.getErrorPages().find(404)->second, "/errors/404.html");
    ASSERT_EQ(copy.getErrorPages().find(500)->second, "/errors/500.html");

    ASSERT_EQ(copy.getLocations().size(), 1u);
    ASSERT_EQ(copy.getLocations()[0].getPath(), "/api");
    ASSERT_EQ(copy.getLocations()[0].getRoot(), "/var/www/api");
    ASSERT_TRUE(copy.getLocations()[0].getAutoindex());
}

TEST(ServerConfig_TestCopyConstructorIndependence) {
    ServerConfig original;
    original.setHost("127.0.0.1");
    original.setPort(8000);
    original.setClientMaxBodySize(4096);
    original.addServerName("original.local");
    original.addErrorPage(404, "/404.html");

    LocationConfig loc1;
    loc1.setPath("/");
    loc1.setRoot("/var/www/html");
    original.addLocation(loc1);

    ServerConfig copy(original);

    // Mutate original - copy must remain unaffected
    original.setHost("192.168.1.1");
    original.setPort(9999);
    original.setClientMaxBodySize(8192);
    original.addServerName("mutated.local");
    original.addErrorPage(404, "/modified_404.html");
    original.addErrorPage(502, "/502.html");

    LocationConfig loc2;
    loc2.setPath("/extra");
    original.addLocation(loc2);

    ASSERT_EQ(copy.getHost(), "127.0.0.1");
    ASSERT_EQ(copy.getPort(), 8000);
    ASSERT_EQ(copy.getPortString(), "8000");
    ASSERT_EQ(copy.getClientMaxBodySize(), 4096u);
    ASSERT_EQ(copy.getServerNames().size(), 1u);
    ASSERT_EQ(copy.getServerNames()[0], "original.local");
    ASSERT_EQ(copy.getErrorPages().size(), 1u);
    ASSERT_EQ(copy.getErrorPages().find(404)->second, "/404.html");
    ASSERT_EQ(copy.getLocations().size(), 1u);
    ASSERT_EQ(copy.getLocations()[0].getPath(), "/");

    // Mutate copy - original must remain unaffected
    copy.setHost("10.0.0.1");
    copy.setPort(3000);
    ASSERT_EQ(original.getHost(), "192.168.1.1");
    ASSERT_EQ(original.getPort(), 9999);
}

TEST(ServerConfig_TestAssignmentOperatorBasic) {
    ServerConfig original;
    original.setHost("10.10.0.1");
    original.setPort(4242);
    original.setClientMaxBodySize(65536);
    original.addServerName("test.42.fr");
    original.addErrorPage(403, "/errors/403.html");

    LocationConfig loc;
    loc.setPath("/uploads");
    loc.setUploadStore("/tmp/uploads");
    original.addLocation(loc);

    ServerConfig assigned;
    // Populate assigned with pre-existing state to verify it gets overwritten
    assigned.setHost("localhost");
    assigned.setPort(80);
    assigned.addServerName("old.name");
    assigned.addServerName("another.old.name");
    assigned.addErrorPage(400, "/old_400.html");
    assigned.addErrorPage(500, "/old_500.html");

    assigned = original;

    ASSERT_EQ(assigned.getHost(), "10.10.0.1");
    ASSERT_EQ(assigned.getPort(), 4242);
    ASSERT_EQ(assigned.getPortString(), "4242");
    ASSERT_EQ(assigned.getClientMaxBodySize(), 65536u);
    ASSERT_EQ(assigned.getServerNames().size(), 1u);
    ASSERT_EQ(assigned.getServerNames()[0], "test.42.fr");
    ASSERT_EQ(assigned.getErrorPages().size(), 1u);
    ASSERT_TRUE(assigned.getErrorPages().find(400) == assigned.getErrorPages().end());
    ASSERT_EQ(assigned.getErrorPages().find(403)->second, "/errors/403.html");
    ASSERT_EQ(assigned.getLocations().size(), 1u);
    ASSERT_EQ(assigned.getLocations()[0].getPath(), "/uploads");
    ASSERT_EQ(assigned.getLocations()[0].getUploadStore(), "/tmp/uploads");
}

TEST(ServerConfig_TestAssignmentOperatorIndependence) {
    ServerConfig original;
    original.setHost("127.0.0.1");
    original.setPort(8080);
    original.addServerName("alpha.com");
    original.addErrorPage(404, "/404.html");

    ServerConfig assigned;
    assigned = original;

    // Mutate original after assignment
    original.setHost("0.0.0.0");
    original.setPort(9000);
    original.addServerName("beta.com");
    original.addErrorPage(404, "/new_404.html");
    original.addErrorPage(500, "/500.html");

    ASSERT_EQ(assigned.getHost(), "127.0.0.1");
    ASSERT_EQ(assigned.getPort(), 8080);
    ASSERT_EQ(assigned.getServerNames().size(), 1u);
    ASSERT_EQ(assigned.getServerNames()[0], "alpha.com");
    ASSERT_EQ(assigned.getErrorPages().size(), 1u);
    ASSERT_EQ(assigned.getErrorPages().find(404)->second, "/404.html");
}

TEST(ServerConfig_TestAssignmentOperatorSelfAssignment) {
    ServerConfig config;
    config.setHost("127.0.0.1");
    config.setPort(8888);
    config.setClientMaxBodySize(12345);
    config.addServerName("self.local");
    config.addErrorPage(418, "/teapot.html");

    LocationConfig loc;
    loc.setPath("/tea");
    config.addLocation(loc);

    config = config;

    ASSERT_EQ(config.getHost(), "127.0.0.1");
    ASSERT_EQ(config.getPort(), 8888);
    ASSERT_EQ(config.getClientMaxBodySize(), 12345u);
    ASSERT_EQ(config.getServerNames().size(), 1u);
    ASSERT_EQ(config.getServerNames()[0], "self.local");
    ASSERT_EQ(config.getErrorPages().size(), 1u);
    ASSERT_EQ(config.getErrorPages().find(418)->second, "/teapot.html");
    ASSERT_EQ(config.getLocations().size(), 1u);
    ASSERT_EQ(config.getLocations()[0].getPath(), "/tea");
}

TEST(ServerConfig_TestAssignmentOperatorChaining) {
    ServerConfig src;
    src.setHost("172.16.0.1");
    src.setPort(7070);
    src.setClientMaxBodySize(99999);
    src.addServerName("chained.org");

    ServerConfig dest1;
    ServerConfig dest2;

    dest2 = dest1 = src;

    ASSERT_EQ(dest1.getHost(), "172.16.0.1");
    ASSERT_EQ(dest1.getPort(), 7070);
    ASSERT_EQ(dest1.getClientMaxBodySize(), 99999u);
    ASSERT_EQ(dest1.getServerNames().size(), 1u);
    ASSERT_EQ(dest1.getServerNames()[0], "chained.org");

    ASSERT_EQ(dest2.getHost(), "172.16.0.1");
    ASSERT_EQ(dest2.getPort(), 7070);
    ASSERT_EQ(dest2.getClientMaxBodySize(), 99999u);
    ASSERT_EQ(dest2.getServerNames().size(), 1u);
    ASSERT_EQ(dest2.getServerNames()[0], "chained.org");
}

// =============================================================================
// 2. Host Management (setHost & getHost)
// =============================================================================

TEST(ServerConfig_TestSetAndGetHost) {
    ServerConfig config;

    config.setHost("127.0.0.1");
    ASSERT_EQ(config.getHost(), "127.0.0.1");

    config.setHost("localhost");
    ASSERT_EQ(config.getHost(), "localhost");

    config.setHost("192.168.100.42");
    ASSERT_EQ(config.getHost(), "192.168.100.42");

    config.setHost("sub.domain.example.com");
    ASSERT_EQ(config.getHost(), "sub.domain.example.com");
}

TEST(ServerConfig_TestSetHostEmptyString) {
    ServerConfig config;
    config.setHost("127.0.0.1");
    ASSERT_EQ(config.getHost(), "127.0.0.1");

    config.setHost("");
    ASSERT_EQ(config.getHost(), "");
    ASSERT_TRUE(config.getHost().empty());
}

// =============================================================================
// 3. Port Management (setPort, getPort & getPortString)
// =============================================================================

TEST(ServerConfig_TestSetAndGetPortStandard) {
    ServerConfig config;

    config.setPort(80);
    ASSERT_EQ(config.getPort(), 80);
    ASSERT_EQ(config.getPortString(), "80");

    config.setPort(443);
    ASSERT_EQ(config.getPort(), 443);
    ASSERT_EQ(config.getPortString(), "443");

    config.setPort(3000);
    ASSERT_EQ(config.getPort(), 3000);
    ASSERT_EQ(config.getPortString(), "3000");

    config.setPort(8080);
    ASSERT_EQ(config.getPort(), 8080);
    ASSERT_EQ(config.getPortString(), "8080");
}

TEST(ServerConfig_TestSetAndGetPortBoundaries) {
    ServerConfig config;

    // Minimum valid TCP port
    config.setPort(1);
    ASSERT_EQ(config.getPort(), 1);
    ASSERT_EQ(config.getPortString(), "1");

    // Maximum valid TCP port
    config.setPort(65535);
    ASSERT_EQ(config.getPort(), 65535);
    ASSERT_EQ(config.getPortString(), "65535");

    // Zero and negative values (ServerConfig stores raw int without validation)
    config.setPort(0);
    ASSERT_EQ(config.getPort(), 0);
    ASSERT_EQ(config.getPortString(), "0");

    config.setPort(-1);
    ASSERT_EQ(config.getPort(), -1);
    ASSERT_EQ(config.getPortString(), "-1");
}

// =============================================================================
// 4. Client Max Body Size Management (setClientMaxBodySize & getClientMaxBodySize)
// =============================================================================

TEST(ServerConfig_TestSetAndGetClientMaxBodySize) {
    ServerConfig config;

    // Default is 1 MB (1048576 bytes)
    ASSERT_EQ(config.getClientMaxBodySize(), 1048576u);

    // Zero body size
    config.setClientMaxBodySize(0);
    ASSERT_EQ(config.getClientMaxBodySize(), 0u);

    // 1 byte
    config.setClientMaxBodySize(1);
    ASSERT_EQ(config.getClientMaxBodySize(), 1u);

    // 10 KB
    config.setClientMaxBodySize(10240);
    ASSERT_EQ(config.getClientMaxBodySize(), 10240u);

    // 50 MB
    config.setClientMaxBodySize(50u * 1024u * 1024u);
    ASSERT_EQ(config.getClientMaxBodySize(), 52428800u);

    // 1 GB
    config.setClientMaxBodySize(1073741824u);
    ASSERT_EQ(config.getClientMaxBodySize(), 1073741824u);

    // Max size_t value
    size_t maxVal = std::numeric_limits<size_t>::max();
    config.setClientMaxBodySize(maxVal);
    ASSERT_EQ(config.getClientMaxBodySize(), maxVal);
}

// =============================================================================
// 5. Server Names Management (addServerName & getServerNames)
// =============================================================================

TEST(ServerConfig_TestAddServerNameSingle) {
    ServerConfig config;
    config.addServerName("example.com");

    const std::vector<std::string>& names = config.getServerNames();
    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(names[0], "example.com");
}

TEST(ServerConfig_TestAddServerNameMultiplePreservesOrder) {
    ServerConfig config;
    config.addServerName("example.com");
    config.addServerName("www.example.com");
    config.addServerName("api.example.com");
    config.addServerName("static.example.com");

    const std::vector<std::string>& names = config.getServerNames();
    ASSERT_EQ(names.size(), 4u);
    ASSERT_EQ(names[0], "example.com");
    ASSERT_EQ(names[1], "www.example.com");
    ASSERT_EQ(names[2], "api.example.com");
    ASSERT_EQ(names[3], "static.example.com");
}

TEST(ServerConfig_TestAddServerNameDuplicatesAndEmpty) {
    ServerConfig config;
    config.addServerName("dup.example.com");
    config.addServerName("");
    config.addServerName("dup.example.com");

    const std::vector<std::string>& names = config.getServerNames();
    ASSERT_EQ(names.size(), 3u);
    ASSERT_EQ(names[0], "dup.example.com");
    ASSERT_EQ(names[1], "");
    ASSERT_EQ(names[2], "dup.example.com");
}

// =============================================================================
// 6. Error Pages Management (addErrorPage & getErrorPages)
// =============================================================================

TEST(ServerConfig_TestAddErrorPageSingle) {
    ServerConfig config;
    config.addErrorPage(404, "/errors/404.html");

    const std::map<int, std::string>& pages = config.getErrorPages();
    ASSERT_EQ(pages.size(), 1u);

    std::map<int, std::string>::const_iterator it = pages.find(404);
    ASSERT_TRUE(it != pages.end());
    ASSERT_EQ(it->second, "/errors/404.html");
}

TEST(ServerConfig_TestAddErrorPageMultipleDistinctCodes) {
    ServerConfig config;
    config.addErrorPage(400, "/errors/400.html");
    config.addErrorPage(403, "/errors/403.html");
    config.addErrorPage(404, "/errors/404.html");
    config.addErrorPage(500, "/errors/50x.html");
    config.addErrorPage(502, "/errors/50x.html");
    config.addErrorPage(503, "/errors/50x.html");

    const std::map<int, std::string>& pages = config.getErrorPages();
    ASSERT_EQ(pages.size(), 6u);
    ASSERT_EQ(pages.find(400)->second, "/errors/400.html");
    ASSERT_EQ(pages.find(403)->second, "/errors/403.html");
    ASSERT_EQ(pages.find(404)->second, "/errors/404.html");
    ASSERT_EQ(pages.find(500)->second, "/errors/50x.html");
    ASSERT_EQ(pages.find(502)->second, "/errors/50x.html");
    ASSERT_EQ(pages.find(503)->second, "/errors/50x.html");
}

TEST(ServerConfig_TestAddErrorPageOverwriteExistingCode) {
    ServerConfig config;
    config.addErrorPage(404, "/errors/old_404.html");
    ASSERT_EQ(config.getErrorPages().size(), 1u);
    ASSERT_EQ(config.getErrorPages().find(404)->second, "/errors/old_404.html");

    // Adding an error page for the same status code must overwrite the previous path
    config.addErrorPage(404, "/errors/new_404.html");
    ASSERT_EQ(config.getErrorPages().size(), 1u);
    ASSERT_EQ(config.getErrorPages().find(404)->second, "/errors/new_404.html");
}

// =============================================================================
// 7. Locations Management (addLocation & getLocations)
// =============================================================================

TEST(ServerConfig_TestAddLocationSingleFullAttributes) {
    ServerConfig config;

    LocationConfig loc;
    loc.setPath("/cgi-bin");
    loc.setRoot("/var/www/cgi");
    loc.setAutoindex(false);
    loc.setUploadStore("/var/www/uploads");
    loc.setRedirect(301, "/new-cgi");
    loc.addIndex("index.py");
    loc.addIndex("default.py");
    loc.addAllowedMethod("GET");
    loc.addAllowedMethod("POST");
    loc.addCgiHandler(".py", "/usr/bin/python3");
    loc.addCgiHandler(".php", "/usr/bin/php-cgi");

    config.addLocation(loc);

    const std::vector<LocationConfig>& locations = config.getLocations();
    ASSERT_EQ(locations.size(), 1u);

    const LocationConfig& stored = locations[0];
    ASSERT_EQ(stored.getPath(), "/cgi-bin");
    ASSERT_EQ(stored.getRoot(), "/var/www/cgi");
    ASSERT_FALSE(stored.getAutoindex());
    ASSERT_EQ(stored.getUploadStore(), "/var/www/uploads");
    ASSERT_EQ(stored.getRedirect().first, 301);
    ASSERT_EQ(stored.getRedirect().second, "/new-cgi");

    ASSERT_EQ(stored.getIndex().size(), 2u);
    ASSERT_EQ(stored.getIndex()[0], "index.py");
    ASSERT_EQ(stored.getIndex()[1], "default.py");

    ASSERT_EQ(stored.getAllowedMethods().size(), 2u);
    ASSERT_EQ(stored.getAllowedMethods()[0], "GET");
    ASSERT_EQ(stored.getAllowedMethods()[1], "POST");

    ASSERT_EQ(stored.getCgiHandlers().size(), 2u);
    ASSERT_EQ(stored.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
    ASSERT_EQ(stored.getCgiHandlers().find(".php")->second, "/usr/bin/php-cgi");
}

TEST(ServerConfig_TestAddLocationMultiplePreservesOrder) {
    ServerConfig config;

    LocationConfig locRoot;
    locRoot.setPath("/");
    locRoot.setRoot("/var/www/html");

    LocationConfig locImages;
    locImages.setPath("/images");
    locImages.setRoot("/var/www/assets/images");
    locImages.setAutoindex(true);

    LocationConfig locUpload;
    locUpload.setPath("/upload");
    locUpload.setUploadStore("/var/www/uploads");

    config.addLocation(locRoot);
    config.addLocation(locImages);
    config.addLocation(locUpload);

    const std::vector<LocationConfig>& locations = config.getLocations();
    ASSERT_EQ(locations.size(), 3u);
    ASSERT_EQ(locations[0].getPath(), "/");
    ASSERT_EQ(locations[0].getRoot(), "/var/www/html");
    ASSERT_FALSE(locations[0].getAutoindex());

    ASSERT_EQ(locations[1].getPath(), "/images");
    ASSERT_EQ(locations[1].getRoot(), "/var/www/assets/images");
    ASSERT_TRUE(locations[1].getAutoindex());

    ASSERT_EQ(locations[2].getPath(), "/upload");
    ASSERT_EQ(locations[2].getUploadStore(), "/var/www/uploads");
}

TEST(ServerConfig_TestAddLocationValueSemanticsIndependence) {
    ServerConfig config;

    LocationConfig loc;
    loc.setPath("/initial");
    loc.setRoot("/var/www/initial");
    config.addLocation(loc);

    // Mutating local LocationConfig object after addLocation must not affect stored LocationConfig
    loc.setPath("/mutated");
    loc.setRoot("/var/www/mutated");
    loc.setAutoindex(true);

    ASSERT_EQ(config.getLocations().size(), 1u);
    ASSERT_EQ(config.getLocations()[0].getPath(), "/initial");
    ASSERT_EQ(config.getLocations()[0].getRoot(), "/var/www/initial");
    ASSERT_FALSE(config.getLocations()[0].getAutoindex());
}

// =============================================================================
// 8. Const-Correctness & Complete Server Configuration Scenario
// =============================================================================

TEST(ServerConfig_TestConstGetters) {
    ServerConfig mutableConfig;
    mutableConfig.setHost("127.0.0.1");
    mutableConfig.setPort(8085);
    mutableConfig.setClientMaxBodySize(4194304);
    mutableConfig.addServerName("const.example.com");
    mutableConfig.addErrorPage(404, "/404.html");

    LocationConfig loc;
    loc.setPath("/");
    loc.setRoot("/var/www");
    mutableConfig.addLocation(loc);

    const ServerConfig& constConfig = mutableConfig;

    ASSERT_EQ(constConfig.getHost(), "127.0.0.1");
    ASSERT_EQ(constConfig.getPort(), 8085);
    ASSERT_EQ(constConfig.getPortString(), "8085");
    ASSERT_EQ(constConfig.getClientMaxBodySize(), 4194304u);
    ASSERT_EQ(constConfig.getServerNames().size(), 1u);
    ASSERT_EQ(constConfig.getServerNames()[0], "const.example.com");
    ASSERT_EQ(constConfig.getErrorPages().size(), 1u);
    ASSERT_EQ(constConfig.getErrorPages().find(404)->second, "/404.html");
    ASSERT_EQ(constConfig.getLocations().size(), 1u);
    ASSERT_EQ(constConfig.getLocations()[0].getPath(), "/");
    ASSERT_EQ(constConfig.getLocations()[0].getRoot(), "/var/www");
}

