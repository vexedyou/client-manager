// fs.hpp — fs.list collector.
#pragma once

#include <string>
#include <vector>

#include "json.hpp"

namespace cm {

// For each path in `paths`: name/size/mtime/is_dir of its entries
// (FindFirstFileW/FindNextFileW). Single file -> one entry.
// `recursive` descends into subdirectories (reparse points skipped).
JsonValue CollectFsList(const std::vector<std::wstring>& paths, bool recursive);

}  // namespace cm
