#include "settings_reader.h"

#include <windows.h>

#include <cstring>

namespace xrpo {

SettingsReader::~SettingsReader() {
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
}

void SettingsReader::tryOpen() {
    const ULONGLONG now = GetTickCount64();
    if (m_lastAttemptMs != 0 && now - m_lastAttemptMs < 2000) return;  // une tentative toutes les 2 s au plus
    m_lastAttemptMs = now;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kSettingsBlockName);
    if (!mapping) return;
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SettingsBlock));
    if (!view) {
        CloseHandle(mapping);
        return;
    }
    m_mapping = mapping;
    m_view = static_cast<const SettingsBlock*>(view);
}

bool SettingsReader::read(SettingsBlock& out) {
    if (!m_view) tryOpen();
    if (!m_view) return false;

    // Même protocole que pour les capteurs : `sequence` est impair pendant une écriture.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const volatile SettingsBlock* v = m_view;
        const uint32_t before = v->sequence;
        MemoryBarrier();
        std::memcpy(&out, m_view, sizeof(SettingsBlock));
        MemoryBarrier();
        const uint32_t after = v->sequence;
        if (before == after && (before & 1u) == 0) return out.magic == kSettingsBlockMagic && out.version == kSettingsBlockVersion;
    }
    return false;
}

}  // namespace xrpo
