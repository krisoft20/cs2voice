#pragma once

namespace vpk {
    bool extract(const std::string& path, const std::string& want, std::vector<uint8_t>& out, std::string& err);
    bool list(const std::string& path, const std::string& prefix, std::vector<std::string>& out, std::string& err);
}
