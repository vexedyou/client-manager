// software.hpp — software.list collector.
#pragma once

#include "json.hpp"

namespace cm {

// Registry uninstall keys: name + version + publisher (+ install date,
// location, uninstall string). HKLM/HKCU Uninstall + Wow6432Node.
JsonValue CollectSoftwareList();

}  // namespace cm
