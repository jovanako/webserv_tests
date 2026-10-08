#include "tiny_test.hpp"
#include "LocationConfig.hpp"
#include <string>
#include <vector>
#include <map>
#include <utility>

// =============================================================================
// 1. Initial State & Orthodox Canonical Form
// =============================================================================

TEST(LocationConfig_TestInitialState) {
    LocationConfig loc;

    ASSERT_EQ(loc.getPath(), "");
    ASSERT_TRUE(loc.getPath().empty());
    ASSERT_EQ(loc.getRoot(), "");
    ASSERT_TRUE(loc.getRoot().empty());
    ASSERT_TRUE(loc.getIndex().empty());
    ASSERT_EQ(loc.getIndex().size(), 0u);
    ASSERT_TRUE(loc.getAllowedMethods().empty());
    ASSERT_EQ(loc.getAllowedMethods().size(), 0u);
    ASSERT_FALSE(loc.getAutoindex());
    ASSERT_TRUE(loc.getCgiHandlers().empty());
    ASSERT_EQ(loc.getCgiHandlers().size(), 0u);
    ASSERT_EQ(loc.getUploadStore(), "");
    ASSERT_TRUE(loc.getUploadStore().empty());
    ASSERT_EQ(loc.getRedirect().first, 0);
    ASSERT_EQ(loc.getRedirect().second, "");
}

TEST(LocationConfig_TestCopyConstructorBasic) {
    LocationConfig original;
    original.setPath("/cgi-bin");
    original.setRoot("/var/www/cgi");
    original.setAutoindex(true);
    original.setUploadStore("/var/www/uploads");
    original.setRedirect(301, "/new-cgi");
    original.addIndex("index.py");
    original.addIndex("default.py");
    original.addAllowedMethod("GET");
    original.addAllowedMethod("POST");
    original.addCgiHandler(".py", "/usr/bin/python3");
    original.addCgiHandler(".php", "/usr/bin/php-cgi");

    LocationConfig copy(original);

    ASSERT_EQ(copy.getPath(), "/cgi-bin");
    ASSERT_EQ(copy.getRoot(), "/var/www/cgi");
    ASSERT_TRUE(copy.getAutoindex());
    ASSERT_EQ(copy.getUploadStore(), "/var/www/uploads");
    ASSERT_EQ(copy.getRedirect().first, 301);
    ASSERT_EQ(copy.getRedirect().second, "/new-cgi");

    ASSERT_EQ(copy.getIndex().size(), 2u);
    ASSERT_EQ(copy.getIndex()[0], "index.py");
    ASSERT_EQ(copy.getIndex()[1], "default.py");

    ASSERT_EQ(copy.getAllowedMethods().size(), 2u);
    ASSERT_EQ(copy.getAllowedMethods()[0], "GET");
    ASSERT_EQ(copy.getAllowedMethods()[1], "POST");

    ASSERT_EQ(copy.getCgiHandlers().size(), 2u);
    ASSERT_EQ(copy.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
    ASSERT_EQ(copy.getCgiHandlers().find(".php")->second, "/usr/bin/php-cgi");
}

TEST(LocationConfig_TestCopyConstructorIndependence) {
    LocationConfig original;
    original.setPath("/static");
    original.setRoot("/var/www/static");
    original.setAutoindex(false);
    original.setUploadStore("/tmp/uploads");
    original.setRedirect(302, "/temp");
    original.addIndex("index.html");
    original.addAllowedMethod("GET");
    original.addCgiHandler(".py", "/usr/bin/python3");

    LocationConfig copy(original);

    // Mutate original - copy must remain unaffected
    original.setPath("/mutated");
    original.setRoot("/var/www/mutated");
    original.setAutoindex(true);
    original.setUploadStore("/var/uploads_new");
    original.setRedirect(307, "/mutated-redirect");
    original.addIndex("extra.html");
    original.addAllowedMethod("DELETE");
    original.addCgiHandler(".py", "/usr/local/bin/python3");
    original.addCgiHandler(".sh", "/bin/bash");

    ASSERT_EQ(copy.getPath(), "/static");
    ASSERT_EQ(copy.getRoot(), "/var/www/static");
    ASSERT_FALSE(copy.getAutoindex());
    ASSERT_EQ(copy.getUploadStore(), "/tmp/uploads");
    ASSERT_EQ(copy.getRedirect().first, 302);
    ASSERT_EQ(copy.getRedirect().second, "/temp");
    ASSERT_EQ(copy.getIndex().size(), 1u);
    ASSERT_EQ(copy.getIndex()[0], "index.html");
    ASSERT_EQ(copy.getAllowedMethods().size(), 1u);
    ASSERT_EQ(copy.getAllowedMethods()[0], "GET");
    ASSERT_EQ(copy.getCgiHandlers().size(), 1u);
    ASSERT_EQ(copy.getCgiHandlers().find(".py")->second, "/usr/bin/python3");

    // Mutate copy - original must remain unaffected
    copy.setPath("/copy-path");
    copy.setAutoindex(true);
    ASSERT_EQ(original.getPath(), "/mutated");
    ASSERT_TRUE(original.getAutoindex());
}

TEST(LocationConfig_TestAssignmentOperatorBasic) {
    LocationConfig original;
    original.setPath("/uploads");
    original.setRoot("/var/www/data");
    original.setAutoindex(true);
    original.setUploadStore("/var/www/data/incoming");
    original.setRedirect(308, "https://example.com/uploads");
    original.addIndex("upload.html");
    original.addAllowedMethod("POST");
    original.addCgiHandler(".pl", "/usr/bin/perl");

    LocationConfig assigned;
    // Populate assigned with pre-existing state to verify it gets completely overwritten
    assigned.setPath("/old");
    assigned.setRoot("/old/root");
    assigned.setAutoindex(false);
    assigned.setUploadStore("/old/store");
    assigned.setRedirect(301, "/old-redirect");
    assigned.addIndex("old1.html");
    assigned.addIndex("old2.html");
    assigned.addAllowedMethod("GET");
    assigned.addAllowedMethod("DELETE");
    assigned.addCgiHandler(".py", "/usr/bin/python");
    assigned.addCgiHandler(".rb", "/usr/bin/ruby");

    assigned = original;

    ASSERT_EQ(assigned.getPath(), "/uploads");
    ASSERT_EQ(assigned.getRoot(), "/var/www/data");
    ASSERT_TRUE(assigned.getAutoindex());
    ASSERT_EQ(assigned.getUploadStore(), "/var/www/data/incoming");
    ASSERT_EQ(assigned.getRedirect().first, 308);
    ASSERT_EQ(assigned.getRedirect().second, "https://example.com/uploads");

    ASSERT_EQ(assigned.getIndex().size(), 1u);
    ASSERT_EQ(assigned.getIndex()[0], "upload.html");

    ASSERT_EQ(assigned.getAllowedMethods().size(), 1u);
    ASSERT_EQ(assigned.getAllowedMethods()[0], "POST");

    ASSERT_EQ(assigned.getCgiHandlers().size(), 1u);
    ASSERT_TRUE(assigned.getCgiHandlers().find(".py") == assigned.getCgiHandlers().end());
    ASSERT_EQ(assigned.getCgiHandlers().find(".pl")->second, "/usr/bin/perl");
}

TEST(LocationConfig_TestAssignmentOperatorIndependence) {
    LocationConfig original;
    original.setPath("/api");
    original.setRoot("/var/www/api");
    original.addIndex("index.json");
    original.addAllowedMethod("GET");
    original.addCgiHandler(".py", "/usr/bin/python3");

    LocationConfig assigned;
    assigned = original;

    // Mutate original after assignment
    original.setPath("/api/v2");
    original.setRoot("/var/www/api_v2");
    original.setAutoindex(true);
    original.addIndex("v2.json");
    original.addAllowedMethod("POST");
    original.addCgiHandler(".py", "/usr/bin/pypy3");
    original.addCgiHandler(".php", "/usr/bin/php-cgi");

    ASSERT_EQ(assigned.getPath(), "/api");
    ASSERT_EQ(assigned.getRoot(), "/var/www/api");
    ASSERT_FALSE(assigned.getAutoindex());
    ASSERT_EQ(assigned.getIndex().size(), 1u);
    ASSERT_EQ(assigned.getIndex()[0], "index.json");
    ASSERT_EQ(assigned.getAllowedMethods().size(), 1u);
    ASSERT_EQ(assigned.getAllowedMethods()[0], "GET");
    ASSERT_EQ(assigned.getCgiHandlers().size(), 1u);
    ASSERT_EQ(assigned.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
}

TEST(LocationConfig_TestAssignmentOperatorSelfAssignment) {
    LocationConfig loc;
    loc.setPath("/self");
    loc.setRoot("/var/www/self");
    loc.setAutoindex(true);
    loc.setUploadStore("/tmp/self");
    loc.setRedirect(301, "/target");
    loc.addIndex("self.html");
    loc.addAllowedMethod("GET");
    loc.addCgiHandler(".py", "/usr/bin/python3");

    loc = loc;

    ASSERT_EQ(loc.getPath(), "/self");
    ASSERT_EQ(loc.getRoot(), "/var/www/self");
    ASSERT_TRUE(loc.getAutoindex());
    ASSERT_EQ(loc.getUploadStore(), "/tmp/self");
    ASSERT_EQ(loc.getRedirect().first, 301);
    ASSERT_EQ(loc.getRedirect().second, "/target");
    ASSERT_EQ(loc.getIndex().size(), 1u);
    ASSERT_EQ(loc.getIndex()[0], "self.html");
    ASSERT_EQ(loc.getAllowedMethods().size(), 1u);
    ASSERT_EQ(loc.getAllowedMethods()[0], "GET");
    ASSERT_EQ(loc.getCgiHandlers().size(), 1u);
    ASSERT_EQ(loc.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
}

TEST(LocationConfig_TestAssignmentOperatorChaining) {
    LocationConfig src;
    src.setPath("/chained");
    src.setRoot("/var/www/chained");
    src.setAutoindex(true);
    src.addIndex("chain.html");
    src.addAllowedMethod("POST");

    LocationConfig dest1;
    LocationConfig dest2;

    dest2 = dest1 = src;

    ASSERT_EQ(dest1.getPath(), "/chained");
    ASSERT_EQ(dest1.getRoot(), "/var/www/chained");
    ASSERT_TRUE(dest1.getAutoindex());
    ASSERT_EQ(dest1.getIndex().size(), 1u);
    ASSERT_EQ(dest1.getIndex()[0], "chain.html");
    ASSERT_EQ(dest1.getAllowedMethods().size(), 1u);
    ASSERT_EQ(dest1.getAllowedMethods()[0], "POST");

    ASSERT_EQ(dest2.getPath(), "/chained");
    ASSERT_EQ(dest2.getRoot(), "/var/www/chained");
    ASSERT_TRUE(dest2.getAutoindex());
    ASSERT_EQ(dest2.getIndex().size(), 1u);
    ASSERT_EQ(dest2.getIndex()[0], "chain.html");
    ASSERT_EQ(dest2.getAllowedMethods().size(), 1u);
    ASSERT_EQ(dest2.getAllowedMethods()[0], "POST");
}

// =============================================================================
// 2. Path Management (setPath & getPath)
// =============================================================================

TEST(LocationConfig_TestSetAndGetPath) {
    LocationConfig loc;

    loc.setPath("/");
    ASSERT_EQ(loc.getPath(), "/");

    loc.setPath("/images");
    ASSERT_EQ(loc.getPath(), "/images");

    loc.setPath("/api/v1/users/");
    ASSERT_EQ(loc.getPath(), "/api/v1/users/");

    loc.setPath("");
    ASSERT_EQ(loc.getPath(), "");
    ASSERT_TRUE(loc.getPath().empty());
}

// =============================================================================
// 3. Root Directory Management (setRoot & getRoot)
// =============================================================================

TEST(LocationConfig_TestSetAndGetRoot) {
    LocationConfig loc;

    loc.setRoot("/var/www/html");
    ASSERT_EQ(loc.getRoot(), "/var/www/html");

    loc.setRoot("./www/assets");
    ASSERT_EQ(loc.getRoot(), "./www/assets");

    loc.setRoot("/srv/http/");
    ASSERT_EQ(loc.getRoot(), "/srv/http/");

    loc.setRoot("");
    ASSERT_EQ(loc.getRoot(), "");
    ASSERT_TRUE(loc.getRoot().empty());
}

// =============================================================================
// 4. Autoindex Management (setAutoindex & getAutoindex)
// =============================================================================

TEST(LocationConfig_TestSetAndGetAutoindex) {
    LocationConfig loc;

    ASSERT_FALSE(loc.getAutoindex());

    loc.setAutoindex(true);
    ASSERT_TRUE(loc.getAutoindex());

    loc.setAutoindex(false);
    ASSERT_FALSE(loc.getAutoindex());
}

// =============================================================================
// 5. Upload Store Management (setUploadStore & getUploadStore)
// =============================================================================

TEST(LocationConfig_TestSetAndGetUploadStore) {
    LocationConfig loc;

    loc.setUploadStore("/var/www/uploads");
    ASSERT_EQ(loc.getUploadStore(), "/var/www/uploads");

    loc.setUploadStore("./uploads/files");
    ASSERT_EQ(loc.getUploadStore(), "./uploads/files");

    loc.setUploadStore("");
    ASSERT_EQ(loc.getUploadStore(), "");
    ASSERT_TRUE(loc.getUploadStore().empty());
}

// =============================================================================
// 6. Redirection Management (setRedirect & getRedirect)
// =============================================================================

TEST(LocationConfig_TestSetAndGetRedirectStandard) {
    LocationConfig loc;

    loc.setRedirect(301, "/new-location");
    ASSERT_EQ(loc.getRedirect().first, 301);
    ASSERT_EQ(loc.getRedirect().second, "/new-location");

    loc.setRedirect(302, "https://example.org/login");
    ASSERT_EQ(loc.getRedirect().first, 302);
    ASSERT_EQ(loc.getRedirect().second, "https://example.org/login");

    loc.setRedirect(307, "/temporary");
    ASSERT_EQ(loc.getRedirect().first, 307);
    ASSERT_EQ(loc.getRedirect().second, "/temporary");

    loc.setRedirect(308, "/permanent");
    ASSERT_EQ(loc.getRedirect().first, 308);
    ASSERT_EQ(loc.getRedirect().second, "/permanent");
}

TEST(LocationConfig_TestSetAndGetRedirectEdgeCases) {
    LocationConfig loc;

    loc.setRedirect(301, "/somewhere");
    ASSERT_EQ(loc.getRedirect().first, 301);

    // Resetting to 0 and empty URL
    loc.setRedirect(0, "");
    ASSERT_EQ(loc.getRedirect().first, 0);
    ASSERT_EQ(loc.getRedirect().second, "");

    // Negative or non-standard status code (LocationConfig stores raw pair without validation)
    loc.setRedirect(-1, "/negative");
    ASSERT_EQ(loc.getRedirect().first, -1);
    ASSERT_EQ(loc.getRedirect().second, "/negative");
}

// =============================================================================
// 7. Index Files Management (addIndex & getIndex)
// =============================================================================

TEST(LocationConfig_TestAddIndexSingle) {
    LocationConfig loc;
    loc.addIndex("index.html");

    const std::vector<std::string>& indices = loc.getIndex();
    ASSERT_EQ(indices.size(), 1u);
    ASSERT_EQ(indices[0], "index.html");
}

TEST(LocationConfig_TestAddIndexMultiplePreservesOrder) {
    LocationConfig loc;
    loc.addIndex("index.html");
    loc.addIndex("index.htm");
    loc.addIndex("default.html");
    loc.addIndex("home.py");

    const std::vector<std::string>& indices = loc.getIndex();
    ASSERT_EQ(indices.size(), 4u);
    ASSERT_EQ(indices[0], "index.html");
    ASSERT_EQ(indices[1], "index.htm");
    ASSERT_EQ(indices[2], "default.html");
    ASSERT_EQ(indices[3], "home.py");
}

TEST(LocationConfig_TestAddIndexDuplicatesAndEmpty) {
    LocationConfig loc;
    loc.addIndex("index.html");
    loc.addIndex("");
    loc.addIndex("index.html");

    const std::vector<std::string>& indices = loc.getIndex();
    ASSERT_EQ(indices.size(), 3u);
    ASSERT_EQ(indices[0], "index.html");
    ASSERT_EQ(indices[1], "");
    ASSERT_EQ(indices[2], "index.html");
}

// =============================================================================
// 8. Allowed HTTP Methods Management (addAllowedMethod & getAllowedMethods)
// =============================================================================

TEST(LocationConfig_TestAddAllowedMethodSingle) {
    LocationConfig loc;
    loc.addAllowedMethod("GET");

    const std::vector<std::string>& methods = loc.getAllowedMethods();
    ASSERT_EQ(methods.size(), 1u);
    ASSERT_EQ(methods[0], "GET");
}

TEST(LocationConfig_TestAddAllowedMethodMultiplePreservesOrder) {
    LocationConfig loc;
    loc.addAllowedMethod("GET");
    loc.addAllowedMethod("POST");
    loc.addAllowedMethod("DELETE");

    const std::vector<std::string>& methods = loc.getAllowedMethods();
    ASSERT_EQ(methods.size(), 3u);
    ASSERT_EQ(methods[0], "GET");
    ASSERT_EQ(methods[1], "POST");
    ASSERT_EQ(methods[2], "DELETE");
}

TEST(LocationConfig_TestAddAllowedMethodDuplicatesAndCustom) {
    LocationConfig loc;
    loc.addAllowedMethod("POST");
    loc.addAllowedMethod("");
    loc.addAllowedMethod("POST");

    const std::vector<std::string>& methods = loc.getAllowedMethods();
    ASSERT_EQ(methods.size(), 3u);
    ASSERT_EQ(methods[0], "POST");
    ASSERT_EQ(methods[1], "");
    ASSERT_EQ(methods[2], "POST");
}

// =============================================================================
// 9. CGI Handlers Management (addCgiHandler & getCgiHandlers)
// =============================================================================

TEST(LocationConfig_TestAddCgiHandlerSingle) {
    LocationConfig loc;
    loc.addCgiHandler(".py", "/usr/bin/python3");

    const std::map<std::string, std::string>& handlers = loc.getCgiHandlers();
    ASSERT_EQ(handlers.size(), 1u);

    std::map<std::string, std::string>::const_iterator it = handlers.find(".py");
    ASSERT_TRUE(it != handlers.end());
    ASSERT_EQ(it->second, "/usr/bin/python3");
}

TEST(LocationConfig_TestAddCgiHandlerMultipleDistinct) {
    LocationConfig loc;
    loc.addCgiHandler(".py", "/usr/bin/python3");
    loc.addCgiHandler(".php", "/usr/bin/php-cgi");
    loc.addCgiHandler(".sh", "/bin/bash");
    loc.addCgiHandler(".pl", "/usr/bin/perl");

    const std::map<std::string, std::string>& handlers = loc.getCgiHandlers();
    ASSERT_EQ(handlers.size(), 4u);
    ASSERT_EQ(handlers.find(".py")->second, "/usr/bin/python3");
    ASSERT_EQ(handlers.find(".php")->second, "/usr/bin/php-cgi");
    ASSERT_EQ(handlers.find(".sh")->second, "/bin/bash");
    ASSERT_EQ(handlers.find(".pl")->second, "/usr/bin/perl");
}

TEST(LocationConfig_TestAddCgiHandlerOverwriteExistingExtension) {
    LocationConfig loc;
    loc.addCgiHandler(".py", "/usr/bin/python2");
    ASSERT_EQ(loc.getCgiHandlers().size(), 1u);
    ASSERT_EQ(loc.getCgiHandlers().find(".py")->second, "/usr/bin/python2");

    // Adding a handler for the same extension must overwrite the previous interpreter path
    loc.addCgiHandler(".py", "/usr/bin/python3");
    ASSERT_EQ(loc.getCgiHandlers().size(), 1u);
    ASSERT_EQ(loc.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
}

// =============================================================================
// 10. Const-Correctness
// =============================================================================

TEST(LocationConfig_TestConstGetters) {
    LocationConfig mutableLoc;
    mutableLoc.setPath("/const-test");
    mutableLoc.setRoot("/var/www/const");
    mutableLoc.setAutoindex(true);
    mutableLoc.setUploadStore("/var/www/const/uploads");
    mutableLoc.setRedirect(301, "/redirected");
    mutableLoc.addIndex("index.html");
    mutableLoc.addAllowedMethod("GET");
    mutableLoc.addCgiHandler(".py", "/usr/bin/python3");

    const LocationConfig& constLoc = mutableLoc;

    ASSERT_EQ(constLoc.getPath(), "/const-test");
    ASSERT_EQ(constLoc.getRoot(), "/var/www/const");
    ASSERT_TRUE(constLoc.getAutoindex());
    ASSERT_EQ(constLoc.getUploadStore(), "/var/www/const/uploads");
    ASSERT_EQ(constLoc.getRedirect().first, 301);
    ASSERT_EQ(constLoc.getRedirect().second, "/redirected");
    ASSERT_EQ(constLoc.getIndex().size(), 1u);
    ASSERT_EQ(constLoc.getIndex()[0], "index.html");
    ASSERT_EQ(constLoc.getAllowedMethods().size(), 1u);
    ASSERT_EQ(constLoc.getAllowedMethods()[0], "GET");
    ASSERT_EQ(constLoc.getCgiHandlers().size(), 1u);
    ASSERT_EQ(constLoc.getCgiHandlers().find(".py")->second, "/usr/bin/python3");
}

