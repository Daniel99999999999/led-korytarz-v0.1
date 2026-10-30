#pragma once
#include "StackModel.hpp"

#include <cstddef>
namespace stack_monitor {
enum class Task : unsigned {
    main,
    led,
    lcd,
    ir,
    mqtt_service,
    mqtt_client,
    ota,
    local_ota,
    http,
    https,
    web_start,
    event,
    console,
    count
};
constexpr std::size_t kCount = static_cast<std::size_t>(Task::count);
const char* name(Task task) noexcept;
void sample(Task task, unsigned budget, bool force = false, bool finished = false) noexcept;
bool read(Task task, stack_model::Record& record) noexcept;
struct Summary {
    Task task{Task::main};
    stack_model::Record record{};
    unsigned measured{0};
    bool complete{true};
};
Summary summary() noexcept;
// Only use on the owning task: no cached handles, no heap and no ISR work.
class Scope final {
  public:
    Scope(Task task, unsigned budget, bool finished = false) noexcept
        : task_(task), budget_(budget), finished_(finished) {}
    ~Scope() {
        sample(task_, budget_, true, finished_);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    Task task_;
    unsigned budget_;
    bool finished_;
};
} // namespace stack_monitor
