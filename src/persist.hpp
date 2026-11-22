// persist.hpp — persist.check collector.
#pragma once

#include "json.hpp"

namespace cm {

// HKLM+HKCU Run/RunOnce, Startup folders, scheduled tasks with actions
// (Task Scheduler COM), auto-start non-Microsoft services (from svc.list).
// `svcList` is the array produced by CollectSvcList().
JsonValue CollectPersistCheck(const JsonValue& svcList);

}  // namespace cm
