$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\..\.."
$exe = "$root\dist\cs2voice.exe"
$ids = @{}
Get-Content "$PSScriptRoot\identity.txt" | Where-Object { $_ -match '^\s*(\w+)\s*=\s*(.+?)\s*$' } | ForEach-Object { $ids[$Matches[1]] = $Matches[2] }
foreach ($k in 'Name', 'Publisher', 'PublisherName') {
    if (-not $ids[$k]) { throw "identity.txt has no $k" }
}
$version = (Get-Item $exe).VersionInfo.ProductVersion
$out = "$root\build\msix"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$out\Assets" | Out-Null
Copy-Item $exe $out
Copy-Item "$PSScriptRoot\Assets\*" "$out\Assets"
$manifest = (Get-Content "$PSScriptRoot\AppxManifest.xml" -Raw).Replace('@NAME@', $ids.Name).Replace('@PUBLISHER@', $ids.Publisher).Replace('@PUBLISHER_NAME@', $ids.PublisherName).Replace('@VERSION@', $version)
[IO.File]::WriteAllText("$out\AppxManifest.xml", $manifest, [Text.UTF8Encoding]::new($false))
$tool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" | Sort-Object FullName | Select-Object -Last 1
& $tool.FullName pack /o /d $out /p "$root\dist\cs2voice.msix" | Select-Object -Last 2
if ($LASTEXITCODE) { throw "makeappx failed" }
