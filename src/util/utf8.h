#pragma once

#include <string>

namespace creatures {

/// Strip invalid UTF-8 bytes from a string.
/// The LLM sometimes outputs partial emoji sequences that break JSON serialization.
std::string sanitizeUtf8(const std::string& input);

/// Replace symbols that TTS engines can't pronounce with spoken equivalents.
/// e.g. "38°F" → "38 degrees F", "%" → " percent"
std::string cleanForTTS(const std::string& input);

}  // namespace creatures
