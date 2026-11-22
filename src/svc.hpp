// svc.hpp — svc.list collector.
#pragma once

#include "json.hpp"

namespace cm {

// name, display name, state, start type, binary path.
// Service Control Manager: EnumServicesStatusExW + QueryServiceConfigW.
JsonValue CollectSvcList();

}  // namespace cm
