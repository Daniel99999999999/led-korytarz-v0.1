#pragma once
#include <cstddef>
#include <cstring>
namespace local_ota_validation {
inline constexpr std::size_t kHeaderBytes = 288;
// ESP image header + first segment header + esp_app_desc_t (32 + 256 bytes).
inline bool header(const char* bytes, std::size_t length, const char* project,
                   const char* current_version) noexcept {
    if (!bytes || length < kHeaderBytes || !project || !current_version)
        return false;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    if (p[0] != 0xE9 || p[32] != 0x32 || p[33] != 0x54 || p[34] != 0xCD || p[35] != 0xAB)
        return false;
    const auto* version = bytes + 48;
    const auto* candidate_project = bytes + 80;
    return version[0] != 0 && std::memchr(version, 0, 32) &&
           std::memchr(candidate_project, 0, 32) &&
           std::strncmp(candidate_project, project, 32) == 0 &&
           std::strncmp(version, current_version, 32) != 0;
}
} // namespace local_ota_validation
