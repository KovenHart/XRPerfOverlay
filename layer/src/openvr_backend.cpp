#include "openvr_backend.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tlhelp32.h>

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "ascii.h"
#include "layer_presence.h"
#include "vr_matrix.h"
#include "log.h"
// En-tête de Valve : avertissements propres à MinGW / clang (déclarations dllimport) sans objet ici,
// les fonctions de la bibliothèque étant chargées à la main.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#endif
#include "openvr/openvr.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace xrpo {

namespace {

// Fonctions exportées par openvr_api.dll : chargées à la main, pour que le programme démarre et
// attende même si la bibliothèque manque (journal explicite) et qu'aucune bibliothèque d'import ne
// soit nécessaire à la compilation.
typedef uint32_t(VR_CALLTYPE* PFN_VR_InitInternal2)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
typedef void(VR_CALLTYPE* PFN_VR_ShutdownInternal)();
typedef bool(VR_CALLTYPE* PFN_VR_IsInterfaceVersionValid)(const char*);
typedef void*(VR_CALLTYPE* PFN_VR_GetGenericInterface)(const char*, vr::EVRInitError*);
typedef const char*(VR_CALLTYPE* PFN_VR_GetVRInitErrorAsEnglishDescription)(vr::EVRInitError);

vr::HmdMatrix34_t ToMatrix(const Pose& p) {
    vr::HmdMatrix34_t m;
    PoseToMatrix34(p, m.m);
    return m;
}

Pose FromMatrix(const vr::HmdMatrix34_t& m) { return Matrix34ToPose(m.m); }

class OpenVrBackend final : public VrBackend {
  public:
    explicit OpenVrBackend(std::wstring dllFolder) : m_dllFolder(std::move(dllFolder)) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        m_frequency = static_cast<double>(f.QuadPart);
    }

    ~OpenVrBackend() override {
        if (m_system) disconnect();
        releaseGpu();
        if (m_dll) FreeLibrary(m_dll);
    }

    double now() override {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return static_cast<double>(c.QuadPart) / m_frequency;
    }

    // SteamVR est lancé si son serveur tourne. On ne se connecte jamais sans lui : une application
    // OpenVR qui se connecte démarrerait SteamVR.
    bool runtimeRunning() override {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        bool found = false;
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
            found = _wcsicmp(entry.szExeFile, L"vrserver.exe") == 0;
        CloseHandle(snapshot);
        return found;
    }

    double runtimeAge() override {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return -1.0;
        FILETIME nowTime;
        GetSystemTimeAsFileTime(&nowTime);
        const auto ticks = [](const FILETIME& t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        double youngest = -1.0;
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
            if (_wcsicmp(entry.szExeFile, L"vrserver.exe") != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            FILETIME created, exited, kernel, user;
            if (GetProcessTimes(process, &created, &exited, &kernel, &user) && ticks(nowTime) >= ticks(created)) {
                const double age = static_cast<double>(ticks(nowTime) - ticks(created)) / 1e7;  // unités de 100 ns
                if (youngest < 0.0 || age < youngest) youngest = age;
            }
            CloseHandle(process);
        }
        CloseHandle(snapshot);
        return youngest;
    }

    std::string processName(uint32_t pid) override {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return std::string();
        std::vector<wchar_t> path(32768);
        DWORD size = static_cast<DWORD>(path.size());
        std::wstring name;
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
            name.assign(path.data(), size);
            const size_t slash = name.find_last_of(L"\\/");
            if (slash != std::wstring::npos) name.erase(0, slash + 1);
        }
        CloseHandle(process);
        return AsciiText(name.c_str(), name.size(), 120);
    }

    bool layerMeasures(uint32_t pid) override { return LayerMeasuresIn(pid); }
    bool layerDisplays(uint32_t pid) override { return LayerDisplaysIn(pid); }
    bool layerRecord(uint32_t pid, int& state, float& seconds) override { return LayerRecordIn(pid, state, seconds); }

    bool connect() override {
        if (!loadLibrary()) return false;
        // Première connexion « en arrière-plan » : celle-ci ne démarre jamais SteamVR et échoue s'il
        // n'est pas (ou plus) en service, par exemple quand son serveur est encore là mais se ferme.
        vr::EVRInitError error = vr::VRInitError_None;
        m_initInternal(&error, vr::VRApplication_Background, nullptr);
        if (error == vr::VRInitError_None) {
            m_shutdownInternal();
            // Puis la vraie connexion, en tant qu'application « overlay » (calques).
            m_initInternal(&error, vr::VRApplication_Overlay, nullptr);
        }
        if (error != vr::VRInitError_None) {
            // Une seule ligne au journal par cause : l'essai est refait toutes les 30 s.
            if (error != m_lastInitError && error != vr::VRInitError_Init_NoServerForBackgroundApp)
                Log("XRPerfVR : connexion a SteamVR refusee (%d : %s).", static_cast<int>(error), m_errorText ? m_errorText(error) : "?");
            m_lastInitError = error;
            return false;
        }
        m_lastInitError = vr::VRInitError_None;
        m_system = static_cast<vr::IVRSystem*>(interfaceOf(vr::IVRSystem_Version));
        m_compositor = static_cast<vr::IVRCompositor*>(interfaceOf(vr::IVRCompositor_Version));
        m_overlay = static_cast<vr::IVROverlay*>(interfaceOf(vr::IVROverlay_Version));
        m_applications = static_cast<vr::IVRApplications*>(interfaceOf(vr::IVRApplications_Version));
        // Facultatives : position de la poignée sur le modèle de la manette.
        m_renderModels = static_cast<vr::IVRRenderModels*>(interfaceOf(vr::IVRRenderModels_Version));
        m_input = static_cast<vr::IVRInput*>(interfaceOf(vr::IVRInput_Version));
        if (!m_system || !m_compositor || !m_overlay || !m_applications) {
            Log("XRPerfVR : version de SteamVR trop ancienne (interfaces %s introuvables).",
                !m_system ? vr::IVRSystem_Version : !m_compositor ? vr::IVRCompositor_Version : !m_overlay ? vr::IVROverlay_Version : vr::IVRApplications_Version);
            disconnect();
            return false;
        }
        for (Grip& grip : m_grips) grip = Grip();
        for (CachedIndex& cached : m_handIndex) cached = CachedIndex();
        m_posesTime = -1.0;
        return true;
    }

    void disconnect() override {
        releaseGpu();
        if (m_shutdownInternal && (m_system || m_compositor || m_overlay)) m_shutdownInternal();
        m_system = nullptr;
        m_compositor = nullptr;
        m_overlay = nullptr;
        m_applications = nullptr;
        m_renderModels = nullptr;
        m_input = nullptr;
        for (vr::VROverlayHandle_t& handle : m_handles) handle = vr::k_ulOverlayHandleInvalid;
    }

    bool quitRequested() override {
        vr::VREvent_t event;
        while (m_system && m_system->PollNextEvent(&event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) {
                m_system->AcknowledgeQuit_Exiting();
                return true;
            }
        }
        return false;
    }

    VrSystemInfo systemInfo() override {
        VrSystemInfo info;
        char text[256] = "";
        vr::ETrackedPropertyError error = vr::TrackedProp_Success;
        m_system->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String, text, sizeof(text), &error);
        if (error == vr::TrackedProp_Success) info.headset = AsciiText(text, sizeof(text));
        const char* version = m_system->GetRuntimeVersion();
        info.runtime = std::string("SteamVR ") + (version ? AsciiText(version, std::strlen(version) + 1, 40) : std::string("?"));
        const float hz = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error);
        if (error == vr::TrackedProp_Success && hz > 0.f && hz < 1000.f) info.refreshHz = hz;
        uint32_t width = 0, height = 0;
        m_system->GetRecommendedRenderTargetSize(&width, &height);
        if (width > 0 && height > 0 && width < 100000 && height < 100000) {
            info.recommendedWidth = static_cast<int>(width);
            info.recommendedHeight = static_cast<int>(height);
        }
        return info;
    }

    uint32_t sceneProcess() override { return m_applications ? m_applications->GetCurrentSceneProcessId() : 0; }

    int frameTimings(VrFrame* out, int capacity) override {
        if (!m_compositor || capacity <= 0) return 0;
        vr::Compositor_FrameTiming timings[32];
        const uint32_t wanted = static_cast<uint32_t>(capacity < 32 ? capacity : 32);
        std::memset(timings, 0, sizeof(timings));
        timings[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
        uint32_t count = m_compositor->GetFrameTimings(timings, wanted);
        if (count > wanted) count = wanted;
        for (uint32_t i = 0; i < count; ++i) {
            const vr::Compositor_FrameTiming& t = timings[i];
            VrFrame& f = out[i];
            f = VrFrame();
            f.index = t.m_nFrameIndex;
            f.vsyncSeconds = t.m_flSystemTimeInSeconds;
            f.presents = t.m_nNumFramePresents;
            // Temps CPU : des poses reçues (WaitGetPoses) au second Submit, c'est-à-dire le travail du
            // fil de rendu du jeu pour l'image. Temps GPU : rendu du jeu avant et après Submit.
            const float cpu = t.m_flNewFrameReadyMs - t.m_flNewPosesReadyMs;
            if (t.m_flNewFrameReadyMs > 0.f && cpu > 0.f && cpu < 1000.f) f.cpuMs = cpu;
            const float gpu = t.m_flPreSubmitGpuMs + t.m_flPostSubmitGpuMs;
            if (gpu > 0.f && gpu < 1000.f) f.gpuMs = gpu;
        }
        return static_cast<int>(count);
    }

    bool gripInHead(int hand, Pose& out) override {
        const vr::TrackedDeviceIndex_t index = handIndex(hand);
        if (index == vr::k_unTrackedDeviceIndexInvalid || index >= vr::k_unMaxTrackedDeviceCount) return false;
        updatePoses();
        const vr::TrackedDevicePose_t& head = m_poses[vr::k_unTrackedDeviceIndex_Hmd];
        const vr::TrackedDevicePose_t& device = m_poses[index];
        if (!head.bPoseIsValid || !device.bPoseIsValid || !device.bDeviceIsConnected) return false;
        const Pose gripInWorld = Compose(FromMatrix(device.mDeviceToAbsoluteTracking), gripOffset(hand, index));
        out = Compose(Inverse(FromMatrix(head.mDeviceToAbsoluteTracking)), gripInWorld);
        return true;
    }

    bool battery(int hand, float& level, bool& charging) override {
        const vr::TrackedDeviceIndex_t index = handIndex(hand);
        if (index == vr::k_unTrackedDeviceIndexInvalid) return false;
        vr::ETrackedPropertyError error = vr::TrackedProp_Success;
        const bool provides = m_system->GetBoolTrackedDeviceProperty(index, vr::Prop_DeviceProvidesBatteryStatus_Bool, &error);
        if (error == vr::TrackedProp_Success && !provides) return false;
        const float value = m_system->GetFloatTrackedDeviceProperty(index, vr::Prop_DeviceBatteryPercentage_Float, &error);
        if (error != vr::TrackedProp_Success || !(value >= 0.f && value <= 1.f)) return false;
        level = value;
        charging = m_system->GetBoolTrackedDeviceProperty(index, vr::Prop_DeviceIsCharging_Bool, &error) && error == vr::TrackedProp_Success;
        return true;
    }

    bool createOverlays() override {
        const char* keys[kOverlays] = {"xrperfoverlay.panel", "xrperfoverlay.watch", "xrperfoverlay.window"};
        const char* names[kOverlays] = {"XR Perf Overlay", "XR Perf Overlay - montre", "XR Perf Overlay - fenetre"};
        bool ok = true;
        for (int i = 0; i < kOverlays; ++i) {
            vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
            const vr::EVROverlayError error = m_overlay->CreateOverlay(keys[i], names[i], &handle);
            if (error != vr::VROverlayError_None) {
                Log("XRPerfVR : calque \"%s\" refuse par SteamVR (%d).", keys[i], static_cast<int>(error));
                ok = false;
                continue;
            }
            m_handles[i] = handle;
            // Les images du panneau et de la montre sont en alpha prémultiplié, comme dans la layer.
            m_overlay->SetOverlayFlag(handle, vr::VROverlayFlags_IsPremultiplied, true);
            m_overlay->HideOverlay(handle);
        }
        if (!ok) destroyOverlays();
        return ok;
    }

    void destroyOverlays() override {
        for (vr::VROverlayHandle_t& handle : m_handles) {
            if (m_overlay && handle != vr::k_ulOverlayHandleInvalid) m_overlay->DestroyOverlay(handle);
            handle = vr::k_ulOverlayHandleInvalid;
        }
        for (Upload& upload : m_uploads) upload.failures = 0;
        releaseTextures();
    }

    // L'image est envoyée sous forme de texture Direct3D 11, sur la carte graphique qu'utilise SteamVR :
    // c'est la voie prévue pour un calque mis à jour souvent. L'envoi d'une image brute
    // (SetOverlayRaw), asynchrone, fait clignoter le calque à chaque mise à jour et finit par être
    // refusé (erreur 23), l'image restant alors figée : il ne sert plus que de secours, si Direct3D 11
    // est indisponible.
    void setPixels(VrOverlay overlay, const uint8_t* pixels, int width, int height, bool bgra) override {
        const int i = static_cast<int>(overlay);
        if (!valid(i) || !pixels || width <= 0 || height <= 0) return;
        Upload& upload = m_uploads[i];
        vr::EVROverlayError error = vr::VROverlayError_None;
        if (gpuReady()) {
            error = setTexture(upload, m_handles[i], pixels, width, height, bgra);
        } else {
            // SteamVR lit l'image après l'appel : elle est gardée ici jusqu'à la suivante (en RGBA).
            upload.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
            if (bgra)
                for (size_t p = 0; p < upload.pixels.size(); p += 4) std::swap(upload.pixels[p], upload.pixels[p + 2]);
            error = m_overlay->SetOverlayRaw(m_handles[i], upload.pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), 4);
        }
        report(i, error);
    }

    void setWidth(VrOverlay overlay, float meters) override {
        const int i = static_cast<int>(overlay);
        if (valid(i)) m_overlay->SetOverlayWidthInMeters(m_handles[i], meters);
    }

    void placeOnHead(VrOverlay overlay, const Pose& pose) override {
        const int i = static_cast<int>(overlay);
        if (!valid(i)) return;
        const vr::HmdMatrix34_t matrix = ToMatrix(pose);
        m_overlay->SetOverlayTransformTrackedDeviceRelative(m_handles[i], vr::k_unTrackedDeviceIndex_Hmd, &matrix);
    }

    bool placeOnHand(VrOverlay overlay, int hand, const Pose& inGrip) override {
        const int i = static_cast<int>(overlay);
        const vr::TrackedDeviceIndex_t index = handIndex(hand);
        if (!valid(i) || index == vr::k_unTrackedDeviceIndexInvalid || index >= vr::k_unMaxTrackedDeviceCount) return false;
        // La pose de la manette dans OpenVR est son repère de suivi brut : on y ajoute la position de
        // la poignée, pour que les emplacements sur la main soient les mêmes qu'en OpenXR.
        const vr::HmdMatrix34_t matrix = ToMatrix(Compose(gripOffset(hand, index), inGrip));
        return m_overlay->SetOverlayTransformTrackedDeviceRelative(m_handles[i], index, &matrix) == vr::VROverlayError_None;
    }

    void placeInWorld(VrOverlay overlay, const Pose& pose) override {
        const int i = static_cast<int>(overlay);
        if (!valid(i)) return;
        const vr::HmdMatrix34_t matrix = ToMatrix(pose);
        m_overlay->SetOverlayTransformAbsolute(m_handles[i], vr::TrackingUniverseStanding, &matrix);
    }

    bool headInWorld(Pose& out) override {
        if (!m_system) return false;
        updatePoses();
        const vr::TrackedDevicePose_t& head = m_poses[vr::k_unTrackedDeviceIndex_Hmd];
        if (!head.bPoseIsValid || !head.bDeviceIsConnected) return false;
        out = FromMatrix(head.mDeviceToAbsoluteTracking);
        return true;
    }

    uint32_t imageGeneration() override { return m_imageGeneration; }

    void show(VrOverlay overlay, bool visible) override {
        const int i = static_cast<int>(overlay);
        if (!valid(i)) return;
        if (visible)
            m_overlay->ShowOverlay(m_handles[i]);
        else
            m_overlay->HideOverlay(m_handles[i]);
    }

  private:
    struct Grip {
        vr::TrackedDeviceIndex_t index = vr::k_unTrackedDeviceIndexInvalid;
        std::string model;
        Pose offset;  // poignée dans le repère de suivi de la manette
        bool known = false;
        bool found = false;      // composant « handgrip » trouvé dans le modèle
        double checkedAt = -1.0;
    };
    struct CachedIndex {
        vr::TrackedDeviceIndex_t index = vr::k_unTrackedDeviceIndexInvalid;
        double readAt = -1.0;
    };

    static constexpr int kOverlays = static_cast<int>(VrOverlay::Count);
    bool valid(int i) const { return m_overlay && i >= 0 && i < kOverlays && m_handles[i] != vr::k_ulOverlayHandleInvalid; }

    // Envoi de l'image d'un calque : texture de la bonne taille, mise à jour, puis transmise à SteamVR
    // (qui en garde la dernière version affichée tant qu'aucune autre n'arrive).
    struct Upload {
        ID3D11Texture2D* texture = nullptr;
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels;  // secours sans Direct3D : image brute gardée jusqu'à la suivante
        int failures = 0;             // envois refusés d'affilée
        int lastError = 0;
        bool bgra = false;            // format de la texture
    };

    // Appareil Direct3D 11 sur la carte de SteamVR, créé au premier envoi (et recréé après une perte
    // de l'appareil, pilote réinitialisé par exemple). Un échec n'est pas réessayé avant 30 s.
    bool gpuReady() {
        if (m_device && m_device->GetDeviceRemovedReason() != S_OK) {
            Log("XRPerfVR : carte graphique reinitialisee, textures des calques recreees.");
            releaseGpu();
        }
        if (m_device) return true;
        const double t = now();
        if (m_gpuFailedAt >= 0.0 && t - m_gpuFailedAt < 30.0) return false;
        if (createDevice()) {
            m_gpuFailedAt = -1.0;
            return true;
        }
        m_gpuFailedAt = t;
        return false;
    }

    bool createDevice() {
        int32_t adapterIndex = -1;
        if (m_system) m_system->GetDXGIOutputInfo(&adapterIndex);
        IDXGIAdapter1* adapter = nullptr;
        if (adapterIndex >= 0) {
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
                if (FAILED(factory->EnumAdapters1(static_cast<UINT>(adapterIndex), &adapter))) adapter = nullptr;
                factory->Release();
            }
        }
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                       sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &m_device, nullptr, &m_context);
        // Pilote sans le niveau 11.1 : nouvel essai sans lui (E_INVALIDARG sur certains systèmes).
        if (hr == E_INVALIDARG)
            hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1,
                                   sizeof(levels) / sizeof(levels[0]) - 1, D3D11_SDK_VERSION, &m_device, nullptr, &m_context);
        if (adapter) adapter->Release();
        if (FAILED(hr) || !m_device || !m_context) {
            releaseGpu();
            if (!m_gpuErrorLogged) {
                m_gpuErrorLogged = true;
                Log("XRPerfVR : Direct3D 11 indisponible (0x%08lx), image des calques envoyee sans texture.", static_cast<unsigned long>(hr));
            }
            return false;
        }
        return true;
    }

    vr::EVROverlayError setTexture(Upload& upload, vr::VROverlayHandle_t handle, const uint8_t* pixels, int width, int height, bool bgra) {
        if (!upload.texture || upload.width != width || upload.height != height || upload.bgra != bgra) {
            if (upload.texture) {
                // SteamVR ne doit plus lire l'ancienne texture avant qu'elle soit libérée.
                m_overlay->ClearOverlayTexture(handle);
                upload.texture->Release();
                upload.texture = nullptr;
            }
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(width);
            desc.Height = static_cast<UINT>(height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // partagée avec le compositeur de SteamVR
            if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &upload.texture)) || !upload.texture) {
                upload.texture = nullptr;
                upload.width = upload.height = 0;
                return vr::VROverlayError_InvalidTexture;
            }
            upload.width = width;
            upload.height = height;
            upload.bgra = bgra;
        }
        m_context->UpdateSubresource(upload.texture, 0, nullptr, pixels, static_cast<UINT>(width) * 4, 0);
        m_context->Flush();  // copie terminée avant que le compositeur ne lise la texture
        // Image du panneau : couleurs déjà encodées pour l'écran (sRGB), comme dans la layer.
        vr::Texture_t texture = {upload.texture, vr::TextureType_DirectX, vr::ColorSpace_Gamma};
        return m_overlay->SetOverlayTexture(handle, &texture);
    }

    // Journal : premier refus d'une série (et chaque changement de code), puis retour à la normale avec
    // le nombre d'envois refusés. Après 20 refus d'affilée, textures et appareil sont recréés.
    void report(int i, vr::EVROverlayError error) {
        Upload& upload = m_uploads[i];
        const char* name = i == 0 ? "panneau" : (i == 1 ? "montre" : "fenetre");
        if (error == vr::VROverlayError_None) {
            if (upload.failures > 0) Log("XRPerfVR : image du calque %s de nouveau acceptee (%d envoi(s) refuse(s)).", name, upload.failures);
            upload.failures = 0;
            upload.lastError = 0;
            return;
        }
        if (upload.failures == 0 || upload.lastError != static_cast<int>(error))
            Log("XRPerfVR : image du calque %s refusee par SteamVR (%d%s).", name, static_cast<int>(error), m_device ? ", texture" : ", image brute");
        upload.lastError = static_cast<int>(error);
        if (++upload.failures % 20 == 0 && m_device) {
            Log("XRPerfVR : %d refus d'affilee, textures des calques recreees.", upload.failures);
            releaseGpu();
        }
    }

    void releaseTextures() {
        ++m_imageGeneration;
        for (int i = 0; i < kOverlays; ++i) {
            Upload& upload = m_uploads[i];
            if (upload.texture) {
                if (valid(i)) m_overlay->ClearOverlayTexture(m_handles[i]);
                upload.texture->Release();
            }
            upload.texture = nullptr;
            upload.width = upload.height = 0;
            upload.pixels.clear();
            upload.pixels.shrink_to_fit();
        }
    }

    void releaseGpu() {
        releaseTextures();
        if (m_context) {
            m_context->ClearState();
            m_context->Flush();
            m_context->Release();
        }
        if (m_device) m_device->Release();
        m_context = nullptr;
        m_device = nullptr;
    }

    void* interfaceOf(const char* version) {
        if (!m_versionValid(version)) return nullptr;
        vr::EVRInitError error = vr::VRInitError_None;
        void* pointer = m_getInterface(version, &error);
        return error == vr::VRInitError_None ? pointer : nullptr;
    }

    bool loadLibrary() {
        if (m_dll) return true;
        if (m_dllFolder.empty()) return false;  // dossier du programme inconnu : jamais de recherche par le chemin de Windows
        std::wstring path = m_dllFolder;
        if (!path.empty() && path.back() != L'\\') path += L'\\';
        path += L"openvr_api.dll";
        m_dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m_dll) {
            Log("XRPerfVR : openvr_api.dll introuvable a cote du programme (erreur %lu).", static_cast<unsigned long>(GetLastError()));
            return false;
        }
        m_initInternal = reinterpret_cast<PFN_VR_InitInternal2>(reinterpret_cast<void*>(GetProcAddress(m_dll, "VR_InitInternal2")));
        m_shutdownInternal = reinterpret_cast<PFN_VR_ShutdownInternal>(reinterpret_cast<void*>(GetProcAddress(m_dll, "VR_ShutdownInternal")));
        m_versionValid = reinterpret_cast<PFN_VR_IsInterfaceVersionValid>(reinterpret_cast<void*>(GetProcAddress(m_dll, "VR_IsInterfaceVersionValid")));
        m_getInterface = reinterpret_cast<PFN_VR_GetGenericInterface>(reinterpret_cast<void*>(GetProcAddress(m_dll, "VR_GetGenericInterface")));
        m_errorText = reinterpret_cast<PFN_VR_GetVRInitErrorAsEnglishDescription>(
            reinterpret_cast<void*>(GetProcAddress(m_dll, "VR_GetVRInitErrorAsEnglishDescription")));
        if (!m_initInternal || !m_shutdownInternal || !m_versionValid || !m_getInterface) {
            Log("XRPerfVR : openvr_api.dll incomplete.");
            FreeLibrary(m_dll);
            m_dll = nullptr;
            return false;
        }
        return true;
    }

    // Numéro de la manette d'une main, relu au plus deux fois par seconde (manette rallumée, changée...).
    vr::TrackedDeviceIndex_t handIndex(int hand) {
        if (!m_system || hand < 0 || hand > 1) return vr::k_unTrackedDeviceIndexInvalid;
        CachedIndex& cached = m_handIndex[hand];
        const double t = now();
        if (cached.readAt < 0.0 || t - cached.readAt >= 0.5) {
            cached.index =
                m_system->GetTrackedDeviceIndexForControllerRole(hand == 0 ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);
            cached.readAt = t;
        }
        return cached.index;
    }

    // Poses de tous les appareils, prédites pour l'affichage de la prochaine image (une fois par passage).
    void updatePoses() {
        const double t = now();
        if (m_posesTime >= 0.0 && t - m_posesTime < 0.005) return;
        m_posesTime = t;
        float sinceVsync = 0.f;
        uint64_t frameCounter = 0;
        m_system->GetTimeSinceLastVsync(&sinceVsync, &frameCounter);
        vr::ETrackedPropertyError error = vr::TrackedProp_Success;
        float hz = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error);
        if (error != vr::TrackedProp_Success || !(hz > 0.f)) hz = 90.f;
        float toPhotons = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float, &error);
        if (error != vr::TrackedProp_Success || !(toPhotons >= 0.f && toPhotons < 0.1f)) toPhotons = 0.f;
        float ahead = 1.f / hz - sinceVsync + toPhotons;
        if (!(ahead >= 0.f && ahead < 0.1f)) ahead = 0.f;
        m_system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, ahead, m_poses, vr::k_unMaxTrackedDeviceCount);
    }

    // Position de la poignée (composant « handgrip » du modèle de la manette) dans le repère de suivi
    // de la manette. Relue quand la manette ou son modèle change.
    const Pose& gripOffset(int hand, vr::TrackedDeviceIndex_t index) {
        Grip& grip = m_grips[hand];
        // Modèle relu toutes les 2 s au plus ; un modèle sans poignée est réessayé toutes les 10 s (les
        // modèles peuvent se charger après la connexion).
        const double t = now();
        if (grip.known && grip.index == index && t - grip.checkedAt < (grip.found ? 2.0 : 10.0)) return grip.offset;
        char model[256] = "";
        vr::ETrackedPropertyError error = vr::TrackedProp_Success;
        m_system->GetStringTrackedDeviceProperty(index, vr::Prop_RenderModelName_String, model, sizeof(model), &error);
        if (error != vr::TrackedProp_Success) model[0] = '\0';
        if (grip.known && grip.index == index && grip.model == model && grip.found) {
            grip.checkedAt = t;
            return grip.offset;
        }
        const bool wasKnown = grip.known && grip.index == index && grip.model == model;
        const bool wasFound = grip.found;
        grip = Grip();
        grip.index = index;
        grip.model = model;
        grip.known = true;
        grip.checkedAt = t;
        bool found = false;
        if (m_renderModels && model[0]) {
            vr::RenderModel_ControllerMode_State_t mode = {};
            vr::RenderModel_ComponentState_t state = {};
            vr::VRInputValueHandle_t source = vr::k_ulInvalidInputValueHandle;
            if (m_input && m_input->GetInputSourceHandle(hand == 0 ? "/user/hand/left" : "/user/hand/right", &source) == vr::VRInputError_None &&
                m_renderModels->GetComponentStateForDevicePath(model, vr::k_pch_Controller_Component_HandGrip, source, &mode, &state)) {
                found = true;
            } else if (m_renderModels->RenderModelHasComponent(model, vr::k_pch_Controller_Component_HandGrip)) {
                vr::VRControllerState_t controller = {};
                found = m_renderModels->GetComponentState(model, vr::k_pch_Controller_Component_HandGrip, &controller, &mode, &state);
            }
            if (found) grip.offset = FromMatrix(state.mTrackingToComponentLocal);
        }
        grip.found = found;
        if (wasKnown && wasFound == found) return grip.offset;  // même résultat qu'avant : rien de nouveau au journal
        Log("XRPerfVR : manette %s (modele \"%s\") : %s.", hand == 0 ? "gauche" : "droite", AsciiText(model, sizeof(model)).c_str(),
            found ? "poignee trouvee" : "pas de poignee dans le modele, repere de suivi de la manette utilise");
        return grip.offset;
    }

    const std::wstring m_dllFolder;
    double m_frequency = 1.0;
    HMODULE m_dll = nullptr;
    PFN_VR_InitInternal2 m_initInternal = nullptr;
    PFN_VR_ShutdownInternal m_shutdownInternal = nullptr;
    PFN_VR_IsInterfaceVersionValid m_versionValid = nullptr;
    PFN_VR_GetGenericInterface m_getInterface = nullptr;
    PFN_VR_GetVRInitErrorAsEnglishDescription m_errorText = nullptr;

    vr::IVRSystem* m_system = nullptr;
    vr::IVRCompositor* m_compositor = nullptr;
    vr::IVROverlay* m_overlay = nullptr;
    vr::IVRApplications* m_applications = nullptr;
    vr::IVRRenderModels* m_renderModels = nullptr;
    vr::IVRInput* m_input = nullptr;

    vr::VROverlayHandle_t m_handles[kOverlays] = {vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid};
    Upload m_uploads[kOverlays];
    uint32_t m_imageGeneration = 0;  // augmente à chaque perte des textures des calques
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    double m_gpuFailedAt = -1.0;
    bool m_gpuErrorLogged = false;
    vr::TrackedDevicePose_t m_poses[vr::k_unMaxTrackedDeviceCount] = {};
    double m_posesTime = -1.0;
    Grip m_grips[2];
    CachedIndex m_handIndex[2];
    vr::EVRInitError m_lastInitError = vr::VRInitError_None;
};

}  // namespace

std::unique_ptr<VrBackend> CreateOpenVrBackend(const std::wstring& dllFolder) { return std::unique_ptr<VrBackend>(new OpenVrBackend(dllFolder)); }

}  // namespace xrpo
