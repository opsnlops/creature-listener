#pragma once

#include <memory>
#include <string>

#include "config/Configuration.h"

namespace creatures {

/// Load configuration from a YAML file.
/// Values from the file are merged into the provided Configuration,
/// overriding defaults but not CLI overrides applied afterward.
bool loadConfigFile(const std::string& path, Configuration& config);

}  // namespace creatures
