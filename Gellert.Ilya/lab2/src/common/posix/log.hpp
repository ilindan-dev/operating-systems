#pragma once

#include <string>
#include <string_view>

namespace chat::log {

/// Sets the tag printed in every line, e.g. "host" or "client".
void setRole(std::string role);

void info(std::string_view message);
void warning(std::string_view message);
void error(std::string_view message);

} // namespace chat::log
