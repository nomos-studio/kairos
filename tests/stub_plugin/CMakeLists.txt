# SPDX-License-Identifier: LGPL-2.1-or-later
add_library(kairos-stub-plugin SHARED stub_plugin.cpp)

target_link_libraries(kairos-stub-plugin PRIVATE clap)

set_target_properties(kairos-stub-plugin PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)
