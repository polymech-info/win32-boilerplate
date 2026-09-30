// Stable machine fingerprint for trial + license binding (Windows).

#if defined(_WIN32)

#include "machine_fingerprint.hpp"

#include <picosha2.h>

#include <Windows.h>

#include <cstdio>
#include <string>

namespace media::win {
namespace {

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string read_machine_guid_utf8() {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &hkey)
        != ERROR_SUCCESS)
        return {};
    wchar_t buf[384]{};
    DWORD sz = sizeof(buf);
    DWORD typ = 0;
    LSTATUS st =
        RegQueryValueExW(hkey, L"MachineGuid", nullptr, &typ, reinterpret_cast<LPBYTE>(buf), &sz);
    RegCloseKey(hkey);
    if (st != ERROR_SUCCESS || typ != REG_SZ)
        return {};
    return wide_to_utf8(buf);
}

bool volume_serial_hex(std::string& out) {
    wchar_t sys[MAX_PATH]{};
    if (!GetWindowsDirectoryW(sys, MAX_PATH))
        return false;
    if (sys[0] == 0)
        return false;
    wchar_t root[8]{};
    root[0] = sys[0];
    root[1] = L':';
    root[2] = L'\\';
    root[3] = 0;
    DWORD serial = 0;
    if (!GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        return false;
    char tmp[16]{};
    snprintf(tmp, sizeof(tmp), "%08lx", static_cast<unsigned long>(serial));
    out.assign(tmp);
    return true;
}

} // namespace

std::string machine_fingerprint_hex() {
    std::string mg = read_machine_guid_utf8();
    std::string vol;
    if (!volume_serial_hex(vol))
        vol = "unknown";
    const std::string combined = mg + "|" + vol;
    std::string hex;
    picosha2::hash256_hex_string(combined.begin(), combined.end(), hex);
    return hex;
}

} // namespace media::win

#endif
