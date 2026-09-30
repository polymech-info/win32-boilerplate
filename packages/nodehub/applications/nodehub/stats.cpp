#include "stats.h"
#include "Logging.h" // For LOG_INFO

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "Psapi.lib")

MemoryUsageWin32 CaptureMemoryUsageWin32()
{
    MemoryUsageWin32 usage;
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
    {
        usage.workingSet = pmc.WorkingSetSize;
        usage.privateBytes = pmc.PrivateUsage;
    }
    return usage;
}

void LogMemoryUsageWin32(const char *label, const MemoryUsageWin32 &usage)
{
    double workingSetMb = static_cast<double>(usage.workingSet) / (1024.0 * 1024.0);
    double privateMb = static_cast<double>(usage.privateBytes) / (1024.0 * 1024.0);
    LOG_INFO("[Memory] {} - Working Set: {:.2f} MB, Private Bytes: {:.2f} MB", label, workingSetMb, privateMb);
}

CpuTimesWin32 CaptureCpuTimesWin32()
{
    CpuTimesWin32 result;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
    {
        ULARGE_INTEGER k{};
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        ULARGE_INTEGER u{};
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        result.kernel = k.QuadPart;
        result.user = u.QuadPart;
    }
    return result;
}
double ComputeCpuUsagePercentWin32(const CpuTimesWin32 &begin,
                                          const CpuTimesWin32 &end,
                                          double elapsedSeconds)
{
    if (elapsedSeconds <= 0.0)
        return 0.0;
    ULONGLONG deltaKernel = end.kernel - begin.kernel;
    ULONGLONG deltaUser = end.user - begin.user;
    double cpuSeconds = static_cast<double>(deltaKernel + deltaUser) / 10'000'000.0;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    double cores = static_cast<double>(info.dwNumberOfProcessors > 0 ? info.dwNumberOfProcessors : 1);
    double usage = (cpuSeconds / (elapsedSeconds * cores)) * 100.0;
    if (usage < 0.0)
        usage = 0.0;
    return usage;
}
#endif
