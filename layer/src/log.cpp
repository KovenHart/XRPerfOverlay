#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace xrpo {

namespace {

std::mutex g_logMutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
bool g_logOpened = false;

void openLog() {
    g_logOpened = true;
    wchar_t base[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring dir = std::wstring(base) + L"\\XRPerfOverlay";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\XRPerfOverlay.log";

    // On repart d'un fichier vide quand il dépasse 256 Ko.
    WIN32_FILE_ATTRIBUTE_DATA info;
    DWORD disposition = OPEN_ALWAYS;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && (info.nFileSizeHigh != 0 || info.nFileSizeLow > 256 * 1024))
        disposition = CREATE_ALWAYS;
    g_logFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, disposition,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
}

}  // namespace

void Log(const char* format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[640];
    const int len = std::snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u [%lu] %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                                  st.wMinute, st.wSecond, static_cast<unsigned long>(GetCurrentProcessId()), message);
    if (len <= 0) return;

    OutputDebugStringA(line);
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logOpened) openLog();
    if (g_logFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_logFile, line, static_cast<DWORD>(len < static_cast<int>(sizeof(line)) ? len : sizeof(line) - 1), &written, nullptr);
    }
}

}  // namespace xrpo
