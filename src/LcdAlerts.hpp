#pragma once
#include <cstdint>

namespace lcd_alerts {
enum class Alert : unsigned {
    local,
    watchdog,
    brownout,
    panic,
    reset_unknown,
    nvram_address,
    nvram_read,
    nvram_write,
    nvram_record,
    nvram_pending,
    eeprom,
    eeprom_write,
    eeprom_record,
    eeprom_apply,
    ina,
    radiator,
    enclosure,
    wifi,
    mqtt_auth,
    mqtt,
    mqtt_queue,
    ntp,
    heap,
    stack,
    ota,
    auto_ota,
    updating,
    wifi_config,
    mqtt_config,
    count
};
using Mask = std::uint32_t;
constexpr unsigned kCount = static_cast<unsigned>(Alert::count);
static_assert(kCount < 32);
constexpr Mask bit(Alert alert) noexcept {
    return Mask{1} << static_cast<unsigned>(alert);
}
inline constexpr char kText[kCount][21] = {
    "STEROWANIE: BLAD",     "RESET: WATCHDOG",      "RESET: BROWNOUT",      "RESET: PANIC",
    "RESET: NIEZNANY",      "DS1307: BRAK ACK",     "DS1307: BLAD ODCZYTU", "NVRAM: BLAD ZAPISU",
    "NVRAM: BRAK REKORDU",  "NVRAM: TYLKO RAM",     "EEPROM: BLAD",         "EEPROM: NIEZAPISANE",
    "EEPROM: BRAK REKORDU", "KONFIG: NIEAKTYWNA", "INA228: BRAK POMIARU", "TEMP RAD: BRAK",
    "TEMP OBU: BRAK",       "WIFI: ROZLACZONE",     "MQTT: BLAD LOGOWANIA", "MQTT: ROZLACZONE",
    "MQTT: ZALEGLE DANE",   "CZAS: BRAK NTP",       "RAM: MALO PAMIECI",    "STOS: MINIMUM <512B",
    "OTA: BLAD INSTALACJI", "OTA: BLAD KONTROLI",   "OTA: AKTUALIZACJA",    "WIFI: BRAK KONFIG",
    "MQTT: BRAK KONFIG"};
class Rotation {
  public:
    const char* select(Mask active, std::uint32_t seconds) noexcept {
        active &= (Mask{1} << kCount) - 1;
        if (!active) {
            current_ = kCount;
            return nullptr;
        }
        if (current_ < kCount && (active & (Mask{1} << current_)) && seconds - changed_ < 3U)
            return kText[current_];
        const unsigned start = current_ < kCount ? (current_ + 1) % kCount : 0;
        for (unsigned i = 0; i < kCount; ++i) {
            const auto candidate = (start + i) % kCount;
            if (active & (Mask{1} << candidate)) {
                current_ = candidate;
                changed_ = seconds;
                return kText[current_];
            }
        }
        return nullptr;
    }

  private:
    unsigned current_{kCount};
    std::uint32_t changed_{0};
};
} // namespace lcd_alerts
