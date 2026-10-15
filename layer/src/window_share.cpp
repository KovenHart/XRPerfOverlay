#include "window_share.h"

#include <windows.h>

#include <cstring>

#include "shared_block.h"

namespace xrpo {

WindowShareReader::~WindowShareReader() {
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
}

void WindowShareReader::tryOpen() {
    const ULONGLONG now = GetTickCount64();
    if (m_lastAttemptMs != 0 && now - m_lastAttemptMs < 2000) return;  // une tentative toutes les 2 s au plus
    m_lastAttemptMs = now;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kWindowShareName);
    if (!mapping) return;
    // Taille exacte : une mémoire plus petite (autre version de XRPerfSensors) n'est pas ouverte.
    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kWindowShareBytes);
    if (!view) {
        CloseHandle(mapping);
        return;
    }
    m_mapping = mapping;
    m_view = static_cast<uint8_t*>(view);
}

void WindowShareReader::markWanted() {
    if (!m_view) tryOpen();
    if (!m_view) return;
    volatile WindowShareHeader* header = reinterpret_cast<volatile WindowShareHeader*>(m_view);
    if (header->magic != kWindowShareMagic || header->version != kWindowShareVersion) return;
    header->readerTickMs = GetTickCount64();
}

bool WindowShareReader::readNew(std::vector<uint8_t>& pixels, int& width, int& height) {
    if (!m_view) tryOpen();
    if (!m_view) return false;
    const volatile WindowShareHeader* header = reinterpret_cast<const volatile WindowShareHeader*>(m_view);
    if (header->magic != kWindowShareMagic || header->version != kWindowShareVersion) return false;
    // Même protocole que les autres mémoires : `sequence` est impair pendant une écriture. Une image en
    // cours d'écriture est simplement relue au passage suivant.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const uint32_t before = header->sequence;
        if (before & 1u) return false;
        MemoryBarrier();
        const uint32_t frame = header->frame;
        if (m_hasFrame && frame == m_lastFrame) return false;
        const uint32_t w = header->width, h = header->height;
        if (w == 0 || h == 0 || w > static_cast<uint32_t>(kWindowMaxWidth) || h > static_cast<uint32_t>(kWindowMaxHeight)) return false;
        pixels.resize(static_cast<size_t>(w) * h * 4);
        std::memcpy(pixels.data(), m_view + sizeof(WindowShareHeader), pixels.size());
        MemoryBarrier();
        if (header->sequence != before) continue;
        m_lastFrame = frame;
        m_hasFrame = true;
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        return true;
    }
    return false;
}

}  // namespace xrpo
