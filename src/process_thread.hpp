// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "event_collector.hpp"
#include "input_event_buffer.hpp"
#include "link_peer.hpp"

#include <kairos/input_event.hpp>
#include <kairos/plugin_graph_manager.hpp>
#include <kairos/rcu.hpp>
#include <kairos/time_identity.hpp>

#include <atomic>
#include <cstdint>
#include <thread>

namespace kairos {

// Timer-driven CLAP process loop.
//
// Runs at block rate (block_size / sample_rate), drains three input queues
// (IPC, hardware MIDI, OSC) into an input_event_buffer, calls process_all()
// on all graph nodes, translates CLAP output events to midi_out_events, and
// pushes batches into the midi_event_queue for the MIDI dispatch thread.
//
// No audio I/O — plugins run in headless mode (null audio buffers).
class process_thread {
  public:
    struct config {
        double   sample_rate{48000.0};
        uint32_t block_size{64};
    };

    process_thread(config cfg, rcu_managed<plugin_graph_manager>& graph, link_peer& link,
                   midi_event_queue& midi_out_queue, input_event_queue& ipc_in_queue,
                   input_event_queue& hw_midi_in_queue, input_event_queue& osc_in_queue);
    ~process_thread();

    process_thread(const process_thread&)            = delete;
    process_thread& operator=(const process_thread&) = delete;

    void start();
    void stop();
    bool running() const noexcept;

  private:
    void run();

    config                             cfg_;
    rcu_managed<plugin_graph_manager>& graph_;
    link_peer&                         link_;
    midi_event_queue&                  midi_out_queue_;
    input_event_queue&                 ipc_in_queue_;
    input_event_queue&                 hw_midi_in_queue_;
    input_event_queue&                 osc_in_queue_;
    event_collector                    collector_;
    input_event_buffer                 in_buf_;
    time_identity                      time_;

    std::atomic<bool> running_{false};
    std::thread       thread_;
};

} // namespace kairos
