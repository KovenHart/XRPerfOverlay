// Filtres d'image dans le casque (option, désactivée par défaut) : netteté, débandage, couleurs, mode
// daltonien, appliqués à l'image de chaque œil juste avant qu'elle parte vers le runtime
// (xrReleaseSwapchainImage), sur la carte graphique, dans la file de travail du jeu.
//
// Contrairement au reste de la layer, qui ne fait qu'ajouter des calques, ceci modifie l'image du jeu :
// une copie de la zone de l'œil, puis un rendu plein cadre dans cette zone (shader image_filter_*.hlsli).
// Direct3D 11 : l'état du contexte du jeu est mis de côté le temps du rendu (SwapDeviceContextState) et
// rendu tel quel. Direct3D 12 : une liste de commandes à nous, exécutée sur la file du jeu.
// Toute erreur désactive les filtres pour la session ; le jeu n'est pas touché.
#pragma once

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>

#include <cstdint>
#include <memory>

#include "image_params.h"

namespace xrpo {

// Zone d'une texture du jeu à filtrer.
struct ImageFilterTarget {
    void* texture = nullptr;  // ID3D11Texture2D* ou ID3D12Resource*
    uint32_t slice = 0;       // tranche (tableau de textures)
    uint32_t x = 0, y = 0, width = 0, height = 0;
};

class ImageFilter {
  public:
    static std::unique_ptr<ImageFilter> CreateD3D11(ID3D11Device* device);
    static std::unique_ptr<ImageFilter> CreateD3D12(ID3D12Device* device, ID3D12CommandQueue* queue);

    ImageFilter(const ImageFilter&) = delete;
    ImageFilter& operator=(const ImageFilter&) = delete;
    virtual ~ImageFilter() = default;

    // Filtre les zones (même texture) avec les réglages `params` (origine et taille remplies ici).
    // `format` : format DXGI choisi par le jeu pour la swapchain ; `mipCount` : niveaux par tranche.
    // Retourne false si rien n'a été fait (l'erreur éventuelle est dans le journal).
    virtual bool apply(const ImageFilterTarget* targets, int count, int64_t format, uint32_t mipCount, const ImageFilterParams& params) = 0;
    // Plus de filtres pour la session (erreur).
    bool failed() const { return m_failed; }
    // Format que les filtres savent traiter (8 et 10 bits ; pas les flottants, aux valeurs au-delà de 1).
    static bool Supported(int64_t format);

  protected:
    ImageFilter() = default;
    bool m_failed = false;
};

}  // namespace xrpo
