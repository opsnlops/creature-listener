#include "util/utf8.h"

namespace creatures {

std::string sanitizeUtf8(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        int expectedLen = 0;
        if (c < 0x80) {
            expectedLen = 1;
        } else if ((c & 0xE0) == 0xC0) {
            expectedLen = 2;
        } else if ((c & 0xF0) == 0xE0) {
            expectedLen = 3;
        } else if ((c & 0xF8) == 0xF0) {
            expectedLen = 4;
        } else {
            i++;
            continue;
        }

        if (i + expectedLen > input.size()) {
            break;
        }

        bool valid = true;
        for (int j = 1; j < expectedLen; j++) {
            if ((static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }

        if (valid) {
            result.append(input, i, expectedLen);
            i += expectedLen;
        } else {
            i++;
        }
    }

    return result;
}

std::string cleanForTTS(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); i++) {
        // ° is UTF-8 bytes 0xC2 0xB0
        if (i + 1 < input.size()
            && static_cast<unsigned char>(input[i]) == 0xC2
            && static_cast<unsigned char>(input[i + 1]) == 0xB0) {
            result += " degrees";
            i++;  // skip the second byte
        } else {
            result += input[i];
        }
    }

    return result;
}

}  // namespace creatures
