#pragma once

#include "cJSON.h"

namespace local_control {
[[nodiscard]] bool appendDiscovery(cJSON* components, const char* base,
                                   const char* device_id) noexcept;
}
