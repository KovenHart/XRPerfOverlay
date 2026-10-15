#include "sensors_reader.h"

#include <windows.h>

#include <cstring>

namespace xrpo {

SensorsReader::~SensorsReader() {
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
}

void SensorsReader::tryOpen() {
    const ULONGLONG now = GetTickCount64();
    if (m_lastAttemptMs != 0 && now - m_lastAttemptMs < 2000) return;  // une tentative toutes les 2 s au plus
    m_lastAttemptMs = now;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedBlockName);
    if (!mapping) return;
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SensorBlock));
    if (!view) {
        CloseHandle(mapping);
        return;
    }
    m_mapping = mapping;
    m_view = static_cast<const SensorBlock*>(view);
}

bool SensorsReader::read(SensorBlock& out) {
    if (!m_view) tryOpen();
    if (!m_view) return false;

    // L'écrivain passe `sequence` à une valeur impaire pendant qu'il écrit : on relit tant que la
    // copie n'est pas cohérente (quelques essais suffisent, l'écriture dure une microseconde).
    for (int attempt = 0; attempt < 4; ++attempt) {
        const volatile SensorBlock* v = m_view;
        const uint32_t before = v->sequence;
        MemoryBarrier();
        std::memcpy(&out, m_view, sizeof(SensorBlock));
        MemoryBarrier();
        const uint32_t after = v->sequence;
        if (before == after && (before & 1u) == 0) {
            if (out.magic != kSharedBlockMagic || out.version != kSharedBlockVersion) return false;
            const ULONGLONG now = GetTickCount64();
            return now >= out.tickMs && now - out.tickMs < 3000;
        }
    }
    return false;
}

}  // namespace xrpo
