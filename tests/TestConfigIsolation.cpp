#include "TemporaryConfig.h"

// Linked into every test executable. QStandardPaths test mode puts QSettings in ~/.qttest, one file per test name for the
// whole machine, and without test mode QSettings uses the user's real config. Both break when the same test runs in two
// worktrees at once. A static initializer runs before main() and before the first QSettings object, so no test needs to
// remember to call useTemporaryConfig().
namespace {
const bool configIsolated = (useTemporaryConfig(), true);
}
