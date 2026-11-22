// sysinfo.hpp — sys.info collector.
#pragma once

#include "json.hpp"

namespace cm {

// OS version/build, CPU model + cores, RAM total/available, disks
// free/total, NICs MAC/IPv4/IPv6, uptime, logged-in user, hostname, domain.
JsonValue CollectSysInfo();

}  // namespace cm
