# Launch pm-image with Chat dock focused; seeds queue + chat context from Explorer multi-select.
# Semicolon-joins paths ( ';' is invalid in Windows filenames ). Uses CreateNoWindow — no console flash.
param(
    [Parameter(Mandatory)][string]$MediaImg,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Paths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($null -eq $Paths -or $Paths.Length -eq 0) {
    exit 1
}

$clean = @($Paths | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
if ($clean.Length -eq 0) {
    exit 1
}

$joined = $clean -join ';'

$p = New-Object System.Diagnostics.Process
$p.StartInfo.FileName = $MediaImg
$p.StartInfo.Arguments = "resize --ui-next --chat --src `"$joined`""
$p.StartInfo.UseShellExecute = $false
$p.StartInfo.CreateNoWindow = $true
$p.StartInfo.RedirectStandardOutput = $false
$p.StartInfo.RedirectStandardError = $false
[void]$p.Start()
$p.WaitForExit()
exit $p.ExitCode
