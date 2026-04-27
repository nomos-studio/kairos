# SPDX-License-Identifier: LGPL-2.1-or-later
include(Catch)

add_executable(kairos-tests
    test_time_identity.cpp
    test_ipc.cpp
)

target_link_libraries(kairos-tests
    PRIVATE
        kairos::core
        Catch2::Catch2WithMain
)

set_target_properties(kairos-tests PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

catch_discover_tests(kairos-tests)
