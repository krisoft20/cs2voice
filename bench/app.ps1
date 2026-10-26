param(
    [int]$Runs = 5,
    [string]$Exe = "D:\Projects\cs2voice\build\release\bin\Release\cs2voice.exe"
)

$log = Join-Path $env:LOCALAPPDATA "cs2voice\cs2voice.log"
$shot = Join-Path $env:TEMP "cs2voice-bench.png"

function Milestone([string]$text, [string]$pattern) {
    $m = [regex]::Match($text, "^\s*(\d+)\s+$pattern", "Multiline")
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return $null
}

$ready = @(); $demos = @(); $wall = @(); $web = @(); $timedOut = 0

Write-Host "cs2voice app bench: $Runs runs"
for ($i = 0; $i -lt $Runs; $i++) {
    Get-Process cs2voice -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 200
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $Exe -ArgumentList @("--screenshot", "`"$shot`"", "--wait", "3000") -PassThru
    if (-not $p.WaitForExit(30000)) { $p.Kill(); $timedOut++ }
    $sw.Stop()
    $wall += [int]$sw.Elapsed.TotalMilliseconds

    $text = Get-Content $log -Raw -ErrorAction SilentlyContinue
    $a = Milestone $text '\[web\] ready'
    $b = Milestone $text '\[bridge\] ui ready'
    $c = Milestone $text '\[bridge\] \d+ demos found'
    if ($a) { $web += $a }
    if ($b) { $ready += $b }
    if ($c) { $demos += $c }
}
Get-Process cs2voice -ErrorAction SilentlyContinue | Stop-Process -Force

function Stat([string]$name, $values) {
    if ($values.Count -eq 0) { Write-Host ("  {0,-30} -" -f $name); return }
    $min = ($values | Measure-Object -Minimum).Minimum
    $max = ($values | Measure-Object -Maximum).Maximum
    $avg = [math]::Round((($values | Measure-Object -Average).Average), 1)
    Write-Host ("  {0,-30} best {1,5} ms   avg {2,6} ms   worst {3,5} ms" -f $name, $min, $avg, $max)
}

Write-Host ""
Stat "webview process up" $web
Stat "ui ready (first paint)" $ready
Stat "demo list scanned" $demos
Stat "process wall (headless run)" $wall

# memory with the normal window running
$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 6
$p.Refresh()
$ws = [math]::Round($p.WorkingSet64 / 1MB, 1)
$priv = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
$threads = $p.Threads.Count
$handles = $p.HandleCount
Write-Host ""
Write-Host ("  working set {0} MB   private {1} MB   threads {2}   handles {3}" -f $ws, $priv, $threads, $handles)
$p.Kill()