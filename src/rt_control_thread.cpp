// SPDX-License-Identifier: GPL-2.0-or-later
#include <kairos/ipc.hpp>
#include <kairos/rt_control_thread.hpp>

#include <edn/builtins.hpp>
#include <edn/parser.hpp>
#include <txlog/txlog.hpp>

#include <clap/events.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

namespace {

// Parse the canonical UUID string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" into bytes.
edn::uuid parse_uuid(const std::string& s) noexcept {
    edn::uuid u{};
    if (s.size() != 36)
        return u;
    int  bi = 0;
    auto h  = [](char c) -> uint8_t {
        return c <= '9' ? static_cast<uint8_t>(c - '0') : static_cast<uint8_t>((c | 32) - 'a' + 10);
    };
    for (std::size_t i = 0; i < 36 && bi < 16;) {
        if (s[i] == '-') {
            ++i;
            continue;
        }
        u.bytes[bi++] = static_cast<uint8_t>((h(s[i]) << 4) | h(s[i + 1]));
        i += 2;
    }
    return u;
}

} // namespace

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

    // Build a CLAP note event from its component fields.
    clap_event_union make_note_event(bool is_on, int16_t key, int16_t channel, int16_t port,
                                     int16_t note_id, double velocity) noexcept {
        clap_event_union ev{};
        ev.note.header.size     = sizeof(clap_event_note_t);
        ev.note.header.time     = 0;
        ev.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.note.header.type     = is_on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
        ev.note.header.flags    = 0;
        ev.note.note_id         = note_id;
        ev.note.port_index      = port;
        ev.note.channel         = channel;
        ev.note.key             = key;
        ev.note.velocity        = velocity;
        return ev;
    }

} // namespace

rt_control_thread::rt_control_thread(config cfg, param_queue& queue, input_event_queue& in_queue)
    : cfg_(std::move(cfg)), queue_(queue), in_queue_(in_queue) {
}

rt_control_thread::~rt_control_thread() {
    stop();
}

void rt_control_thread::start() {
    listen_fd_ = make_listen_socket(cfg_.socket_path);
    if (listen_fd_ < 0)
        return;

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&rt_control_thread::run, this);
}

void rt_control_thread::stop() {
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

bool rt_control_thread::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void rt_control_thread::run() {
    while (running_.load(std::memory_order_acquire)) {
        const int conn_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (conn_fd < 0)
            break;

        handle_connection(conn_fd);
        ::close(conn_fd);
    }
}

void rt_control_thread::handle_connection(int conn_fd) {
    std::optional<session> sess;

    while (running_.load(std::memory_order_acquire)) {
        auto result = ipc::read_message(conn_fd);
        if (!result)
            break;
        dispatch_message(conn_fd, *result, sess);
    }
}

void rt_control_thread::dispatch_message(int conn_fd, const ipc::message& msg,
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
        queue_.push(param_event{.path = *path, .value = *value, .time = {}});
        break;
    }

    case ipc::msg_note_on:
    case ipc::msg_note_off: {
        if (msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        auto get_i16 = [&](const char* kw, int16_t def) -> int16_t {
            const auto* v = m.find_kw(kw);
            if (v && v->is<int64_t>())
                return static_cast<int16_t>(v->get<int64_t>());
            return def;
        };
        auto get_dbl = [&](const char* kw, double def) -> double {
            const auto* v = m.find_kw(kw);
            if (v && v->is<double>())
                return v->get<double>();
            if (v && v->is<int64_t>())
                return static_cast<double>(v->get<int64_t>());
            return def;
        };

        const bool is_on = (msg.type() == ipc::msg_note_on);
        auto       ev =
            make_note_event(is_on, get_i16("key", 60), get_i16("channel", 0), get_i16("port", 0),
                            get_i16("note-id", -1), get_dbl("velocity", 0.0));

        // Optional :beat field — if present and a scheduler is wired, defer the
        // event until that Link beat rather than dispatching immediately.
        const auto* beat_v = m.find_kw("beat");
        if (beat_v && cfg_.sched_staging) {
            const double target = get_dbl("beat", 0.0);
            cfg_.sched_staging->push(scheduled_event{.beat = target, .event = ev});
        } else {
            in_queue_.push(ev);
        }
        break;
    }

    case ipc::msg_schedule_bundle: {
        // Bundle of beat-accurate events: {:at-beat D :events [{:at-tick N :type :kw ...}]}
        // Each event's beat = at-beat + at-tick / 24.0.
        // Requires sched_staging to be wired; silently drops the bundle otherwise.
        if (msg.payload.empty() || !cfg_.sched_staging)
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m        = parsed->get<edn::map>();
        const auto* beat_v   = m.find_kw("at-beat");
        const auto* events_v = m.find_kw("events");
        if (!beat_v || !events_v || !events_v->is<edn::vector>())
            break;

        double anchor = 0.0;
        if (beat_v->is<double>())
            anchor = beat_v->get<double>();
        else if (beat_v->is<int64_t>())
            anchor = static_cast<double>(beat_v->get<int64_t>());

        for (const auto& item : events_v->get<edn::vector>().items) {
            if (!item.is<edn::map>())
                continue;
            const auto& em = item.get<edn::map>();

            auto get_i = [&](const char* kw, int64_t def) -> int64_t {
                const auto* v = em.find_kw(kw);
                if (v && v->is<int64_t>())
                    return v->get<int64_t>();
                return def;
            };
            auto get_d = [&](const char* kw, double def) -> double {
                const auto* v = em.find_kw(kw);
                if (v && v->is<double>())
                    return v->get<double>();
                if (v && v->is<int64_t>())
                    return static_cast<double>(v->get<int64_t>());
                return def;
            };

            const int64_t at_tick = get_i("at-tick", 0);
            const double  beat    = anchor + at_tick / 24.0;

            const auto* type_v = em.find_kw("type");
            const bool  is_on  = !(type_v && type_v->is<edn::keyword>() &&
                                   type_v->get<edn::keyword>().name == "note-off");

            auto ev = make_note_event(
                is_on, static_cast<int16_t>(get_i("key", 60)),
                static_cast<int16_t>(get_i("channel", 0)), static_cast<int16_t>(get_i("port", 0)),
                static_cast<int16_t>(get_i("note-id", -1)), get_d("velocity", 0.0));

            cfg_.sched_staging->push(scheduled_event{.beat = beat, .event = ev});
        }
        break;
    }

    case ipc::msg_midi_in: {
        if (msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        const auto* port_v = m.find_kw("port");
        const auto* data_v = m.find_kw("data");
        if (!data_v || !data_v->is<edn::vector>())
            break;
        const auto& bytes = data_v->get<edn::vector>().items;

        clap_event_union ev{};
        ev.midi.header.size     = sizeof(clap_event_midi_t);
        ev.midi.header.time     = 0;
        ev.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.midi.header.type     = CLAP_EVENT_MIDI;
        ev.midi.header.flags    = 0;
        ev.midi.port_index =
            (port_v && port_v->is<int64_t>()) ? static_cast<uint16_t>(port_v->get<int64_t>()) : 0;
        for (std::size_t i = 0; i < 3 && i < bytes.size(); ++i) {
            if (bytes[i].is<int64_t>())
                ev.midi.data[i] = static_cast<uint8_t>(bytes[i].get<int64_t>());
        }
        in_queue_.push(ev);
        break;
    }

    case ipc::msg_tx_log: {
        if (!sess || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        txlog::entry e;

        if (const auto* v = m.find_kw("id"); v && v->is<edn::tagged>()) {
            const auto& t = v->get<edn::tagged>();
            if (t.tag == "uuid" && t.val && t.val->is<std::string>())
                e.id = parse_uuid(t.val->get<std::string>());
        }

        if (const auto* v = m.find_kw("beat"); v) {
            if (v->is<double>())
                e.beat = v->get<double>();
            else if (v->is<int64_t>())
                e.beat = static_cast<double>(v->get<int64_t>());
        }

        if (const auto* v = m.find_kw("wall-ns"); v && v->is<int64_t>())
            e.wall_ns = v->get<int64_t>();

        if (const auto* v = m.find_kw("source"); v && v->is<edn::keyword>())
            e.source = v->get<edn::keyword>();

        if (const auto* v = m.find_kw("path"); v)
            e.path = *v;

        if (const auto* v = m.find_kw("before"); v && !v->is_nil())
            e.before = *v;
        if (const auto* v = m.find_kw("after"); v && !v->is_nil())
            e.after = *v;
        if (const auto* v = m.find_kw("parent"); v && !v->is_nil())
            e.parent = *v;

        sess->emit(e);
        break;
    }

    default:
        dispatch_extension(conn_fd, msg, sess);
        break;
    }

    (void)conn_fd;
}

void rt_control_thread::dispatch_extension(int, const ipc::message&, std::optional<session>&) {
    // no-op — derived classes override to handle additional message types
}

} // namespace kairos
