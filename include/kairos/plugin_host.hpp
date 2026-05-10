// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <clap/host.h>

namespace kairos {

// Returns the static clap_host_t descriptor for the kairos container.
// The returned pointer has program lifetime; pass it to every plugin instance.
const clap_host_t* kairos_host() noexcept;

} // namespace kairos
