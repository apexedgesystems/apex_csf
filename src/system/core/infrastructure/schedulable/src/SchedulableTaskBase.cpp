/**
 * @file SchedulableTaskBase.cpp
 * @brief Implementation of SchedulableTaskBase.
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskBase.hpp"

namespace system_core {
namespace schedulable {

/* ----------------------------- SchedulableTaskBase Methods ----------------------------- */

SchedulableTaskBase::SchedulableTaskBase(TaskFn task, std::string_view label) noexcept
    : task_(task), label_(label) {}

std::string_view SchedulableTaskBase::getLabel() const noexcept { return label_; }

} // namespace schedulable
} // namespace system_core