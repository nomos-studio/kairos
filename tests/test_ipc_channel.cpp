// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_test_macros.hpp>

#include <kairos/ipc_channel.hpp>

#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

// Create a connected socketpair for testing read/write round-trips.
static std::pair<int, int> make_pair() {
    int fds[2]{-1, -1};
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    return {fds[0], fds[1]};
}

TEST_CASE("ipc_channel: round-trip empty payload", "[ipc_channel]") {
    auto [a, b] = make_pair();

    auto wres = kairos::ipc::write_message(a, kairos::ipc::msg_session_open);
    REQUIRE(wres);

    auto rres = kairos::ipc::read_message(b);
    REQUIRE(rres);
    REQUIRE(rres->type() == kairos::ipc::msg_session_open);
    REQUIRE(rres->payload.empty());

    ::close(a);
    ::close(b);
}

TEST_CASE("ipc_channel: round-trip string payload", "[ipc_channel]") {
    auto [a, b] = make_pair();

    const std::string payload = "{:id :org.cljseq/loop :name \"loop\"}";
    auto wres = kairos::ipc::write_message(a, kairos::ipc::msg_register_source, payload);
    REQUIRE(wres);

    auto rres = kairos::ipc::read_message(b);
    REQUIRE(rres);
    REQUIRE(rres->type() == kairos::ipc::msg_register_source);

    const std::string_view got{reinterpret_cast<const char*>(rres->payload.data()),
                               rres->payload.size()};
    REQUIRE(std::string(got) == payload);

    ::close(a);
    ::close(b);
}

TEST_CASE("ipc_channel: EOF returns eof error", "[ipc_channel]") {
    auto [a, b] = make_pair();
    ::close(a); // close the write end

    auto rres = kairos::ipc::read_message(b);
    REQUIRE(!rres);
    REQUIRE(rres.error() == kairos::ipc::channel_error::eof);

    ::close(b);
}
