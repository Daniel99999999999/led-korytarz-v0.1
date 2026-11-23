#pragma once
#include <cstdint>
// POD snapshots: only the I2C owner writes; web reads a synchronized copy.
struct IoObservation {
    bool attempted{false}, responding{false};
    std::int32_t error{0};
    std::uint32_t attempts{0}, failures{0}, last_io_s{0};
    void record(std::int32_t result, std::uint32_t seconds) noexcept {
        attempted = true;
        responding = result == 0;
        error = result;
        ++attempts;
        if (result != 0)
            ++failures;
        last_io_s = seconds;
    }
};
enum class NvramOperation : unsigned {
    none,
    attach,
    probe,
    pointer,
    read_byte,
    read_record,
    invalidate,
    write_payload,
    commit,
    verify
};
inline const char* nvramOperationName(NvramOperation operation) noexcept {
    switch (operation) {
    case NvramOperation::none:
        return "none";
    case NvramOperation::attach:
        return "attach";
    case NvramOperation::probe:
        return "probe_0x68";
    case NvramOperation::pointer:
        return "pointer_0x08";
    case NvramOperation::read_byte:
        return "read_byte_0x08";
    case NvramOperation::read_record:
        return "read_records";
    case NvramOperation::invalidate:
        return "invalidate";
    case NvramOperation::write_payload:
        return "write_payload";
    case NvramOperation::commit:
        return "commit";
    case NvramOperation::verify:
        return "verify";
    }
    return "unknown";
}
struct NvramDiagnostics {
    IoObservation io{};
    NvramOperation operation{NvramOperation::none}, failed_operation{NvramOperation::none};
    bool enabled{false}, record_valid{false}, runtime_matches{false};
    bool save_pending{false}, restore_queued{false}, retry_pending{false};
    std::int32_t last_error{0};
    std::uint32_t sequence{0}, saves{0}, runtime_revision{0};
    int slot{-1};
    unsigned phase{0}, ram_key1{0}, saved_key1{0};
    void completeOperation(std::int32_t result, bool performed_io) noexcept {
        if (result != 0) {
            last_error = result;
            failed_operation = operation;
        } else if (performed_io && (operation == NvramOperation::read_record ||
                                    operation == NvramOperation::verify)) {
            last_error = 0;
            failed_operation = NvramOperation::none;
        }
    }
};
struct HardwareDiagnostics {
    bool sampled{false}, bus_initialized{false}, lcd_ready{false}, backlight{false};
    bool ddram_checked{false};
    IoObservation lcd_io{};
    std::int32_t lcd_error{0}, ddram_error{0}, bus_recovery_error{0};
    std::uint32_t sampled_at{0}, lcd_retries{0}, frames{0}, last_frame_s{0};
    std::uint32_t checks{0}, mismatches{0}, last_check_s{0}, bus_recoveries{0};
    NvramDiagnostics nvram{};
};
