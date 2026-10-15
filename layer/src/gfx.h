// Copie du panneau (tampon mémoire) vers les images de notre swapchain OpenXR.
// C'est la seule partie de la layer qui touche à Direct3D, et elle n'y fait qu'une copie de
// texture : aucun état de rendu du jeu n'est lu ni modifié.
//
// Option (désactivée par défaut) : mesure du temps GPU de chaque image. Deux horodatages GPU sont
// alors ajoutés à la file de travail du jeu, l'un à xrBeginFrame et l'autre à xrEndFrame ; c'est
// la seule fois où la layer insère quelque chose dans le travail GPU du jeu.
#pragma once

#include <cstdint>

#include "xr.h"

namespace xrpo {

// Une mesure terminée : temps GPU d'une image et instant (horloge de la layer, en secondes) où
// le jeu a terminé cette image côté CPU.
struct GpuTiming {
    double endTime;
    float gpuMs;
};

enum class UploadResult {
    Ok,
    Busy,   // la copie précédente n'est pas terminée : réessayer à l'image suivante
    Error,
};

class Gfx {
  public:
    // Swapchains de la layer : 0 pour le panneau et la montre, 1 pour la fenêtre capturée.
    static constexpr int kSwapchains = 2;

    virtual ~Gfx() = default;
    virtual const char* name() const = 0;
    // Récupère les textures de la swapchain `slot` créée par le runtime.
    virtual bool enumerateImages(const NextFunctions& next, XrSwapchain swapchain, int slot) = 0;
    // `pixels` : width x height pixels de 4 octets (lignes contiguës), déjà dans l'ordre d'octets de la
    // texture ; copiés dans le coin supérieur gauche de la texture, qui peut être plus grande.
    virtual UploadResult upload(int slot, uint32_t imageIndex, const uint8_t* pixels, int width, int height) = 0;
    // true tant que la copie précédente vers `slot` n'a pas été exécutée par le GPU (jamais en D3D11).
    virtual bool busy(int slot) {
        (void)slot;
        return false;
    }
    // Attend la fin des copies en cours avant libération (sans effet en D3D11).
    virtual void waitIdle() {}

    // --- Mesure du temps GPU (option) -------------------------------------------------------------
    // Prépare la mesure. Retourne false si elle n'est pas possible (API, file de travail, matériel).
    virtual bool enableTiming() { return false; }
    // Repère de début d'image, à xrBeginFrame.
    virtual void timingBegin() {}
    // Repère de fin d'image, au début de xrEndFrame (avant la copie du panneau). `now` : instant côté CPU.
    virtual void timingEnd(double now) { (void)now; }
    // Mesures terminées par le GPU depuis le dernier appel, de la plus ancienne à la plus récente ;
    // jamais bloquant. Retourne leur nombre.
    virtual int timingCollect(GpuTiming* out, int capacity) {
        (void)out;
        (void)capacity;
        return 0;
    }
    // Arrête la mesure pour le reste de la session (plus aucun repère), depuis xrEndFrame, après
    // timingEnd. Les mesures encore en cours sont abandonnées.
    virtual void stopTiming() {}

    // Identifiant de la carte graphique du jeu (pour lire son nom, voir hardware_info.h).
    virtual bool adapterLuid(LUID& luid) {
        (void)luid;
        return false;
    }
};

Gfx* CreateGfxD3D11(ID3D11Device* device);
Gfx* CreateGfxD3D12(ID3D12Device* device, ID3D12CommandQueue* queue);

}  // namespace xrpo
