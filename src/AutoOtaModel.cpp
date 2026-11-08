#include "AutoOtaModel.hpp"

#include "cJSON.h"

#include <cstring>
#include <limits>

namespace auto_ota {
namespace {
bool version(const char* text, std::array<std::uint32_t, 3>& parts) noexcept {
    if (!text)
        return false;
    for (unsigned part = 0; part < 3; ++part) {
        if (*text < '0' || *text > '9')
            return false;
        const bool zero = *text == '0';
        unsigned digits = 0;
        std::uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            const auto digit = static_cast<std::uint32_t>(*text++ - '0');
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U)
                return false;
            value = value * 10U + digit;
            ++digits;
        }
        if (zero && digits > 1)
            return false;
        parts[part] = value;
        if (part != 2 && *text++ != '.')
            return false;
    }
    return *text == '\0';
}
bool stringIs(const cJSON* root, const char* key, const char* expected) noexcept {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && std::strcmp(item->valuestring, expected) == 0;
}
int hex(char c) noexcept {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
} // namespace
bool newer(const char* candidate, const char* current) noexcept {
    std::array<std::uint32_t, 3> a{}, b{};
    return version(candidate, a) && version(current, b) && a > b;
}
bool parseManifest(const char* json, std::size_t length, Manifest& output) noexcept {
    if (!json || length == 0 || length > kManifestLimit || std::memchr(json, 0, length))
        return false;
    // Manifest is flat; reject nesting before cJSON can recurse on the task stack.
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const char c = json[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 1)
                return false;
        } else if (c == '}' || c == ']') {
            if (depth == 0)
                return false;
            --depth;
        }
    }
    if (quoted || depth != 0)
        return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root)
        return false;
    bool valid = cJSON_IsObject(root);
    while (end && end < json + length &&
           (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t'))
        ++end;
    valid = valid && end == json + length;
    for (const cJSON* a = root->child; a; a = a->next)
        for (const cJSON* b = a->next; b; b = b->next)
            if (a->string && b->string && std::strcmp(a->string, b->string) == 0)
                valid = false;
    const auto* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const auto* size = cJSON_GetObjectItemCaseSensitive(root, "size");
    const auto* ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    const auto* tag = cJSON_GetObjectItemCaseSensitive(root, "tag");
    const auto* hash = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    Manifest result{};
    std::array<std::uint32_t, 3> parts{};
    valid = valid && cJSON_IsNumber(schema) && schema->valuedouble == 1 && cJSON_IsNumber(size) &&
            size->valuedouble >= 256 && size->valuedouble <= 0x1D0000 &&
            size->valuedouble == size->valueint && cJSON_IsString(ver) &&
            std::strlen(ver->valuestring) < result.version.size() &&
            version(ver->valuestring, parts) && cJSON_IsString(tag) && tag->valuestring[0] == 'v' &&
            std::strcmp(tag->valuestring + 1, ver->valuestring) == 0 &&
            stringIs(root, "project", "led_controller") &&
            stringIs(root, "profile", "esp32doit-devkit-v1-ota") &&
            stringIs(root, "filename", "firmware.bin") && cJSON_IsString(hash) &&
            std::strlen(hash->valuestring) == 64;
    if (valid) {
        std::strcpy(result.version.data(), ver->valuestring);
        result.size = static_cast<std::uint32_t>(size->valueint);
        for (unsigned i = 0; i < 32; ++i) {
            const int high = hex(hash->valuestring[i * 2]), low = hex(hash->valuestring[i * 2 + 1]);
            if (high < 0 || low < 0) {
                valid = false;
                break;
            }
            result.sha256[i] = static_cast<std::uint8_t>(high * 16 + low);
        }
    }
    cJSON_Delete(root);
    if (valid)
        output = result;
    return valid;
}
} // namespace auto_ota
