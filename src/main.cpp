// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace kairos {
std::string_view version() noexcept;
}

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "kairos v" << kairos::version() << "\n";
    return EXIT_SUCCESS;
}
