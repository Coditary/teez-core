#pragma once

namespace teez::core {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitInterrupted = 130;

/// Maps run outcome to CI-friendly exit codes.
int normalize_exit_code(bool interrupted, bool saw_failure, int child_exit_code);

} // namespace teez::core
