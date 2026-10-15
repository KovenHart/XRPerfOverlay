#include "panel_share.h"

#include <windows.h>

#include <cstring>

#include "panel.h"
#include "shared_block.h"

namespace xrpo {

namespace {

const size_t kPixelBytes = static_cast<size_t>(Panel::kWidth) * Panel::kHeight * 4;
const size_t kShareBytes = sizeof(PanelShareHeader) + kPixelBytes;

}  // namespace

PanelShare::~PanelShare() {
    if (m_view) {
        // Fin de session : la fenêtre du bureau sait tout de suite qu'il n'y a plus de jeu (sans
        // cela, elle ne pourrait pas distinguer un jeu fermé d'un jeu en plein chargement).
        reinterpret_cast<volatile PanelShareHeader*>(m_view)->writerTickMs = 0;
        UnmapViewOfFile(m_view);
    }
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
}

void PanelShare::tryOpen() {
    const ULONGLONG now = GetTickCount64();
    if (m_lastAttemptMs != 0 && now - m_lastAttemptMs < 2000) return;  // une tentative toutes les 2 s au plus
    m_lastAttemptMs = now;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kPanelShareName);
    if (!mapping) return;
    // La taille demandée est exacte : si la mémoire existante est plus petite (autre version du
    // programme de capteurs), l'ouverture échoue et nous n'écrivons jamais hors limites.
    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kShareBytes);
    if (!view) {
        CloseHandle(mapping);
        return;
    }
    m_mapping = mapping;
    m_view = static_cast<uint8_t*>(view);
}

bool PanelShare::wanted() {
    if (!m_view) tryOpen();
    if (!m_view) return false;
    const volatile PanelShareHeader* header = reinterpret_cast<const volatile PanelShareHeader*>(m_view);
    if (header->magic != kPanelShareMagic || header->version != kPanelShareVersion) return false;
    // Le lecteur rafraîchit son horodatage plusieurs fois par seconde tant que sa fenêtre est visible.
    const ULONGLONG reader = header->readerTickMs;
    const ULONGLONG now = GetTickCount64();
    return reader != 0 && now >= reader && now - reader < 3000;
}

void PanelShare::publish(const uint8_t* pixels, int contentHeight) {
    if (!m_view || !pixels) return;
    volatile PanelShareHeader* header = reinterpret_cast<volatile PanelShareHeader*>(m_view);
    // Séquence impaire pendant la copie : le lecteur sait qu'il doit relire.
    const uint32_t sequence = header->sequence | 1u;
    header->sequence = sequence;
    MemoryBarrier();
    std::memcpy(m_view + sizeof(PanelShareHeader), pixels, kPixelBytes);
    header->contentHeight = static_cast<uint32_t>(contentHeight);
    header->writerTickMs = GetTickCount64();
    MemoryBarrier();
    header->sequence = sequence + 1u;
}

}  // namespace xrpo
