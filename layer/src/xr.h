// Inclusion unique d'OpenXR avec les API graphiques prises en charge, et table des fonctions
// du maillon suivant de la chaîne (autre layer ou runtime).
#pragma once

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

namespace xrpo {

struct NextFunctions {
    PFN_xrGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance DestroyInstance = nullptr;
    PFN_xrCreateSession CreateSession = nullptr;
    PFN_xrDestroySession DestroySession = nullptr;
    PFN_xrWaitFrame WaitFrame = nullptr;
    PFN_xrBeginFrame BeginFrame = nullptr;
    PFN_xrEndFrame EndFrame = nullptr;
    // Utilisées uniquement pour notre propre calque.
    PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
    PFN_xrDestroySpace DestroySpace = nullptr;
    PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain CreateSwapchain = nullptr;
    PFN_xrDestroySwapchain DestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage AcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage WaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
    // Facultatives : nom du casque et du runtime. Absentes, ces informations ne sont pas affichées.
    PFN_xrGetSystemProperties GetSystemProperties = nullptr;
    PFN_xrGetInstanceProperties GetInstanceProperties = nullptr;
    // Facultative : résolution recommandée par le runtime (comparée à celle que le jeu rend vraiment).
    PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews = nullptr;
    // Accroche aux manettes (facultatives : sans elles, les calques restent devant les yeux).
    PFN_xrLocateSpace LocateSpace = nullptr;
    PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings = nullptr;
    PFN_xrAttachSessionActionSets AttachSessionActionSets = nullptr;
    PFN_xrSyncActions SyncActions = nullptr;

    bool overlayUsable() const {
        return CreateReferenceSpace && DestroySpace && EnumerateSwapchainFormats && CreateSwapchain && DestroySwapchain &&
               EnumerateSwapchainImages && AcquireSwapchainImage && WaitSwapchainImage && ReleaseSwapchainImage;
    }
};

// Pointeur COM minimal (évite de dépendre de WRL / ATL).
template <typename T>
class Com {
  public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }
    // Prend une référence supplémentaire sur un objet qui ne nous appartient pas.
    void share(T* p) {
        if (p) p->AddRef();
        reset();
        m_p = p;
    }
    T* get() const { return m_p; }
    T* operator->() const { return m_p; }
    T** put() {
        reset();
        return &m_p;
    }
    explicit operator bool() const { return m_p != nullptr; }

  private:
    T* m_p = nullptr;
};

}  // namespace xrpo
