#include "summary_file.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace xrpo {

namespace {

// Marque (ou démarque) le fichier comme "à supprimer à la fermeture".
bool setDeleteOnClose(HANDLE handle, bool enable) {
    // Initialisation sans nommer le champ : son nom, "DeleteFile", est aussi une macro de windows.h.
    FILE_DISPOSITION_INFO info = {static_cast<BOOLEAN>(enable ? 1 : 0)};
    return SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info)) != 0;
}

}  // namespace

bool SummaryFile::open(const std::wstring& folder, const std::wstring& fileName) {
    close();
    if (folder.empty() || fileName.empty()) return false;
    CreateDirectoryW(folder.c_str(), nullptr);  // sans effet s'il existe déjà
    std::wstring path = folder;
    if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += fileName;
    // Partage en lecture : le fichier peut être ouvert dans le Bloc-notes pendant le jeu.
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    m_handle = handle;
    m_path = path;
    m_kept = false;
    setDeleteOnClose(handle, true);
    return true;
}

void SummaryFile::keep() {
    if (!m_handle || m_kept) return;
    setDeleteOnClose(static_cast<HANDLE>(m_handle), false);
    m_kept = true;
}

void SummaryFile::write(const char* text, size_t length) {
    if (!m_handle || !text) return;
    const HANDLE handle = static_cast<HANDLE>(m_handle);
    LARGE_INTEGER start;
    start.QuadPart = 0;
    if (!SetFilePointerEx(handle, start, nullptr, FILE_BEGIN)) return;
    DWORD written = 0;
    WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
    SetEndOfFile(handle);  // au cas où le nouveau texte serait plus court que l'ancien
}

void SummaryFile::close() {
    if (!m_handle) return;
    CloseHandle(static_cast<HANDLE>(m_handle));
    m_handle = nullptr;
    // Filet de sécurité si le système de fichiers n'a pas accepté la suppression à la fermeture.
    if (!m_kept) DeleteFileW(m_path.c_str());
}

std::string LocalTimeText() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

int LocalClockMinutes() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return t.wHour * 60 + t.wMinute;
}

int LocalClockSeconds() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return (t.wHour * 60 + t.wMinute) * 60 + t.wSecond;
}

std::wstring LocalTimeFileStamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

}  // namespace xrpo
