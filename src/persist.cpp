// persist.cpp — persist.check: Run/RunOnce registry, Startup folders,
// scheduled tasks via Task Scheduler COM, non-Microsoft auto-start services.
#include "persist.hpp"

#include <windows.h>
#include <shlobj.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <vector>

#include "util.hpp"

namespace cm {

using Microsoft::WRL::ComPtr;

namespace {

// ---- BSTR RAII -----------------------------------------------------------

struct BstrDeleter {
    void operator()(BSTR b) const {
        if (b) SysFreeString(b);
    }
};
using UniqueBstr = std::unique_ptr<OLECHAR, BstrDeleter>;

std::wstring BstrToWString(BSTR b) {
    return b ? std::wstring(b, SysStringLen(b)) : std::wstring();
}

// ---- registry Run / RunOnce ----------------------------------------------

std::wstring RegDataToString(DWORD type, const BYTE* data, DWORD size) {
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        std::wstring s(reinterpret_cast<const wchar_t*>(data), size / sizeof(wchar_t));
        while (!s.empty() && s.back() == L'\0') s.pop_back();
        return s;
    }
    if (type == REG_DWORD && size >= sizeof(DWORD)) {
        return std::to_wstring(*reinterpret_cast<const DWORD*>(data));
    }
    if (type == REG_MULTI_SZ) {
        std::wstring out;
        const wchar_t* p = reinterpret_cast<const wchar_t*>(data);
        size_t remaining = size / sizeof(wchar_t);
        while (remaining > 0 && *p) {
            size_t len = 0;
            while (len < remaining && p[len] != L'\0') ++len;
            if (!out.empty()) out += L"; ";
            out.append(p, len);
            if (len == remaining) break;
            p += len + 1;
            remaining -= len + 1;
        }
        return out;
    }
    return L"<binary>";
}

void EnumRunKey(HKEY hive, const wchar_t* subkey, const wchar_t* hiveLabel,
                std::vector<JsonValue>& out) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return;
    UniqueRegKey guard(h);

    DWORD idx = 0;
    while (true) {
        wchar_t nameBuf[1024];
        DWORD nameLen = static_cast<DWORD>(sizeof(nameBuf) / sizeof(nameBuf[0]));
        LONG r = RegEnumValueW(h, idx, nameBuf, &nameLen, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) break;
        ++idx;
        std::wstring valueName(nameBuf, nameLen);

        DWORD type = 0, dataSize = 0;
        if (RegQueryValueExW(h, valueName.c_str(), nullptr, &type, nullptr, &dataSize) != ERROR_SUCCESS) {
            continue;
        }
        std::vector<BYTE> data(dataSize);
        if (RegQueryValueExW(h, valueName.c_str(), nullptr, &type, data.data(), &dataSize) != ERROR_SUCCESS) {
            continue;
        }
        std::wstring dataStr = RegDataToString(type, data.data(), dataSize);

        std::wstring expanded = dataStr;
        if (type == REG_EXPAND_SZ) {
            std::vector<wchar_t> exp(4096);
            DWORD n = ExpandEnvironmentStringsW(dataStr.c_str(), exp.data(),
                                                static_cast<DWORD>(exp.size()));
            if (n > 0 && n <= exp.size()) expanded.assign(exp.data(), n - 1);
        }

        JsonValue e = JsonValue::object();
        e.set("hive", JsonValue::stringWide(std::wstring(hiveLabel)));
        e.set("path", JsonValue::stringWide(std::wstring(subkey)));
        e.set("name", JsonValue::stringWide(valueName));
        e.set("data", JsonValue::stringWide(dataStr));
        e.set("expanded_data", JsonValue::stringWide(expanded));
        e.set("type", JsonValue::unsignedInteger(type));
        out.push_back(std::move(e));
    }
}

JsonValue CollectRunKeys() {
    JsonValue root = JsonValue::object();

    std::vector<JsonValue> run, runOnce;
    const wchar_t* kRun = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kRunOnce = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    const wchar_t* kRunWow = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kRunOnceWow = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce";

    EnumRunKey(HKEY_LOCAL_MACHINE, kRun, L"HKLM", run);
    EnumRunKey(HKEY_LOCAL_MACHINE, kRunWow, L"HKLM (Wow6432Node)", run);
    EnumRunKey(HKEY_CURRENT_USER, kRun, L"HKCU", run);

    EnumRunKey(HKEY_LOCAL_MACHINE, kRunOnce, L"HKLM", runOnce);
    EnumRunKey(HKEY_LOCAL_MACHINE, kRunOnceWow, L"HKLM (Wow6432Node)", runOnce);
    EnumRunKey(HKEY_CURRENT_USER, kRunOnce, L"HKCU", runOnce);

    auto toArray = [](std::vector<JsonValue>& v) {
        JsonValue a = JsonValue::array();
        for (auto& e : v) a.push(std::move(e));
        return a;
    };
    root.set("run", toArray(run));
    root.set("run_once", toArray(runOnce));
    return root;
}

// ---- Startup folders ------------------------------------------------------

JsonValue ListFolderEntries(const std::wstring& folderPath) {
    JsonValue entries = JsonValue::array();
    std::wstring pattern = folderPath + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return entries;
    UniqueFindHandle guard(h);
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        JsonValue e = JsonValue::object();
        e.set("name", JsonValue::stringWide(std::wstring(fd.cFileName)));
        e.set("path", JsonValue::stringWide(folderPath + L"\\" + fd.cFileName));
        ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.set("size", JsonValue::unsignedInteger(size));
        e.set("mtime_utc", JsonValue::stringWide(FormatFileTimeUtc(fd.ftLastWriteTime)));
        e.set("is_dir", JsonValue::boolean((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));
        entries.push(std::move(e));
    } while (FindNextFileW(h, &fd));
    return entries;
}

JsonValue CollectStartupFolders() {
    JsonValue folders = JsonValue::array();
    struct Item { int csidl; const wchar_t* label; };
    const Item items[] = {{CSIDL_STARTUP, L"user"}, {CSIDL_COMMON_STARTUP, L"common"}};
    for (const Item& it : items) {
        wchar_t path[MAX_PATH] = {};
        HRESULT hr = SHGetFolderPathW(nullptr, it.csidl, nullptr, SHGFP_TYPE_CURRENT, path);
        JsonValue f = JsonValue::object();
        f.set("scope", JsonValue::stringWide(std::wstring(it.label)));
        f.set("path", JsonValue::stringWide(std::wstring(path)));
        if (SUCCEEDED(hr) && path[0]) {
            f.set("entries", ListFolderEntries(std::wstring(path)));
        } else {
            f.set("entries", JsonValue::array());
            f.set("error", JsonValue::stringWide(WinErrorString(static_cast<DWORD>(hr))));
        }
        folders.push(std::move(f));
    }
    return folders;
}

// ---- scheduled tasks (Task Scheduler COM) ---------------------------------

const wchar_t* TaskStateName(TASK_STATE s) {
    switch (s) {
        case TASK_STATE_UNKNOWN: return L"UNKNOWN";
        case TASK_STATE_DISABLED: return L"DISABLED";
        case TASK_STATE_QUEUED: return L"QUEUED";
        case TASK_STATE_READY: return L"READY";
        case TASK_STATE_RUNNING: return L"RUNNING";
        default: return L"UNKNOWN";
    }
}

const wchar_t* TriggerTypeName(TASK_TRIGGER_TYPE2 t) {
    switch (t) {
        case TASK_TRIGGER_EVENT: return L"EVENT";
        case TASK_TRIGGER_LOGON: return L"LOGON";
        case TASK_TRIGGER_BOOT: return L"BOOT";
        case TASK_TRIGGER_TIME: return L"TIME";
        case TASK_TRIGGER_DAILY: return L"DAILY";
        case TASK_TRIGGER_WEEKLY: return L"WEEKLY";
        case TASK_TRIGGER_MONTHLY: return L"MONTHLY";
        case TASK_TRIGGER_MONTHLYDOW: return L"MONTHLYDOW";
        case TASK_TRIGGER_IDLE: return L"IDLE";
        case TASK_TRIGGER_REGISTRATION: return L"REGISTRATION";
        default: return L"UNKNOWN";
    }
}

const wchar_t* ActionTypeName(TASK_ACTION_TYPE t) {
    switch (t) {
        case TASK_ACTION_EXEC: return L"EXEC";
        case TASK_ACTION_COM_HANDLER: return L"COM_HANDLER";
        case TASK_ACTION_SEND_EMAIL: return L"SEND_EMAIL";
        case TASK_ACTION_SHOW_MESSAGE: return L"SHOW_MESSAGE";
        default: return L"UNKNOWN";
    }
}

std::wstring DateToIso(DATE d) {
    SYSTEMTIME st{};
    if (d == 0 || !VariantTimeToSystemTime(d, &st)) return L"";
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &st, &utc)) return L"";
    return FormatSystemTimeUtc(utc);
}

JsonValue DescribeTask(IRegisteredTask* task) {
    JsonValue t = JsonValue::object();

    BSTR pathB = nullptr;
    if (SUCCEEDED(task->get_Path(&pathB))) {
        UniqueBstr path(pathB);
        t.set("path", JsonValue::stringWide(BstrToWString(path.get())));
    } else {
        t.set("path", JsonValue::stringWide(L""));
    }

    // Preserve the task XML for action types that do not expose an executable
    // path (e.g. COM handlers), as well as future action types.
    BSTR xml = nullptr;
    if (SUCCEEDED(task->get_Xml(&xml))) {
        UniqueBstr g(xml);
        t.set("xml", JsonValue::stringWide(BstrToWString(g.get())));
    }

    TASK_STATE state = TASK_STATE_UNKNOWN;
    if (SUCCEEDED(task->get_State(&state))) {
        t.set("state", JsonValue::stringWide(std::wstring(TaskStateName(state))));
    }

    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (SUCCEEDED(task->get_Enabled(&enabled))) {
        t.set("enabled", JsonValue::boolean(enabled == VARIANT_TRUE));
    }

    DATE lastRun = 0, nextRun = 0;
    if (SUCCEEDED(task->get_LastRunTime(&lastRun))) {
        t.set("last_run_time_utc", JsonValue::stringWide(DateToIso(lastRun)));
    }
    if (SUCCEEDED(task->get_NextRunTime(&nextRun))) {
        t.set("next_run_time_utc", JsonValue::stringWide(DateToIso(nextRun)));
    }

    LONG missed = 0;
    if (SUCCEEDED(task->get_NumberOfMissedRuns(&missed))) {
        t.set("missed_runs", JsonValue::integer(missed));
    }

    ComPtr<ITaskDefinition> def;
    if (FAILED(task->get_Definition(&def)) || !def) return t;

    // Registration info.
    ComPtr<IRegistrationInfo> reg;
    if (SUCCEEDED(def->get_RegistrationInfo(&reg)) && reg) {
        BSTR author = nullptr, desc = nullptr, date = nullptr;
        if (SUCCEEDED(reg->get_Author(&author))) {
            UniqueBstr g(author);
            t.set("author", JsonValue::stringWide(BstrToWString(g.get())));
        }
        if (SUCCEEDED(reg->get_Description(&desc))) {
            UniqueBstr g(desc);
            t.set("description", JsonValue::stringWide(BstrToWString(g.get())));
        }
        if (SUCCEEDED(reg->get_Date(&date))) {
            UniqueBstr g(date);
            t.set("registration_date", JsonValue::stringWide(BstrToWString(g.get())));
        }
    }

    // Triggers.
    JsonValue triggers = JsonValue::array();
    ComPtr<ITriggerCollection> trigColl;
    if (SUCCEEDED(def->get_Triggers(&trigColl)) && trigColl) {
        LONG n = 0;
        if (SUCCEEDED(trigColl->get_Count(&n))) {
            for (LONG i = 1; i <= n; ++i) {
                ComPtr<ITrigger> trig;
                if (FAILED(trigColl->get_Item(i, &trig)) || !trig) continue;
                JsonValue tr = JsonValue::object();
                TASK_TRIGGER_TYPE2 tt = TASK_TRIGGER_EVENT;
                if (SUCCEEDED(trig->get_Type(&tt))) {
                    tr.set("type", JsonValue::stringWide(std::wstring(TriggerTypeName(tt))));
                }
                VARIANT_BOOL te = VARIANT_FALSE;
                if (SUCCEEDED(trig->get_Enabled(&te))) {
                    tr.set("enabled", JsonValue::boolean(te == VARIANT_TRUE));
                }
                BSTR id = nullptr;
                if (SUCCEEDED(trig->get_Id(&id))) {
                    UniqueBstr g(id);
                    tr.set("id", JsonValue::stringWide(BstrToWString(g.get())));
                }
                triggers.push(std::move(tr));
            }
        }
    }
    t.set("triggers", std::move(triggers));

    // Actions.
    JsonValue actions = JsonValue::array();
    ComPtr<IActionCollection> actColl;
    if (SUCCEEDED(def->get_Actions(&actColl)) && actColl) {
        LONG n = 0;
        if (SUCCEEDED(actColl->get_Count(&n))) {
            for (LONG i = 1; i <= n; ++i) {
                ComPtr<IAction> act;
                if (FAILED(actColl->get_Item(i, &act)) || !act) continue;
                JsonValue a = JsonValue::object();
                TASK_ACTION_TYPE at = TASK_ACTION_EXEC;
                if (SUCCEEDED(act->get_Type(&at))) {
                    a.set("type", JsonValue::stringWide(std::wstring(ActionTypeName(at))));
                }
                if (at == TASK_ACTION_EXEC) {
                    ComPtr<IExecAction> exec;
                    if (SUCCEEDED(act.As(&exec)) && exec) {
                        BSTR p = nullptr, args = nullptr, wd = nullptr;
                        if (SUCCEEDED(exec->get_Path(&p))) {
                            UniqueBstr g(p);
                            a.set("path", JsonValue::stringWide(BstrToWString(g.get())));
                        }
                        if (SUCCEEDED(exec->get_Arguments(&args))) {
                            UniqueBstr g(args);
                            a.set("arguments", JsonValue::stringWide(BstrToWString(g.get())));
                        }
                        if (SUCCEEDED(exec->get_WorkingDirectory(&wd))) {
                            UniqueBstr g(wd);
                            a.set("working_directory", JsonValue::stringWide(BstrToWString(g.get())));
                        }
                    }
                }
                actions.push(std::move(a));
            }
        }
    }
    t.set("actions", std::move(actions));
    return t;
}

void EnumerateTaskFolder(ITaskFolder* folder, int depth, std::vector<JsonValue>& out) {
    if (!folder) return;

    ComPtr<IRegisteredTaskCollection> tasks;
    if (SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks)) && tasks) {
        LONG n = 0;
        if (SUCCEEDED(tasks->get_Count(&n))) {
            for (LONG i = 1; i <= n; ++i) {
                ComPtr<IRegisteredTask> task;
                VARIANT index; VariantInit(&index); index.vt = VT_I4; index.lVal = i;
                if (FAILED(tasks->get_Item(index, &task)) || !task) continue;
                out.push_back(DescribeTask(task.Get()));
            }
        }
    }

    ComPtr<ITaskFolderCollection> folders;
    if (FAILED(folder->GetFolders(0, &folders)) || !folders) return;
    LONG n = 0;
    if (FAILED(folders->get_Count(&n))) return;
    for (LONG i = 1; i <= n; ++i) {
        ComPtr<ITaskFolder> sub;
        VARIANT index; VariantInit(&index); index.vt = VT_I4; index.lVal = i;
        if (FAILED(folders->get_Item(index, &sub)) || !sub) continue;
        EnumerateTaskFolder(sub.Get(), depth + 1, out);
    }
}

JsonValue CollectScheduledTasks() {
    JsonValue tasks = JsonValue::array();
    ComPtr<ITaskService> svc;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITaskService, reinterpret_cast<LPVOID*>(svc.GetAddressOf()));
    if (FAILED(hr) || !svc) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(static_cast<DWORD>(hr))));
        tasks.push(std::move(err));
        return tasks;
    }
    // VT_EMPTY VARIANTs = connect to the local scheduler as current user.
    VARIANT vtEmpty;
    VariantInit(&vtEmpty);
    hr = svc->Connect(vtEmpty, vtEmpty, vtEmpty, vtEmpty);
    if (FAILED(hr)) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(static_cast<DWORD>(hr))));
        tasks.push(std::move(err));
        return tasks;
    }
    ComPtr<ITaskFolder> root;
    BSTR rootPath = SysAllocString(L"\\");
    hr = svc->GetFolder(rootPath, &root);
    if (rootPath) SysFreeString(rootPath);
    if (FAILED(hr) || !root) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(static_cast<DWORD>(hr))));
        tasks.push(std::move(err));
        return tasks;
    }
    std::vector<JsonValue> all;
    EnumerateTaskFolder(root.Get(), 0, all);
    // Deterministic order by path.
    std::sort(all.begin(), all.end(), [](const JsonValue& a, const JsonValue& b) {
        const JsonValue* pa = a.find("path");
        const JsonValue* pb = b.find("path");
        std::string sa = pa && pa->isString() ? pa->asString() : "";
        std::string sb = pb && pb->isString() ? pb->asString() : "";
        return sa < sb;
    });
    for (auto& t : all) tasks.push(std::move(t));
    return tasks;
}

// ---- auto-start non-Microsoft services ------------------------------------

JsonValue CollectNonMicrosoftAutoStart(const JsonValue& svcList) {
    JsonValue out = JsonValue::array();
    if (!svcList.isArray()) return out;

    for (const JsonValue& s : svcList.asArray()) {
        const JsonValue* startCode = s.find("start_type_code");
        const JsonValue* startType = s.find("start_type");
        bool isAuto = false;
        if (startCode && startCode->isNumber()) {
            // SERVICE_AUTO_START = 2 (delayed-auto also reports 2).
            isAuto = startCode->asInt() == 2;
        } else if (startType && startType->isString()) {
            const std::string& v = startType->asString();
            isAuto = (v == "AUTO" || v == "AUTO_DELAYED");
        }
        if (!isAuto) continue;

        const JsonValue* bin = s.find("binary_path");
        std::wstring binPath = (bin && bin->isString()) ? Utf8ToWide(bin->asString()) : L"";
        std::wstring exe = ExtractExePath(binPath);
        DWORD expandedSize = ExpandEnvironmentStringsW(exe.c_str(), nullptr, 0);
        if (expandedSize) {
            std::vector<wchar_t> expanded(expandedSize);
            if (ExpandEnvironmentStringsW(exe.c_str(), expanded.data(), expandedSize))
                exe.assign(expanded.data());
        }
        std::wstring company = GetFileCompanyName(exe);
        bool isMs = ContainsCi(company, L"Microsoft");

        JsonValue e = JsonValue::object();
        const JsonValue* name = s.find("name");
        const JsonValue* disp = s.find("display_name");
        const JsonValue* state = s.find("state");
        if (name) e.set("name", *name);
        if (disp) e.set("display_name", *disp);
        if (state) e.set("state", *state);
        if (startType) e.set("start_type", *startType);
        if (bin) e.set("binary_path", *bin);
        e.set("exe_path", JsonValue::stringWide(exe));
        e.set("company", JsonValue::stringWide(company));
        e.set("is_microsoft", JsonValue::boolean(isMs));
        if (!isMs) out.push(std::move(e));
    }
    return out;
}

}  // namespace

JsonValue CollectPersistCheck(const JsonValue& svcList) {
    JsonValue root = JsonValue::object();
    root.set("registry", CollectRunKeys());
    root.set("startup_folders", CollectStartupFolders());
    root.set("scheduled_tasks", CollectScheduledTasks());
    root.set("auto_start_non_microsoft_services", CollectNonMicrosoftAutoStart(svcList));
    return root;
}

}  // namespace cm
