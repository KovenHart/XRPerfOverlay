// Swapchains créées par le jeu : format, taille, et image rendue en dernier. Sert à l'enregistrement
// vidéo, qui copie l'image d'un œil au moment où le jeu soumet son image (xrEndFrame). Rempli par les
// fonctions xrCreateSwapchain, xrDestroySwapchain, xrAcquireSwapchainImage et xrReleaseSwapchainImage
// de la layer, seulement si l'enregistrement vidéo est permis ([video] capture=1).
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xr.h"

namespace xrpo {

class SwapchainRegistry {
  public:
    struct Info {
        int64_t format = 0;  // format DXGI choisi par le jeu
        uint32_t width = 0, height = 0, arraySize = 1, mipCount = 1, sampleCount = 1;
        int64_t released = -1;  // image rendue en dernier au runtime (-1 : aucune)
        std::vector<void*> images;  // textures, lues une fois au besoin
        bool imagesRead = false;
    };

    void created(XrSwapchain swapchain, const XrSwapchainCreateInfo& info) {
        // Seulement les images de couleur : la profondeur ne sert pas à la vidéo.
        if ((info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            destroyed(swapchain);  // poignée réutilisée : l'ancienne entrée ne vaut plus
            return;
        }
        Entry entry;
        entry.info.format = info.format;
        entry.info.width = info.width;
        entry.info.height = info.height;
        entry.info.arraySize = info.arraySize ? info.arraySize : 1;
        entry.info.mipCount = info.mipCount ? info.mipCount : 1;
        entry.info.sampleCount = info.sampleCount ? info.sampleCount : 1;
        std::lock_guard<std::mutex> lock(m_mutex);
        // Jeu qui ne détruit jamais ses swapchains : au-delà, les nouvelles ne sont plus suivies (les
        // anciennes, peut-être encore utilisées, restent).
        if (m_entries.size() >= 1024 && m_entries.find(key(swapchain)) == m_entries.end()) return;
        m_entries[key(swapchain)] = std::move(entry);
    }

    void destroyed(XrSwapchain swapchain) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.erase(key(swapchain));
    }

    void acquired(XrSwapchain swapchain, uint32_t index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key(swapchain));
        if (it == m_entries.end()) return;
        if (it->second.acquired.size() >= 8) it->second.acquired.pop_front();
        it->second.acquired.push_back(index);
    }

    // Image sur le point d'être rendue au runtime (la plus ancienne acquise) ; false si inconnue.
    bool releasing(XrSwapchain swapchain, uint32_t& index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key(swapchain));
        if (it == m_entries.end() || it->second.acquired.empty()) return false;
        index = it->second.acquired.front();
        return true;
    }

    // Les images sont rendues dans l'ordre où elles ont été acquises (règle OpenXR).
    void released(XrSwapchain swapchain) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key(swapchain));
        if (it == m_entries.end() || it->second.acquired.empty()) return;
        it->second.info.released = it->second.acquired.front();
        it->second.acquired.pop_front();
    }

    bool lookup(XrSwapchain swapchain, Info& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key(swapchain));
        if (it == m_entries.end()) return false;
        out = it->second.info;
        return true;
    }

    void setImages(XrSwapchain swapchain, std::vector<void*> images) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key(swapchain));
        if (it == m_entries.end()) return;
        it->second.info.images = std::move(images);
        it->second.info.imagesRead = true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }

  private:
    struct Entry {
        Info info;
        std::deque<uint32_t> acquired;
    };
    static uint64_t key(XrSwapchain swapchain) { return reinterpret_cast<uint64_t>(swapchain); }

    std::mutex m_mutex;
    std::unordered_map<uint64_t, Entry> m_entries;
};

}  // namespace xrpo
