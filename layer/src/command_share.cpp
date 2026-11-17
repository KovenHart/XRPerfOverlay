#include "command_share.h"

#include <windows.h>

#include <cstring>

namespace xrpo {

namespace {
constexpr unsigned long long kOnlineMs = 3000;  // XRPerfSensors renouvelle son horodatage plusieurs fois par seconde
}

CommandShare::~CommandShare() {
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
}

bool CommandShare::open() {
    if (m_view) return true;
    const ULONGLONG now = GetTickCount64();
    if (m_lastAttemptMs != 0 && now - m_lastAttemptMs < 2000) return false;  // une tentative toutes les 2 s au plus
    m_lastAttemptMs = now;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kCommandBlockName);
    if (!mapping) return false;
    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(CommandBlock));
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    m_mapping = mapping;
    m_view = static_cast<CommandBlock*>(view);
    return true;
}

CommandShare::State CommandShare::read() {
    State s;
    if (!open()) return s;
    const volatile CommandBlock* v = m_view;
    if (v->magic != kCommandBlockMagic || v->version != kCommandBlockVersion) return s;
    const ULONGLONG tick = v->sensorsTickMs, now = GetTickCount64();
    if (tick == 0 || now < tick || now - tick > kOnlineMs) return s;
    // `sequence` est impair pendant que XRPerfSensors écrit les drapeaux et le nom du preset.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const uint32_t before = v->sequence;
        MemoryBarrier();
        const uint32_t flags = v->flags;
        char preset[sizeof(s.preset)];
        for (size_t i = 0; i < sizeof(preset); ++i) preset[i] = v->preset[i];
        MemoryBarrier();
        if (before != v->sequence || (before & 1u) != 0) continue;
        s.online = true;
        s.flags = flags;
        preset[sizeof(preset) - 1] = 0;
        for (size_t i = 0; i < sizeof(preset); ++i) {
            const unsigned char c = static_cast<unsigned char>(preset[i]);
            s.preset[i] = c == 0 ? 0 : (c < 32 || c > 126 ? '?' : static_cast<char>(c));
            if (c == 0) break;
        }
        return s;
    }
    return s;
}

bool CommandShare::post(TouchAction action) {
    if (!read().online) return false;
    const LONG n = InterlockedIncrement(reinterpret_cast<volatile LONG*>(&m_view->posted));
    const uint32_t number = static_cast<uint32_t>(n);
    volatile uint32_t* slot = &m_view->commands[(number - 1u) % kCommandSlots];
    MemoryBarrier();
    *slot = (number << 8) | (static_cast<uint32_t>(action) & 0xFFu);
    MemoryBarrier();
    return true;
}

}  // namespace xrpo
