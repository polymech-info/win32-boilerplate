#pragma once

#if defined(_WIN32)

#include <windows.h> // For ULONGLONG, SIZE_T etc.

struct MemoryUsageWin32
{
    SIZE_T workingSet = 0;
    SIZE_T privateBytes = 0;
};

struct CpuTimesWin32
{
    ULONGLONG kernel = 0;
    ULONGLONG user = 0;
};

MemoryUsageWin32 CaptureMemoryUsageWin32();
void LogMemoryUsageWin32(const char *label, const MemoryUsageWin32 &usage);
CpuTimesWin32 CaptureCpuTimesWin32();
double ComputeCpuUsagePercentWin32(const CpuTimesWin32 &begin,
                                   const CpuTimesWin32 &end,
                                   double elapsedSeconds);

#endif
