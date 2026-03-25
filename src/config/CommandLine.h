#pragma once

#include <memory>

#include "config/Configuration.h"

namespace creatures {

/// Parse command-line arguments into a Configuration.
/// Returns nullptr if the program should exit (e.g. --help, --list-devices).
std::unique_ptr<Configuration> parseCommandLine(int argc, char* argv[]);

}  // namespace creatures
