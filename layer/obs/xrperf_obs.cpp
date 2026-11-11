// xrperf-obs.dll : plugin OBS Studio (30 et plus, Windows 64 bits) qui ajoute la source « XR Perf (casque) ».
//
// L'image est celle de la vidéo du casque de XR Perf Overlay (œil cadré, stabilisé, filtres, panneau),
// préparée par l'enregistreur de XRPerfSensors (layer/src/video_record.cpp) dans une texture Direct3D 11
// partagée : rien n'est encodé ni ne passe par le processeur. Bloc de mémoire partagée Local\XRPerfOverlay.Obs
// (ObsShare, layer/src/shared_block.h) :
//   - la source y note sa présence tant qu'elle est affichée : XRPerfSensors fait alors tourner l'enregistreur
//     sans fichier (ou en plus d'un enregistrement) ;
//   - l'enregistreur y décrit la texture (poignée partagée, taille). Verrou « keyed mutex » : il prend la clé
//     0 et rend la clé 1 ; la source prend 1 (une image neuve), la recopie dans une texture à elle et rend 0.
//
// Réglages : ceux de la vidéo, dans XRPerfSensors (page Vidéo). Sans jeu VR, la source garde sa taille et
// n'affiche rien (transparente).

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <sddl.h>

#include <obs-module.h>

#include <cstdint>
#include <cstring>

#include "shared_block.h"
#include "version_info.h"

OBS_DECLARE_MODULE()

namespace {

using xrpo::ObsShare;

// Même descripteur que les autres blocs de XRPerfSensors : lecture et écriture pour les comptes ouverts.
const wchar_t kSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)";
// Image plus vieille : le jeu ou l'enregistreur est arrêté.
constexpr unsigned long long kStaleMs = 1500;

bool French() {
    const char* locale = obs_get_locale();
    return locale && std::strncmp(locale, "fr", 2) == 0;
}

struct XrSource {
    obs_source_t* source = nullptr;
    HANDLE mapping = nullptr;
    volatile ObsShare* share = nullptr;
    gs_texture_t* shared = nullptr;  // texture de l'enregistreur
    gs_texture_t* copy = nullptr;    // la nôtre : dernière image reçue
    uint32_t handle = 0, generation = 0;
    uint32_t width = 0, height = 0;  // taille annoncée à OBS (gardée quand le jeu s'arrête)
    bool failedOpen = false;         // poignée refusée : pas de nouvel essai avant une autre texture
    bool adapterChecked = false;
};

void OpenShare(XrSource* s) {
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), nullptr, FALSE};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kSddl, SDDL_REVISION_1, &descriptor, nullptr)) attributes.lpSecurityDescriptor = descriptor;
    s->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, descriptor ? &attributes : nullptr, PAGE_READWRITE, 0, sizeof(ObsShare), xrpo::kObsShareName);
    if (!s->mapping) s->mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, xrpo::kObsShareName);
    if (descriptor) LocalFree(descriptor);
    if (!s->mapping) {
        blog(LOG_WARNING, "[xrperf-obs] Bloc partage indisponible (%lu).", GetLastError());
        return;
    }
    s->share = static_cast<volatile ObsShare*>(MapViewOfFile(s->mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(ObsShare)));
    if (!s->share) return;
    volatile ObsShare* o = s->share;
    if (o->magic == 0) {  // créé ici (mémoire à zéro)
        o->version = xrpo::kObsShareVersion;
        MemoryBarrier();
        o->magic = xrpo::kObsShareMagic;
    }
    o->consumerPid = GetCurrentProcessId();
    o->consumerVersion = (XRPO_VERSION_MAJOR << 16) | (XRPO_VERSION_MINOR << 8) | XRPO_VERSION_PATCH;
}

void ReleaseTextures(XrSource* s) {
    if (!s->shared && !s->copy) return;
    obs_enter_graphics();
    if (s->shared) gs_texture_destroy(s->shared);
    if (s->copy) gs_texture_destroy(s->copy);
    obs_leave_graphics();
    s->shared = s->copy = nullptr;
    s->handle = 0;
}

const char* GetName(void*) { return French() ? "XR Perf (casque)" : "XR Perf (headset)"; }

void* Create(obs_data_t*, obs_source_t* source) {
    auto* s = new XrSource();
    s->source = source;
    OpenShare(s);
    return s;
}

void Destroy(void* data) {
    auto* s = static_cast<XrSource*>(data);
    ReleaseTextures(s);
    if (s->share) UnmapViewOfFile(const_cast<ObsShare*>(s->share));
    if (s->mapping) CloseHandle(s->mapping);
    delete s;
}

uint32_t GetWidth(void* data) { return static_cast<XrSource*>(data)->width; }
uint32_t GetHeight(void* data) { return static_cast<XrSource*>(data)->height; }

obs_properties_t* GetProperties(void*) {
    obs_properties_t* props = obs_properties_create();
    obs_property_t* info = obs_properties_add_text(
        props, "info",
        French() ? "Image de la vidéo du casque de XR Perf Overlay. Les réglages (œil, format, résolution, stabilisation, panneau, filtres) sont ceux "
                   "de la page Vidéo de XRPerfSensors, qui doit être ouvert. Le son se prend dans OBS (capture audio de l'application ou du bureau)."
                 : "Headset video image from XR Perf Overlay. Settings (eye, format, resolution, stabilization, panel, filters) are those of the Video "
                   "page of XRPerfSensors, which must be running. Sound is captured in OBS (application or desktop audio capture).",
        OBS_TEXT_INFO);
    (void)info;
    return props;
}

// Hors du fil graphique : présence de la source (tant qu'elle est affichée quelque part).
void VideoTick(void* data, float) {
    auto* s = static_cast<XrSource*>(data);
    if (s->share && obs_source_showing(s->source)) s->share->consumerTickMs = GetTickCount64();
}

// Même carte graphique qu'OBS ? (sinon, la texture partagée ne peut pas s'ouvrir) : dit une fois dans le journal.
void CheckAdapter(XrSource* s, uint64_t luid) {
    if (s->adapterChecked) return;
    s->adapterChecked = true;
    auto* device = static_cast<ID3D11Device*>(gs_get_device_obj());
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC desc = {};
    if (device && SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetDesc(&desc))) {
        const uint64_t mine = static_cast<uint64_t>(desc.AdapterLuid.LowPart) | (static_cast<uint64_t>(static_cast<uint32_t>(desc.AdapterLuid.HighPart)) << 32);
        if (mine != luid)
            blog(LOG_WARNING, "[xrperf-obs] OBS et le jeu VR utilisent deux cartes graphiques differentes : l'image ne peut pas etre partagee. "
                              "Reglez OBS sur la carte du casque (Parametres > Avance, ou parametres graphiques de Windows).");
    }
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();
}

void VideoRender(void* data, gs_effect_t*) {
    auto* s = static_cast<XrSource*>(data);
    volatile ObsShare* o = s->share;
    if (!o || o->magic != xrpo::kObsShareMagic || o->version != xrpo::kObsShareVersion) return;
    const unsigned long long now = GetTickCount64();
    const unsigned long long produced = o->producerTickMs;
    const uint32_t handle = o->handle, generation = o->generation;
    const bool live = handle != 0 && produced != 0 && now - produced < kStaleMs;
    if (live && (handle != s->handle || generation != s->generation)) {
        // Nouvelle texture (taille changée, nouvel enregistreur) : ouverte, et une copie à nous.
        if (s->shared) gs_texture_destroy(s->shared);
        if (s->copy) gs_texture_destroy(s->copy);
        s->shared = s->copy = nullptr;
        s->handle = handle;
        s->generation = generation;
        s->failedOpen = false;
        CheckAdapter(s, o->adapterLuid);
        s->shared = gs_texture_open_shared(handle);
        if (s->shared) {
            s->copy = gs_texture_create(gs_texture_get_width(s->shared), gs_texture_get_height(s->shared), gs_texture_get_color_format(s->shared), 1,
                                        nullptr, 0);
            s->width = gs_texture_get_width(s->shared);
            s->height = gs_texture_get_height(s->shared);
            blog(LOG_INFO, "[xrperf-obs] Image de XR Perf : %ux%u.", s->width, s->height);
        } else {
            s->failedOpen = true;
            blog(LOG_WARNING, "[xrperf-obs] Texture partagee impossible a ouvrir.");
        }
    }
    if (live && s->shared && s->copy && gs_texture_acquire_sync(s->shared, 1, 0) == 0) {
        gs_copy_texture(s->copy, s->shared);
        gs_texture_release_sync(s->shared, 0);
    }
    if (!live || !s->copy) return;  // jeu ou XRPerfSensors arrêté : rien (la taille reste)
    obs_source_draw(s->copy, 0, 0, 0, 0, false);
}

}  // namespace

bool obs_module_load(void) {
    obs_source_info info = {};
    info.id = "xrperf_headset";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
    info.get_name = GetName;
    info.create = Create;
    info.destroy = Destroy;
    info.get_width = GetWidth;
    info.get_height = GetHeight;
    info.get_properties = GetProperties;
    info.video_tick = VideoTick;
    info.video_render = VideoRender;
    info.icon_type = OBS_ICON_TYPE_GAME_CAPTURE;
    obs_register_source(&info);
    blog(LOG_INFO, "[xrperf-obs] XR Perf Overlay %s : source \"XR Perf\" disponible.", XRPO_VERSION_TEXT);
    return true;
}

MODULE_EXPORT const char* obs_module_name(void) { return "XR Perf Overlay"; }

MODULE_EXPORT const char* obs_module_description(void) {
    return "Headset video image from XR Perf Overlay (OpenXR and SteamVR games) / Image de la video du casque de XR Perf Overlay.";
}
