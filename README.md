# client-manager

Windows C++ inventory tool (MSVC, Win11). Reads a config path from `argv[1]`,
writes one JSON report object to `argv[2]`.

```bat
client-manager.exe <config.json> <report.json>
```

Real Win32, COM, IP Helper, and WMI — no stubs. Build order: layout +
`sys.info`, then `proc.list`, then `persist.check`, then the remaining
collectors (`net.conn`, `svc.list`, `software.list`, `fs.list`).

## Layout

```
CMakeLists.txt        CMake build (MSVC, C++17)
build.bat             MSVC build script (CMake preferred, cl fallback)
config.example.json   Example config
src/
  main.cpp            argv handling, config load, collector orchestration, report write
  json.hpp            JSON value + writer (header-only, UTF-8)
  config.hpp          Minimal JSON config parser
  util.hpp/.cpp       Time formatting, file I/O, version-info company lookup
  wmi.hpp/.cpp        WMI ROOT\CIMV2 query helper (COM)
  sysinfo.hpp/.cpp    sys.info
  proc.hpp/.cpp       proc.list
  net.hpp/.cpp        net.conn
  svc.hpp/.cpp        svc.list
  persist.hpp/.cpp    persist.check
  software.hpp/.cpp   software.list
  fs.hpp/.cpp         fs.list
```

## Build (Win11, MSVC)

Requires Visual Studio 2022 with "Desktop development with C++"
(or CMake + the MSVC toolchain).

```bat
build.bat
```

Or manually:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\client-manager.exe`.

Run the portable JSON/config test with `ctest --test-dir build -C Release
--output-on-failure`. The Windows CI workflow also exercises the executable
and validates the report structure on a Windows runner.

## Usage

```bat
client-manager.exe config.example.json report.json
```

Config (`config.example.json`):

```json
{
  "fs_paths": ["C:\\Windows\\System32", "C:\\Program Files"],
  "fs_recursive": false,
  "include": {
    "sys_info": true,
    "proc_list": true,
    "net_conn": true,
    "svc_list": true,
    "persist_check": true,
    "software_list": true,
    "fs_list": true
  }
}
```

- `fs_paths` — paths enumerated by `fs.list` (dirs list entries, files report
  themselves). Alias: `paths`.
- `fs_recursive` — descend into subdirectories (reparse points skipped).
  Alias: `recursive`.
- `include.*` — toggle collectors (all default `true`).

## Report schema (one JSON object)

Top-level keys: `generated_at_utc`, `sys.info`, `proc.list`, `net.conn`,
`svc.list`, `persist.check`, `software.list`, `fs.list` (omitted only if
disabled via `include`).

### sys.info

- `os`: `product_name`, `display_version`, `edition_id`, `release_id`,
  `build_lab_ex`, `major`, `minor`, `build`, `platform_id`,
  `current_build_number`, `ubr`, `build_full` (e.g. `22631.4169`),
  `install_date_unix`, `architecture`.
  Version via `RtlGetVersion` (ntdll) + `CurrentVersion` registry keys —
  accurate on Win11 where `GetVersionEx` lies.
- `cpu`: `model`, `manufacturer`, `cores`, `logical_processors`,
  `enabled_cores`, `max_clock_mhz`, `architecture` (WMI `Win32_Processor`,
  registry fallback), `system_logical_processors`, `page_size`.
- `memory`: `total_bytes`, `available_bytes`, virtual/page-file sizes,
  `load_percent` (`GlobalMemoryStatusEx`).
- `disks[]`: `drive`, `root`, `type`, `volume_label`, `filesystem`,
  `volume_flags`, `total_bytes`, `free_bytes`, `available_bytes`, `used_bytes`
  (`GetLogicalDrives` + `GetDiskFreeSpaceExW` + `GetVolumeInformationW`).
- `nics[]`: `name`, `description`, `if_index`, `mtu`, `oper_status`,
  `if_type`, `mac`, `ipv4[]`, `ipv6[]`, `dns_servers[]`, `gateways[]`
  (IP Helper `GetAdaptersAddresses`).
- `uptime_seconds`, `boot_time_utc` (`GetTickCount64`).
- `user` (`GetUserNameExW` NameSamCompatible), `logged_on_users[]`
  (WMI `Win32_LoggedOnUser`), `hostname`, `hostname_dns`, `domain`,
  `domain_machine` (`GetComputerNameExW`).

### proc.list[]

`pid`, `ppid`, `name`, `image_path`, `user`, `cmdline`, `start_time_utc`.
Toolhelp snapshot (`CreateToolhelp32Snapshot`) + `QueryFullProcessImageNameW`
+ `GetProcessTimes` + token SID lookup (`LookupAccountSidW`) for the user;
cmdline/start-time fallback from WMI `Win32_Process`. Sorted by pid.

### net.conn[]

`proto` (`TCP`/`UDP`), `family` (`IPv4`/`IPv6`), `local_addr`, `local_port`,
`remote_addr`, `remote_port`, `state`, `pid`. IP Helper
`GetExtendedTcpTable` with `TCP_TABLE_OWNER_PID_ALL` and
`GetExtendedUdpTable` with `UDP_TABLE_OWNER_PID`.
UDP rows have empty remote + `NONE` state. Sorted by proto/port/pid.

### svc.list[]

`name`, `display_name`, `state`, `state_code`, `start_type`
(`BOOT`/`SYSTEM`/`AUTO`/`AUTO_DELAYED`/`MANUAL`/`DISABLED`),
`start_type_code`, `delayed_auto_start`, `binary_path`, `start_name`,
`has_config`. SCM `EnumServicesStatusExW` + `QueryServiceConfigW`;
delayed-auto via `DelayedAutoStart` registry value. Sorted by name.

### persist.check

- `registry`: `run[]`, `run_once[]` — `{hive, path, name, data,
  expanded_data, type}` from HKLM/HKCU `...\CurrentVersion\Run`/`RunOnce`
  (+ Wow6432Node).
- `startup_folders[]`: `{scope, path, entries[{name, path, size, mtime_utc,
  is_dir}]}` for user + common Startup (`SHGetFolderPathW`).
- `scheduled_tasks[]`: `{path, state, enabled, last_run_time_utc,
  next_run_time_utc, missed_runs, author, description, registration_date,
  xml, triggers[{type, enabled, id}], actions[{type, path, arguments,
  working_directory}]}` via Task Scheduler COM (`ITaskService`,
  recursive folders). `xml` preserves details for non-executable action types.
- `auto_start_non_microsoft_services[]`: auto-start services from `svc.list`
  whose binary version-resource `CompanyName` does not contain "Microsoft"
  (`{name, display_name, state, start_type, binary_path, exe_path, company,
  is_microsoft}`). This is a version-metadata heuristic, not signature
  verification; missing or misleading company metadata can cause false positives.

### software.list[]

`name`, `version`, `publisher`, `install_date`, `install_location`,
`uninstall_string`, `quiet_uninstall_string`, `hive`, `key` from HKLM/HKCU
`...\Uninstall` (+ Wow6432Node). Entries without `DisplayName` skipped.
Sorted by name/version.

### fs.list[]

Per config path: `{path, exists, is_dir, entries[{name, path, size,
mtime_utc, is_dir}]}` via `FindFirstFileW`. Missing paths carry an `error`.

## Notes

- Run as Administrator for complete process/service/task visibility;
  collectors may return incomplete data or errors on access denied. Windows
  access controls and protected processes still limit what administrators can see.
- Reports may contain sensitive usernames, command lines, paths and network
  details; restrict access to the output file.
- Output is UTF-8 JSON, 2-space indent.
- Exit codes: `0` success, `1` config/output error, `2` usage.
