#pragma once

#include <optional>
#include <string_view>

namespace pkgfile {

std::optional<int> ValidateCompression(std::string_view compress);

}  // namespace pkgfile
