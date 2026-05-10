// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <kairos/result.hpp>

#include <clap/entry.h>
#include <clap/ext/audio-ports.h>
#include <clap/plugin.h>
#include <clap/process.h>

#include <string>
#include <vector>

namespace kairos {

enum class plugin_error {
    load_failed,        // dlopen() returned null
    entry_not_found,    // clap_plugin_entry symbol missing from binary
    init_failed,        // entry->init() returned false
    factory_not_found,  // no CLAP_PLUGIN_FACTORY_ID factory in binary
    create_failed,      // factory->create_plugin() returned null
    plugin_init_failed, // plugin->init() returned false
    activate_failed,    // plugin->activate() returned false
    processing_failed,  // plugin->start_processing() returned false
    wrong_state,        // operation not valid in current state
};

// RAII wrapper around the full CLAP plugin lifecycle.
//
// State machine:
//   load()            → initialized
//   activate()        → activated
//   start_processing()→ processing
//   process()           (only valid in processing state)
//   stop_processing() → activated
//   deactivate()      → initialized
//   ~plugin_instance()  calls destroy(), deinit(), dlclose()
class plugin_instance {
  public:
    enum class state { initialized, activated, processing };

    // Load a CLAP plugin binary at `path`, find the plugin with `plugin_id`,
    // and call plugin->init().  On macOS the path must point directly to the
    // Mach-O binary (bundle unwrapping is handled by the caller for now).
    static result<plugin_instance, plugin_error>
    load(const std::string& path, const std::string& plugin_id, const clap_host_t* host);

    ~plugin_instance();
    plugin_instance(plugin_instance&&) noexcept;
    plugin_instance& operator=(plugin_instance&&) noexcept;
    plugin_instance(const plugin_instance&)            = delete;
    plugin_instance& operator=(const plugin_instance&) = delete;

    result<std::monostate, plugin_error> activate(double sample_rate, uint32_t min_frames,
                                                  uint32_t max_frames);

    result<std::monostate, plugin_error> start_processing();

    clap_process_status process(const clap_process_t& proc);

    void stop_processing();
    void deactivate();

    const clap_plugin_descriptor_t* descriptor() const noexcept;
    state                           current_state() const noexcept;

    const std::vector<clap_audio_port_info_t>& in_ports() const noexcept { return in_ports_; }
    const std::vector<clap_audio_port_info_t>& out_ports() const noexcept { return out_ports_; }

  private:
    plugin_instance() = default;
    void teardown() noexcept;

    void*                      lib_handle_{nullptr};
    const clap_plugin_entry_t* entry_{nullptr};
    const clap_plugin_t*       plugin_{nullptr};
    state                      state_{state::initialized};

    std::vector<clap_audio_port_info_t> in_ports_;
    std::vector<clap_audio_port_info_t> out_ports_;
};

} // namespace kairos
