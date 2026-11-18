// The whole text as a finite number, or nullopt ("abc", "10oops", "1e999", "nan", "", " 5").
// For command-line options.
#pragma once

#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

namespace trainz::common {

inline std::optional<double> strictNumber(const char* text) {
    const std::string s = text == nullptr ? std::string() : std::string(text);
    if (s.empty() || std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(v)) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception&) {  // invalid_argument, out_of_range
        return std::nullopt;
    }
}

}  // namespace trainz::common
