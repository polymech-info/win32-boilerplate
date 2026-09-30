Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-10)} | Where-Object { $_.ProviderName -match 'Application Error|Windows Error Reporting|\.NET Runtime' -or $_.Message -match 'pm-image.exe' } | Select-Object -First 6 TimeCreated,ProviderName,Id,Message | Format-List

where.exe dumpchk 2>$null; where.exe minidump_stackwalk 2>$null; where.exe WinDbgX 2>$null; Get-ChildItem "C:\ProgramData\Microsoft\Windows\WER\ReportArchive" -Directory -Filter "AppCrash_pm-image.exe*" | Sort-Object LastWriteTime -Descending | Select-Object -First 3 FullName,LastWriteTime
