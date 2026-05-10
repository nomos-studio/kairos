// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <kairos/graph.hpp>

#include <string>
#include <vector>

namespace kairos {

// Scan `extra_paths` plus the platform default CLAP search paths for .clap
// bundles.  For each bundle, open it with dlopen, read the plugin factory,
// and record every plugin ID → absolute bundle path pair.
//
// Platform default paths:
//   macOS:  ~/Library/Audio/Plug-Ins/CLAP  /Library/Audio/Plug-Ins/CLAP
//   Linux:  ~/.clap  /usr/lib/clap  /usr/local/lib/clap
//           (plus paths from CLAP_PATH env var, colon-separated)
//
// Returns only successfully opened bundles.  Errors are logged to stderr.
plugin_registry discover_plugins(const std::vector<std::string>& extra_paths = {});

} // namespace kairos
