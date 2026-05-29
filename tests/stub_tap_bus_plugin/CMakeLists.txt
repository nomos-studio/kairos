# SPDX-License-Identifier: LGPL-2.1-or-later
add_library(kairos-stub-tap-bus-plugin SHARED stub_tap_bus_plugin.cpp)

target_include_directories(kairos-stub-tap-bus-plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

target_link_libraries(kairos-stub-tap-bus-plugin PRIVATE clap)

set_target_properties(kairos-stub-tap-bus-plugin PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)
