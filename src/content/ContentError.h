// Error type used by the content readers.
//
// Every problem with a file (bad header, data past the end, broken
// compression) is reported as a ContentError with a message that names the
// file and, where it helps, the byte offset. Nothing in src/content aborts the
// program or reads outside a buffer on bad input.
#pragma once

#include <stdexcept>
#include <string>

namespace trainz::content {

class ContentError : public std::runtime_error {
public:
    explicit ContentError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace trainz::content
