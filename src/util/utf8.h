#pragma once

#include <string>

namespace creatures {

/// Strip invalid UTF-8 bytes from a string.
/// The LLM sometimes outputs partial emoji sequences that break JSON serialization.
std::string sanitizeUtf8(const std::string& input);

}  // namespace creatures
