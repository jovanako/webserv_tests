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
