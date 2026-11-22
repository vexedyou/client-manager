param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$work = Join-Path $env:TEMP ("client-manager-test-" + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $sample = Join-Path $work 'sample.txt'
    [IO.File]::WriteAllText($sample, 'hello')
    $config = Join-Path $work 'config.json'
    $report = Join-Path $work 'report.json'
    @{ fs_paths = @($work); fs_recursive = $false; include = @{ svc_list = $false } } |
        ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $config
    & $Executable $config $report
    if ($LASTEXITCODE -ne 0) { throw "client-manager failed: $LASTEXITCODE" }
    $result = Get-Content -Raw -Encoding utf8 $report | ConvertFrom-Json
    foreach ($key in @('sys.info','proc.list','net.conn','persist.check','software.list','fs.list')) {
        if (-not $result.PSObject.Properties[$key]) { throw "Missing report key $key" }
    }
    if ($result.PSObject.Properties['svc.list']) { throw 'Disabled svc.list was emitted' }
    if (-not $result.'sys.info'.hostname) { throw 'Missing hostname' }
    if (@($result.'proc.list').Count -lt 1) { throw 'No processes found' }
    if (-not ($result.'fs.list'[0].entries | Where-Object { $_.name -eq 'sample.txt' -and $_.size -eq 5 })) {
        throw 'Expected filesystem entry was not reported'
    }
    if (-not $result.'persist.check'.PSObject.Properties['auto_start_non_microsoft_services']) {
        throw 'Missing persistence service inventory when svc.list is disabled'
    }
} finally {
    Remove-Item -Recurse -Force $work
}
