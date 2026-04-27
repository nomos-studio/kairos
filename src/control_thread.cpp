// SPDX-License-Identifier: GPL-2.0-or-later
#include <kairos/control_thread.hpp>
#include <kairos/ipc.hpp>

#include <edn/parser.hpp>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace kairos {

namespace {

    int make_listen_socket(const std::string& path) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        ::unlink(path.c_str()); // remove stale socket

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(fd, 1) < 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

} // namespace

control_thread::control_thread(config cfg, param_queue& queue)
    : cfg_(std::move(cfg)), queue_(queue) {
}

control_thread::~control_thread() {
    stop();
}

void control_thread::start() {
    listen_fd_ = make_listen_socket(cfg_.socket_path);
    if (listen_fd_ < 0)
        return;

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&control_thread::run, this);
}

void control_thread::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(cfg_.socket_path.c_str());
    }

    if (thread_.joinable())
        thread_.join();
}

bool control_thread::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void control_thread::run() {
    std::optional<session> sess;

    while (running_.load(std::memory_order_acquire)) {
        const int conn_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (conn_fd < 0)
            break;

        handle_connection(conn_fd);
        ::close(conn_fd);
    }

    if (sess)
        sess->close();
}

void control_thread::handle_connection(int conn_fd) {
    std::optional<session> sess;

    while (running_.load(std::memory_order_acquire)) {
        auto result = ipc::read_message(conn_fd);
        if (!result)
            break;
        dispatch_message(conn_fd, *result, sess);
    }
}

void control_thread::dispatch_message(int conn_fd, const ipc::message& msg,
                                      std::optional<session>& sess) {
    switch (msg.type()) {
    case ipc::msg_session_open: {
        sess = session::open(cfg_.db_path);
        break;
    }

    case ipc::msg_session_close: {
        if (sess)
            sess->close();
        sess.reset();
        break;
    }

    case ipc::msg_register_source: {
        if (!sess || msg.payload.empty())
            break;
        // Payload is EDN: {:id :some/keyword :name "..." :description "..."}
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m   = parsed->get<edn::map>();
        const auto* id  = m.find_kw("id");
        const auto* nm  = m.find_kw("name");
        const auto* dsc = m.find_kw("description");
        if (!id || !id->is<edn::keyword>())
            break;
        sess->register_source({
            .id          = id->get<edn::keyword>(),
            .name        = nm && nm->is<std::string>() ? nm->get<std::string>() : "",
            .description = dsc && dsc->is<std::string>() ? dsc->get<std::string>() : "",
        });
        break;
    }

    case ipc::msg_param_set: {
        if (msg.payload.empty())
            break;
        // Payload is EDN: {:path [...] :value <v> :time {:current {...} :pending {...}}}
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m     = parsed->get<edn::map>();
        const auto* path  = m.find_kw("path");
        const auto* value = m.find_kw("value");
        if (!path || !value)
            break;
        // time_identity reconstruction is a stub — full parse added with Link integration.
        queue_.push(param_event{.path = *path, .value = *value, .time = {}});
        break;
    }

    case ipc::msg_tx_log: {
        if (!sess || msg.payload.empty())
            break;
        // Payload is a raw EDN entry; forwarded as-is to the session.
        // Full entry parsing comes with the txlog write path.
        break;
    }

    case ipc::msg_graph_load:
    case ipc::msg_graph_reset:
        // Stub — graph loading handled in a later sprint.
        break;

    default:
        break;
    }

    (void)conn_fd; // reserved for future reply messages
}

} // namespace kairos
