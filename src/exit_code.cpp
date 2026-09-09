#include "teez/core/exit_code.hpp"

namespace teez::core {

int normalize_exit_code(bool interrupted, bool saw_failure, int child_exit_code) {
    if (interrupted) {
        return kExitInterrupted;
    }
    if (saw_failure || child_exit_code != 0) {
        return kExitFailure;
    }
    return kExitSuccess;
}

} // namespace teez::core
