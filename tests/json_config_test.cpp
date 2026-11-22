#include "config.hpp"
#include <cassert>
#include <stdexcept>
#include <string>

int main() {
    using namespace cm;
    Config cfg = ParseConfig(ParseJson(
        R"({"fs_paths":["C:\\Windows", "D:\\Data"],"fs_recursive":true,"include":{"sys_info":false}})"));
    assert(cfg.fsPaths.size() == 2 && cfg.fsPaths[0] == L"C:\\Windows");
    assert(cfg.fsRecursive && !cfg.includeSysInfo && cfg.includeProcList);
    JsonValue root = JsonValue::object();
    JsonValue rows = JsonValue::array();
    JsonValue row = JsonValue::object();
    row.set("name", JsonValue::stringUtf8("quoted \" and \\ slash"));
    row.set("value", JsonValue::unsignedInteger(18446744073709551615ULL));
    rows.push(std::move(row));
    root.set("items", std::move(rows));
    const JsonValue parsed = ParseJson(root.dump(2));
    const JsonValue* items = parsed.find("items");
    assert(items && items->isArray() && items->asArray().size() == 1);
    const JsonValue& item = items->asArray()[0];
    assert(item.find("name")->asString() == "quoted \" and \\ slash");
    assert(item.find("value")->asUInt() == 18446744073709551615ULL);
    bool rejected = false;
    try { (void)ParseJson("{broken"); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
