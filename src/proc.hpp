// proc.hpp — proc.list collector.
#pragma once

#include "json.hpp"

namespace cm {

// pid, ppid, name, image path, user, cmdline, start time.
// Toolhelp snapshot + OpenProcess/QueryFullProcessImageNameW/GetProcessTimes
// + token SID lookup for the user + WMI Win32_Process for cmdline.
JsonValue CollectProcList();

}  // namespace cm
