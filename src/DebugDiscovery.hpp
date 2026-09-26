#pragma once
#include "cJSON.h"
namespace debug_discovery {
bool append(cJSON* components, const char* base, const char* device_id) noexcept;
}
