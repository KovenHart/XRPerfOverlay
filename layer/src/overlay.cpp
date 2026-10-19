#include "overlay.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "ascii.h"
#include "hardware_info.h"
#include "summary_file.h"
#include "log.h"
#include "settings_apply.h"
#include "version.h"
#include "view_pose.h"

namespace xrpo {

namespace {

// Mesure du temps GPU jugée non représentative : plus basse que ce rapport x l'estimation, carte
// chargée au moins à kImplausibleLoad %, pendant kImplausibleSeconds d'affilée. Cas typique : le jeu
// envoie son rendu hors des deux repères de la layer (autre file de commandes, envoi différé par un
// fil d'exécution à part, session OpenXR ouverte par un outil comme UEVR qui ne fait que copier
// l'image entre xrBeginFrame et xrEndFrame).
const float kImplausibleRatio = 0.3f;
const float kImplausibleLoad = 50.f;
const double kImplausibleSeconds = 10.0;

const int kMaxUploadErrors = 5;
const int kMaxRejections = 3;
const int kMaxWaitTimeouts = 240;
// XRPerfSensors enregistre le .ini une fraction de seconde après un changement : un changement plus
// récent que cette marge avant notre lecture du .ini peut ne pas s'y trouver.
const unsigned long long kSettingsGraceMs = 2000;

double nowSeconds() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

// Table sRGB -> linéaire, pour les runtimes qui ne proposent pas de format de texture sRGB.
const uint8_t* srgbToLinearTable() {
    static uint8_t table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            const double lin = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            table[i] = static_cast<uint8_t>(lin * 255.0 + 0.5);
        }
        ready = true;
    }
    return table;
}

}  // namespace

Overlay::Overlay(const NextFunctions& next, const Config& config, XrInstance instance, std::shared_ptr<HandInput> hands)
    : m_next(next), m_instance(instance), m_config(config), m_hands(std::move(hands)) {
    m_stats.setWindow(config.statsWindowS);
    m_texture.assign(static_cast<size_t>(kTextureWidth) * kTextureHeight * 4, 0);
    m_converted.resize(m_texture.size());
}

Overlay::~Overlay() {
    // Pas de verrou ici : l'objet n'est détruit que lorsque plus aucun appel ne le référence.
    // Si la session existe encore, ses objets OpenXR seront détruits avec elle par le runtime.
    finishSummary();
    if (m_gfx) m_gfx->waitIdle();
}

void Overlay::onSessionCreated(XrSession session, const XrSessionCreateInfo* createInfo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    destroyResources();
    m_session = session;
    m_failed = false;
    m_resourcesTried = false;
    m_uploadErrors = 0;
    m_rejections = 0;
    // Fenêtre capturée : la dernière image sera relue pour la nouvelle swapchain.
    m_windowShare.resetFrame();
    m_windowPending = false;
    m_windowFailed = false;
    m_windowErrors = 0;
    m_windowPlaceRequested = false;
    m_windowWantedAt = -1.0;
    m_stats.reset();
    m_history.reset();
    m_settingsSeen = false;
    m_hasLastBlock = false;
    m_configTickMs = GetTickCount64();  // m_config vient d'être lu dans le .ini par l'appelant
    {
        std::lock_guard<std::mutex> timing(m_timingMutex);
        m_timer.reset();
    }

    // Recherche de l'API graphique du jeu dans la chaîne `next` de la création de session.
    m_gfx.reset();
    for (auto* entry = static_cast<const XrBaseInStructure*>(createInfo ? createInfo->next : nullptr); entry; entry = entry->next) {
        if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            m_gfx.reset(CreateGfxD3D11(reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry)->device));
            break;
        }
        if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            const auto* binding = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(entry);
            m_gfx.reset(CreateGfxD3D12(binding->device, binding->queue));
            break;
        }
    }
    if (m_gfx) {
        Log("Session creee (%s) : overlay actif.", m_gfx->name());
    } else {
        m_failed = true;
        Log("Session creee avec une API graphique non geree (Vulkan, OpenGL...) : pas d'overlay, le jeu n'est pas modifie.");
    }
    // Jeu lancé à travers SteamVR : XRPerfVR n'y ajoute ni panneau ni rapport (il affiche seulement si
    // la layer ne le peut pas, Vulkan ou OpenGL).
    m_presence.set(true, m_gfx != nullptr);

    // Mesures et résumé de session : indépendants de l'affichage, donc actifs aussi sans panneau.
    finishSummary();
    m_recording = m_recordManual = false;
    m_sessionStats.reset();
    m_sessionStart = nowSeconds();
    m_lastSummaryWrite = m_sessionStart;
    m_lastSample = -1.0;
    m_outRate.reset();
    m_lastOutSample = -1.0;
    m_gameText = Narrow(CurrentExeName(false));
    m_apiText = m_gfx ? m_gfx->name() : "autre (Vulkan, OpenGL...)";
    m_renderSize.store(0, std::memory_order_relaxed);
    readSystemInfo(createInfo ? createInfo->systemId : XR_NULL_SYSTEM_ID);
    // Matériel, pour l'en-tête des rapports : la carte est celle du périphérique Direct3D du jeu.
    m_cpuText.clear();
    m_gpuText.clear();
    if (m_config.summaryHardware) {
        m_cpuText = CpuModelName();
        LUID luid = {};
        if (m_gfx && m_gfx->adapterLuid(luid)) m_gpuText = GpuNameFromLuid(luid);
        if (m_gpuText.empty()) m_gpuText = MainGpuName();
        Log("Materiel : processeur \"%s\", carte graphique \"%s\".", m_cpuText.c_str(), m_gpuText.c_str());
    }
    // Rapport : en mode automatique, l'enregistrement couvre toute la session.
    if (m_reportControl.begin(m_config.reportMode, Hotkeys::comboDown(m_config.keyRecord))) startRecording(m_sessionStart, false);
    if (m_config.reportMode == ReportMode::Manual) Log("Rapports en mode manuel : Ctrl+Alt+touche %d pour demarrer / arreter.", m_config.keyRecord);

    // Mesure du temps GPU : seulement si elle est demandée dans le .ini et possible avec cette API.
    m_timingActive = false;
    m_implausibleSince = -1.0;
    m_implausibleMeasured = m_implausibleEstimate = m_implausibleLoad = 0.0;
    m_implausibleCount = 0;
    m_gpuSum = 0.0;
    m_gpuCount = 0;
    m_gpuShownMs = 0.f;
    m_lastGpuMeasure = -1.0;
    m_timedFrames = 0;
    if (m_config.gpuTiming && m_gfx) {
        const bool enabled = m_gfx->enableTiming();
        m_timingActive = enabled;
        Log(enabled ? "Mesure du temps GPU activee (horodatages %s)." : "Mesure du temps GPU impossible avec %s : estimation seule.",
            m_gfx->name());
    }
}

// Nom du casque et du runtime : deux lectures auprès du runtime, sans effet sur la session du jeu.
void Overlay::readSystemInfo(XrSystemId systemId) {
    m_systemText.clear();
    m_runtimeText.clear();
    if (m_next.GetSystemProperties && systemId != XR_NULL_SYSTEM_ID) {
        XrSystemProperties properties = {XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(m_next.GetSystemProperties(m_instance, systemId, &properties)))
            m_systemText = AsciiText(properties.systemName, sizeof(properties.systemName));
    }
    if (m_next.GetInstanceProperties) {
        XrInstanceProperties properties = {XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(m_next.GetInstanceProperties(m_instance, &properties))) {
            m_runtimeText = AsciiText(properties.runtimeName, sizeof(properties.runtimeName), 96);
            char version[48];
            std::snprintf(version, sizeof(version), " %u.%u.%u", static_cast<unsigned>(XR_VERSION_MAJOR(properties.runtimeVersion)),
                          static_cast<unsigned>(XR_VERSION_MINOR(properties.runtimeVersion)),
                          static_cast<unsigned>(XR_VERSION_PATCH(properties.runtimeVersion)));
            m_runtimeText += version;
        }
    }
    // Résolution recommandée pour chaque œil : celle que le runtime propose au jeu (réglage de
    // suréchantillonnage du logiciel du casque, ou de SteamVR, compris). Le jeu peut rendre plus ou
    // moins (sa propre échelle de rendu) : la comparaison dit qui décide de la résolution.
    m_recommendedWidth = m_recommendedHeight = 0;
    if (m_next.EnumerateViewConfigurationViews && systemId != XR_NULL_SYSTEM_ID) {
        XrViewConfigurationView views[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        uint32_t count = 0;
        if (XR_SUCCEEDED(m_next.EnumerateViewConfigurationViews(m_instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views)) &&
            count >= 1 && views[0].recommendedImageRectWidth > 0 && views[0].recommendedImageRectHeight > 0 &&
            views[0].recommendedImageRectWidth < 100000 && views[0].recommendedImageRectHeight < 100000) {
            m_recommendedWidth = static_cast<int>(views[0].recommendedImageRectWidth);
            m_recommendedHeight = static_cast<int>(views[0].recommendedImageRectHeight);
            Log("Resolution recommandee par le runtime : %d x %d par oeil (maximum %u x %u).", m_recommendedWidth, m_recommendedHeight,
                views[0].maxImageRectWidth, views[0].maxImageRectHeight);
        }
    }
    // Le runtime n'annonce pas toujours le nom commercial du casque : celui du .ini passe devant.
    m_headsetText = m_config.headsetName.empty() ? m_systemText : m_config.headsetName;
    Log("Casque annonce par le runtime : \"%s\" ; runtime : %s.", m_systemText.c_str(), m_runtimeText.empty() ? "?" : m_runtimeText.c_str());
}

void Overlay::onFrameSubmitted(const XrFrameEndInfo& frameEndInfo) noexcept {
    if (!frameEndInfo.layers) return;
    for (uint32_t i = 0; i < frameEndInfo.layerCount && i < 64; ++i) {
        const XrCompositionLayerBaseHeader* layer = frameEndInfo.layers[i];
        if (!layer || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
        // Première scène 3D de l'image : la zone de la texture réservée au premier œil est la
        // résolution à laquelle le jeu rend (après ses propres réglages de qualité).
        const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if (!projection->views || projection->viewCount == 0) return;
        const XrExtent2Di extent = projection->views[0].subImage.imageRect.extent;
        if (extent.width > 0 && extent.height > 0)
            m_renderSize.store((static_cast<uint64_t>(static_cast<uint32_t>(extent.width)) << 32) | static_cast<uint32_t>(extent.height),
                               std::memory_order_relaxed);
        return;
    }
}

void Overlay::openSummary() {
    std::wstring game = CurrentExeName(false);
    const size_t dot = game.find_last_of(L'.');
    if (dot != std::wstring::npos) game.erase(dot);
    const std::wstring fileName = LocalTimeFileStamp() + L"_" + game + L".txt";
    // Dossier choisi dans le .ini, sinon "sessions" à côté de la DLL ; s'il n'est pas accessible en
    // écriture, on se rabat sur le dossier de données de l'utilisateur.
    if (!m_summary.open(m_config.summaryFolder, fileName)) {
        const std::wstring fallback = FallbackSummaryFolder();
        if (fallback.empty() || !m_summary.open(fallback, fileName)) Log("Resume de session : impossible de creer le fichier.");
    }
}

void Overlay::writeSummary(double now, bool finished) {
    if (!m_summary.isOpen()) return;
    const std::string nowText = LocalTimeText();
    SessionInfo info;
    info.version = XRPO_VERSION;
    info.game = m_gameText.c_str();
    info.api = m_apiText.c_str();
    info.headset = m_headsetText.c_str();
    info.system = m_systemText.c_str();
    info.runtime = m_runtimeText.c_str();
    info.recommendedWidth = m_recommendedWidth;
    info.recommendedHeight = m_recommendedHeight;
    if (m_config.summaryHardware) {
        info.cpu = m_cpuText.c_str();
        info.gpu = m_gpuText.c_str();
    }
    info.startText = m_recordStartText.c_str();
    info.nowText = nowText.c_str();
    info.finished = finished;
    info.wallSeconds = now - m_recordStart;
    info.english = m_config.english;
    info.manual = m_recordManual;
    const std::string text = m_sessionStats.format(info);
    if (!m_summary.kept()) {
        m_summary.keep();
        Log("Resume de session : %s", Narrow(m_summary.path()).c_str());
    }
    m_summary.write(text.data(), text.size());
    m_lastSummaryWrite = now;
}

// Dernière écriture puis fermeture. Un enregistrement trop court ne laisse aucun fichier.
bool Overlay::finishSummary() {
    if (!m_summary.isOpen()) return false;
    const double now = nowSeconds();
    const bool keep = KeepReport(m_recordManual, m_sessionStats.activeSeconds(), now - m_recordStart);
    if (keep) writeSummary(now, true);
    m_summary.close();
    return keep;
}

void Overlay::startRecording(double now, bool manual) {
    m_sessionStats.restart();  // le rapport ne compte que la durée de l'enregistrement
    m_recording = true;
    m_recordManual = manual;
    m_recordStart = now;
    m_recordStartText = LocalTimeText();
    m_lastSummaryWrite = now;
    openSummary();
    if (manual) Log("Enregistrement manuel demarre.");
}

void Overlay::stopRecording(double now) {
    if (!m_recording) return;
    const bool kept = finishSummary();
    if (m_recordManual) {
        m_reportControl.stoppedManual(now, kept, now - m_recordStart);
        Log(kept ? "Enregistrement manuel arrete : rapport ecrit." : "Enregistrement manuel arrete : trop court, pas de rapport.");
    }
    m_recording = m_recordManual = false;
}

// Mode des rapports (changeable en jeu depuis XRPerfSensors) et raccourci du mode manuel.
void Overlay::updateRecording(double now) {
    const ReportControl::Step step = m_reportControl.update(m_config.reportMode, m_recording, Hotkeys::comboDown(m_config.keyRecord));
    if (step.stop) stopRecording(now);
    if (step.start) startRecording(now, step.manual);
}

void Overlay::onSessionDestroying() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hotkeys.takePending()) SaveState(m_config);
    stopRecording(nowSeconds());
    finishSummary();
    destroyResources();
    m_gfx.reset();
    m_session = XR_NULL_HANDLE;
    m_presence.set(false, false);
}

void Overlay::destroyResources() {
    if (m_gfx) m_gfx->waitIdle();
    destroyWindowResources();
    if (m_swapchain != XR_NULL_HANDLE) m_next.DestroySwapchain(m_swapchain);
    if (m_viewSpace != XR_NULL_HANDLE) m_next.DestroySpace(m_viewSpace);
    m_swapchain = XR_NULL_HANDLE;
    m_viewSpace = XR_NULL_HANDLE;
    m_panelImage = SwapImage();
    m_hasContent = false;
    m_panelDirty = true;
}

void Overlay::destroyWindowResources() {
    if (m_windowSwapchain != XR_NULL_HANDLE) m_next.DestroySwapchain(m_windowSwapchain);
    if (m_worldSpace != XR_NULL_HANDLE) m_next.DestroySpace(m_worldSpace);
    m_windowSwapchain = XR_NULL_HANDLE;
    m_worldSpace = XR_NULL_HANDLE;
    m_windowTried = false;
    m_worldTried = false;
    m_worldIsLocal = false;
    m_localWorldSet = false;
    m_windowImage = SwapImage();
    m_windowHasContent = false;
    m_windowShownWidth = m_windowShownHeight = 0;
    // Image déjà lue : à recopier dans la prochaine swapchain.
    m_windowPending = !m_windowPixels.empty();
}

void Overlay::onWaitFrame(const XrFrameState& state) {
    const double now = nowSeconds();
    std::lock_guard<std::mutex> lock(m_timingMutex);
    m_timer.onWaitFrame(now, state.predictedDisplayPeriod);
}

void Overlay::onBeginFrame() {
    {
        std::lock_guard<std::mutex> lock(m_timingMutex);
        m_timer.onBeginFrame();
    }
    if (!m_timingActive.load(std::memory_order_relaxed)) return;
    // Repère de début d'image pour la mesure du temps GPU (option).
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_timingActive.load(std::memory_order_relaxed) && m_gfx && m_session != XR_NULL_HANDLE) m_gfx->timingBegin();
}

// Mesures du temps GPU terminées : moyenne pour le panneau, courbe, résumé de session.
void Overlay::collectGpuTimings() {
    GpuTiming results[8];
    const int count = m_gfx->timingCollect(results, 8);
    for (int i = 0; i < count; ++i) {
        m_gpuSum += results[i].gpuMs;
        ++m_gpuCount;
        m_lastGpuMeasure = results[i].endTime;
        m_history.onGpuFrame(results[i].endTime, results[i].gpuMs);
        m_sessionStats.onGpuFrame(results[i].endTime, results[i].gpuMs);
    }
}

void Overlay::pollHotkeys(double now) {
    const unsigned changes = m_hotkeys.poll(m_config, now);
    if (changes & Hotkeys::ResetStats) m_stats.reset();
    if (changes & Hotkeys::WindowPlace) m_windowPlaceRequested = true;
    if ((changes & Hotkeys::WindowVisibility) && !m_config.window.enabled) m_windowPlaceRequested = false;
    // Contenu à jour dès le réaffichage ; nouvelle opacité dessinée tout de suite.
    if (changes & (Hotkeys::Visibility | Hotkeys::Opacity)) m_panelDirty = true;
    if (m_hotkeys.saveDue(now)) SaveState(m_config);
}

// Format de texture : sRGB 8 bits de préférence ; RGBA ou BGRA d'abord selon `preferBgra` (l'image de
// la fenêtre capturée est en BGRA : sans conversion si le runtime le propose). 0 si aucun ne convient.
int64_t Overlay::chooseFormat(bool preferBgra, bool& bgra, bool& srgb) {
    uint32_t count = 0;
    std::vector<int64_t> formats;
    if (XR_SUCCEEDED(m_next.EnumerateSwapchainFormats(m_session, 0, &count, nullptr)) && count > 0) {
        formats.resize(count);
        if (XR_FAILED(m_next.EnumerateSwapchainFormats(m_session, count, &count, formats.data()))) formats.clear();
        if (formats.size() > count) formats.resize(count);
    }
    const struct {
        DXGI_FORMAT format;
        bool bgra, srgb;
    } rgbaFirst[] = {
        {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false, true},
        {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, true, true},
        {DXGI_FORMAT_R8G8B8A8_UNORM, false, false},
        {DXGI_FORMAT_B8G8R8A8_UNORM, true, false},
    },
      bgraFirst[] = {
          {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, true, true},
          {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false, true},
          {DXGI_FORMAT_B8G8R8A8_UNORM, true, false},
          {DXGI_FORMAT_R8G8B8A8_UNORM, false, false},
      };
    for (const auto& candidate : preferBgra ? bgraFirst : rgbaFirst) {
        for (int64_t f : formats) {
            if (f == static_cast<int64_t>(candidate.format)) {
                bgra = candidate.bgra;
                srgb = candidate.srgb;
                return f;
            }
        }
    }
    return 0;
}

Overlay::ImageState Overlay::prepareImage(XrSwapchain swapchain, SwapImage& image) {
    if (!image.acquired) {
        XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (XR_FAILED(m_next.AcquireSwapchainImage(swapchain, &acquireInfo, &image.index))) return ImageState::Error;
        image.acquired = true;
        image.waited = false;
    }
    if (!image.waited) {
        XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = 1000000;  // 1 ms au plus : on ne bloque pas le jeu, on réessaie à l'image suivante
        const XrResult waitResult = m_next.WaitSwapchainImage(swapchain, &waitInfo);
        if (waitResult == XR_TIMEOUT_EXPIRED) {
            // D'après la spécification, le prochain appel attendra la même image.
            return ++image.waitTimeouts >= kMaxWaitTimeouts ? ImageState::Error : ImageState::NotYet;
        }
        image.waitTimeouts = 0;
        if (XR_FAILED(waitResult)) return ImageState::Error;
        image.waited = true;
    }
    return ImageState::Ready;
}

XrResult Overlay::releaseImage(XrSwapchain swapchain, SwapImage& image) {
    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    t_ownSwapchainCall = true;  // notre propre image : à ne pas compter comme une image du jeu en sortie de chaîne
    const XrResult result = m_next.ReleaseSwapchainImage(swapchain, &releaseInfo);
    t_ownSwapchainCall = false;
    image.acquired = false;
    image.waited = false;
    return result;
}

bool Overlay::ensureResources() {
    if (m_swapchain != XR_NULL_HANDLE) return true;
    if (m_resourcesTried) return false;
    m_resourcesTried = true;

    // Repère lié à la tête : le panneau suit le casque.
    XrReferenceSpaceCreateInfo spaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.f;
    XrResult result = m_next.CreateReferenceSpace(m_session, &spaceInfo, &m_viewSpace);
    if (XR_FAILED(result)) {
        Log("xrCreateReferenceSpace(VIEW) a echoue (%d).", static_cast<int>(result));
        m_viewSpace = XR_NULL_HANDLE;
        return false;
    }

    const int64_t chosen = chooseFormat(false, m_formatIsBgra, m_formatIsSrgb);
    if (chosen == 0) {
        Log("Aucun format de texture RGBA 8 bits propose par le runtime.");
        return false;
    }

    XrSwapchainCreateInfo swapchainInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format = chosen;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = Panel::kWidth;
    swapchainInfo.width = kTextureWidth;
    swapchainInfo.height = kTextureHeight;  // panneau puis montre ; chaque calque n'en montre que la partie dessinée
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = 1;
    swapchainInfo.mipCount = 1;
    result = m_next.CreateSwapchain(m_session, &swapchainInfo, &m_swapchain);
    if (XR_FAILED(result)) {
        Log("xrCreateSwapchain a echoue (%d).", static_cast<int>(result));
        m_swapchain = XR_NULL_HANDLE;
        return false;
    }
    if (!m_gfx->enumerateImages(m_next, m_swapchain, 0)) {
        Log("Impossible de recuperer les images de la swapchain.");
        m_next.DestroySwapchain(m_swapchain);
        m_swapchain = XR_NULL_HANDLE;
        return false;
    }
    Log("Swapchain de l'overlay creee (%dx%d, format DXGI %lld).", kTextureWidth, kTextureHeight, static_cast<long long>(chosen));
    return true;
}

// Réglages publiés par la fenêtre de XRPerfSensors : appliqués quand ils changent en cours de jeu.
void Overlay::pollSettings() {
    SettingsBlock block;
    if (!m_settingsReader.read(block)) return;
    if (!m_settingsSeen) {
        // Premier relevé de la session. En général ces réglages sont ceux du .ini, lu au lancement du
        // jeu, parfois en retard sur lui (un raccourci clavier a pu le modifier depuis) : on ne les
        // applique pas. Exception : l'utilisateur vient de changer un réglage dans la fenêtre, pendant
        // le chargement du jeu ou juste avant, et le .ini lu ne le contenait peut-être pas encore.
        m_settingsSeen = true;
        m_settingsGeneration = block.generation;
        const bool changedSinceIni = block.changeTickMs != 0 && block.changeTickMs + kSettingsGraceMs > m_configTickMs;
        if (!changedSinceIni) {
            m_lastBlock = block;
            m_hasLastBlock = true;
            return;
        }
        m_hasLastBlock = false;  // tout est repris : le .ini lu ne contenait peut-être pas ces réglages
    } else if (block.generation == m_settingsGeneration) {
        return;
    }
    m_settingsGeneration = block.generation;
    ApplySettingsUpdate(m_config, block, m_hasLastBlock ? &m_lastBlock : nullptr);
    m_lastBlock = block;
    m_hasLastBlock = true;
    m_panelDirty = true;
}

// Relevé des valeurs, quelques fois par seconde, que le panneau soit visible ou non.
void Overlay::sample(double now) {
    pollSettings();
    PanelData data;
    const StatsSnapshot stats = m_stats.snapshot(now);
    data.fpsValid = stats.valid;
    data.fps = stats.fps;
    data.fpsAvg = stats.avg;
    data.fpsMin = stats.min;
    data.fpsMax = stats.max;
    data.cpuFrameMs = stats.cpuMs;
    {
        std::lock_guard<std::mutex> timing(m_timingMutex);
        data.targetFps = std::round(m_timer.targetFps());
    }
    SensorBlock sensors;
    if (m_sensors.read(sensors)) {
        data.sensorsOk = true;
        data.cpuLoad = sensors.cpuLoad;
        data.cpuTemp = sensors.cpuTemp;
        data.cpuMHz = sensors.cpuMHz;
        data.gpuLoad = sensors.gpuLoad;
        data.gpuTemp = sensors.gpuTemp;
        data.gpuMHz = sensors.gpuMHz;
        data.vramUsedMB = sensors.vramUsedMB;
        data.vramTotalMB = sensors.vramTotalMB;
        // Temps GPU par image, estimé : part du temps où le GPU travaille x durée d'une image.
        if (stats.valid && stats.fps > 0.f && !std::isnan(sensors.gpuLoad))
            data.gpuFrameEstimateMs = (sensors.gpuLoad / 100.f) * (1000.f / stats.fps);
    }
    // Temps GPU affiché : la moyenne des mesures reçues depuis le relevé précédent si la mesure
    // réelle est active et fonctionne (dernière mesure de moins d'une seconde), sinon l'estimation.
    if (m_gpuCount > 0) {
        m_gpuShownMs = static_cast<float>(m_gpuSum / m_gpuCount);
        m_gpuSum = 0.0;
        m_gpuCount = 0;
    }
    if (m_timingActive && m_lastGpuMeasure >= 0.0 && now - m_lastGpuMeasure < 1.0) {
        data.gpuFrameMs = m_gpuShownMs;
        data.gpuMeasured = true;
    } else {
        data.gpuFrameMs = data.gpuFrameEstimateMs;
        // La courbe reçoit l'estimation pour la période écoulée depuis le relevé précédent.
        if (m_lastSample >= 0.0) m_history.onGpuEstimate(m_lastSample, now, data.gpuFrameEstimateMs);
    }
    if (data.gpuMeasured) checkGpuPlausibility(data, now);
    // Cadence en sortie de chaîne : affichée seulement si le casque reçoit nettement plus d'images
    // que le jeu n'en rend, c'est-à-dire si un outil de génération d'images est à l'œuvre.
    if (g_outputTap.installed.load(std::memory_order_relaxed)) {
        const int64_t period = g_outputTap.periodNs.load(std::memory_order_relaxed);
        const float refresh = period > 0 ? static_cast<float>(1e9 / static_cast<double>(period)) : 0.f;
        const float out = SnapToRefresh(m_outRate.rate(), refresh);
        if (stats.valid && !std::isnan(out) && out > stats.fps * 1.15f) data.outFps = out;
    }
    if (m_config.clock) data.clockMinutes = LocalClockMinutes();

    // Casque et résolution de rendu : relevés dans tous les cas (résumé), affichés selon le réglage.
    data.showInfo = m_config.headsetInfo;
    const size_t nameLength = m_headsetText.size() < sizeof(data.headset) - 1 ? m_headsetText.size() : sizeof(data.headset) - 1;
    std::memcpy(data.headset, m_headsetText.data(), nameLength);
    data.headset[nameLength] = '\0';
    const uint64_t renderSize = m_renderSize.load(std::memory_order_relaxed);
    data.renderWidth = static_cast<int>(renderSize >> 32);
    data.renderHeight = static_cast<int>(renderSize & 0xFFFFFFFFu);
    data.recommendedWidth = m_recommendedWidth;
    data.recommendedHeight = m_recommendedHeight;

    // Ligne session / batterie (option). La batterie vient du runtime, comme pour la montre.
    data.statusElements = m_config.panelElements;
    data.sessionSeconds = now - m_sessionStart;
    // Témoin d'enregistrement manuel (en cours, rapport écrit, trop court).
    data.recordState = m_reportControl.indicator(now, m_recording && m_recordManual, m_recordStart, data.recordSeconds);
    // Pour XRPerfVR, s'il affiche le panneau de ce jeu à la place de la layer (Vulkan, OpenGL).
    m_presence.setRecord(data.recordState, static_cast<float>(data.recordSeconds));
    if ((data.statusElements & kWatchBattery) && m_hands) {
        for (int hand = 0; hand < 2; ++hand) {
            float level = 0.f;
            bool charging = false;
            if (m_hands->battery(m_session, hand, now, level, charging)) {
                data.battery[hand] = level;
                data.charging[hand] = charging;
            }
        }
    }

    // Apparence. La courbe n'est préparée que pour la présentation qui l'affiche.
    data.layout = m_config.layout;
    data.theme = ThemeOf(m_config);
    data.english = m_config.english;
    if (data.layout == Layout::Graph) {
        m_history.columns(now, data.graphFrameMs, data.graphCpuMs, data.graphGpuMs);
        data.graphValid = true;
    }

    // Cadence sur une seconde : seulement quand cette seconde est tout entière dans l'enregistrement.
    m_sessionStats.onSample(data, stats.fullSecond && now - m_recordStart >= 1.0, now);
    if (m_config.watch.enabled) sampleWatch(now, data, stats.valid, stats.fps);

    // Fenêtre du bureau : même panneau, toujours opaque, quel que soit le réglage dans le casque.
    if (m_panelShare.wanted()) {
        if (!m_desktopPanel) m_desktopPanel.reset(new Panel());
        m_desktopPanel->render(data, 1.f, 1.f);
        m_panelShare.publish(m_desktopPanel->pixels(), Panel::contentHeight(data));
    }
    m_lastData = data;
    m_lastSample = now;
    m_panelDirty = true;
}

// La mesure reçue doit rester compatible avec l'estimation : une carte chargée à 88 % qui ne
// travaillerait que 0,2 ms par image de 14 ms est impossible. Si l'écart dure, la mesure est arrêtée
// pour la session et l'estimation reprend partout (panneau, courbe, rapport).
void Overlay::checkGpuPlausibility(PanelData& data, double now) {
    if (!data.sensorsOk || !(data.gpuLoad >= kImplausibleLoad) || !(data.gpuFrameEstimateMs > 0.f)) return;  // rien pour juger
    if (!(data.gpuFrameMs < kImplausibleRatio * data.gpuFrameEstimateMs)) {
        m_implausibleSince = -1.0;
        m_implausibleMeasured = m_implausibleEstimate = m_implausibleLoad = 0.0;
        m_implausibleCount = 0;
        return;
    }
    if (m_implausibleSince < 0.0) m_implausibleSince = now;
    m_implausibleMeasured += data.gpuFrameMs;
    m_implausibleEstimate += data.gpuFrameEstimateMs;
    m_implausibleLoad += data.gpuLoad;
    ++m_implausibleCount;
    if (now - m_implausibleSince < kImplausibleSeconds) return;

    const float measured = static_cast<float>(m_implausibleMeasured / m_implausibleCount);
    const float estimate = static_cast<float>(m_implausibleEstimate / m_implausibleCount);
    const float load = static_cast<float>(m_implausibleLoad / m_implausibleCount);
    m_gfx->stopTiming();
    m_timingActive = false;
    m_sessionStats.rejectGpuMeasure(measured, estimate, load);
    Log("Mesure du temps GPU ecartee : %.1f ms mesure contre %.1f ms estime, carte chargee a %.0f %% ; les reperes de la layer "
        "n'encadrent pas le rendu du jeu. Estimation affichee pour le reste de la session.",
        static_cast<double>(measured), static_cast<double>(estimate), static_cast<double>(load));
    data.gpuFrameMs = data.gpuFrameEstimateMs;
    data.gpuMeasured = false;
    if (m_lastSample >= 0.0) m_history.onGpuEstimate(m_lastSample, now, data.gpuFrameEstimateMs);
}

// Dessine le dernier relevé et le copie dans une image de la swapchain.
void Overlay::uploadPanel() {
    // 1. Une image de la swapchain doit être acquise puis "attendue" avant d'y écrire.
    const ImageState state = prepareImage(m_swapchain, m_panelImage);
    if (state == ImageState::Error) {
        // Attente trop longue (240 images) ou échec : compte comme un échec de mise à jour.
        if (m_panelImage.waitTimeouts >= kMaxWaitTimeouts)
            m_uploadErrors = kMaxUploadErrors;
        else
            ++m_uploadErrors;
        return;
    }
    if (state == ImageState::NotYet) return;

    if (m_gfx->busy(0)) return;  // inutile de dessiner si la copie ne peut pas partir tout de suite

    // 2. Dessin en mémoire : le panneau en haut de la texture, la montre en dessous (si elle est active).
    m_panel.render(m_lastData, m_config.opacity, m_config.globalOpacity);
    std::memcpy(m_texture.data(), m_panel.pixels(), static_cast<size_t>(Panel::kWidth) * Panel::kHeight * 4);
    const bool watch = m_config.watch.enabled;
    if (watch || m_shownWatch) {
        if (watch) m_watch.render(m_lastWatch, m_config.watch.opacity);
        for (int row = 0; row < WatchFace::kSize; ++row) {
            uint8_t* line = m_texture.data() + (static_cast<size_t>(kWatchTop + row) * kTextureWidth) * 4;
            if (watch)
                std::memcpy(line, m_watch.pixels() + static_cast<size_t>(row) * WatchFace::kSize * 4, static_cast<size_t>(WatchFace::kSize) * 4);
            else
                std::memset(line, 0, static_cast<size_t>(WatchFace::kSize) * 4);
        }
    }

    // 3. Conversion vers l'ordre d'octets de la texture (inutile pour le format habituel, RGBA sRGB).
    const uint8_t* pixels = m_texture.data();
    if (m_formatIsBgra || !m_formatIsSrgb) {
        const uint8_t* src = pixels;
        uint8_t* dst = m_converted.data();
        const uint8_t* linear = m_formatIsSrgb ? nullptr : srgbToLinearTable();
        const size_t pixelCount = static_cast<size_t>(kTextureWidth) * kTextureHeight;
        for (size_t i = 0; i < pixelCount; ++i, src += 4, dst += 4) {
            const uint8_t r = linear ? linear[src[0]] : src[0];
            const uint8_t g = linear ? linear[src[1]] : src[1];
            const uint8_t b = linear ? linear[src[2]] : src[2];
            dst[0] = m_formatIsBgra ? b : r;
            dst[1] = g;
            dst[2] = m_formatIsBgra ? r : b;
            dst[3] = src[3];
        }
        pixels = m_converted.data();
    }

    // 4. Copie dans la texture puis restitution au runtime.
    const UploadResult upload = m_gfx->upload(0, m_panelImage.index, pixels, kTextureWidth, kTextureHeight);
    if (upload == UploadResult::Busy) return;  // l'image reste acquise et prête, nouvel essai à l'image suivante
    const XrResult releaseResult = releaseImage(m_swapchain, m_panelImage);
    if (upload == UploadResult::Error || XR_FAILED(releaseResult)) {
        ++m_uploadErrors;
        return;
    }
    m_uploadErrors = 0;
    m_hasContent = true;
    m_panelDirty = false;
    m_shownLayout = m_lastData.layout;
    m_shownInfo = m_lastData.showInfo;
    m_shownStatus = m_lastData.statusElements != 0;
    m_shownWatch = watch;
}

XrPosef Overlay::quadPose() const {
    const Pose p = PanelViewPose(m_config);
    XrPosef pose = {};
    pose.position = {p.position.x, p.position.y, p.position.z};
    pose.orientation = {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w};
    return pose;
}

int Overlay::onEndFrame(XrCompositionLayerQuad (&quads)[kMaxQuads], XrTime displayTime, bool allowLayer) {
    const double now = nowSeconds();

    // Temps CPU de l'image : du retour de xrWaitFrame à l'appel de xrEndFrame.
    float cpuMs = std::numeric_limits<float>::quiet_NaN();
    {
        std::lock_guard<std::mutex> timing(m_timingMutex);
        cpuMs = m_timer.onEndFrame(now);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session == XR_NULL_HANDLE) return 0;

    // Mesures : toujours actives, même panneau masqué ou sans affichage possible (Vulkan...).
    // Repère de fin d'image pour la mesure du temps GPU, avant toute copie du panneau : seul le travail
    // du jeu est compté. Les mesures terminées sont relevées au passage, sans jamais attendre le GPU.
    if (m_timingActive && m_gfx) {
        m_gfx->timingEnd(now);
        collectGpuTimings();
        // Mesure activée mais muette (pilote qui ne renvoie rien) : signalé une fois dans le journal.
        if (m_timedFrames < 300 && ++m_timedFrames == 300 && m_lastGpuMeasure < 0.0)
            Log("Mesure du temps GPU : aucun resultat apres 300 images, l'estimation reste affichee.");
    }
    m_stats.onFrame(now, cpuMs);
    m_sessionStats.onFrame(now, cpuMs);
    m_history.onFrame(now, cpuMs);
    // Raccourcis : seulement si la layer peut afficher. Sinon (Vulkan, OpenGL, affichage abandonné),
    // XRPerfVR affiche le panneau d'un jeu SteamVR et traite lui-même les raccourcis.
    if (m_gfx && !m_failed) pollHotkeys(now);
    // Le compteur de sortie de chaîne est relevé à sa propre cadence, indépendante de refresh_ms.
    if (m_lastOutSample < 0.0 || now - m_lastOutSample >= 0.25) {
        if (g_outputTap.installed.load(std::memory_order_relaxed)) m_outRate.add(now, g_outputTap.fresh.load(std::memory_order_relaxed));
        m_lastOutSample = now;
    }
    if (m_lastSample < 0.0 || (now - m_lastSample) * 1000.0 >= m_config.refreshMs) sample(now);
    // Rapport : le raccourci est relevé par la layer dès qu'elle écrit le rapport, même sans affichage.
    updateRecording(now);
    if (m_summary.isOpen() && now - m_lastSummaryWrite >= kReportPeriod &&
        m_sessionStats.activeSeconds() >= (m_recordManual ? kManualReportMinSeconds : kReportMinSeconds))
        writeSummary(now, false);

    // Affichage abandonné pour la session : XRPerfVR peut prendre le relais dans un jeu SteamVR.
    if (m_failed) m_presence.set(true, false);

    // Affichage.
    const bool wantPanel = m_config.visible, wantWatch = m_config.watch.enabled;
    const bool wantWindow = m_config.window.enabled && !m_windowFailed;
    if (m_failed || !m_gfx || (!wantPanel && !wantWatch && !wantWindow) || !allowLayer) return 0;

    if (!ensureResources()) {
        m_failed = true;
        Log("Overlay desactive pour cette session (ressources indisponibles).");
        return 0;
    }
    if ((wantPanel || wantWatch) && (m_panelDirty || m_panelImage.acquired)) uploadPanel();
    if (m_uploadErrors >= kMaxUploadErrors) {
        m_failed = true;
        Log("Overlay desactive pour cette session (echecs repetes de mise a jour de la texture).");
        return 0;
    }
    if (wantWindow) updateWindow(now, displayTime);

    int count = 0;
    // Fenêtre capturée : en premier, sous le panneau et la montre.
    if (wantWindow && windowQuad(displayTime, quads[count])) ++count;
    if (!m_hasContent) return count;

    const auto baseQuad = [&]() -> XrCompositionLayerQuad& {
        XrCompositionLayerQuad& quad = quads[count++];
        quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.space = m_viewSpace;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = m_swapchain;
        quad.subImage.imageArrayIndex = 0;
        return quad;
    };

    // Panneau de performances : devant les yeux, ou accroché à une main.
    if (wantPanel) {
        XrSpace space = m_viewSpace;
        XrPosef pose = quadPose();
        const HandResult hand =
            handQuad(m_config.anchor, m_config.handPlacement, m_config.handAlong, m_config.handLift, m_config.gazeOnly, displayTime, m_panelLooked, space, pose);
        if (hand != HandResult::Hide) {
            XrCompositionLayerQuad& quad = baseQuad();
            quad.space = space;
            quad.pose = pose;
            quad.subImage.imageRect.offset = {0, 0};
            // Selon la présentation, le bas de la texture est vide : le calque ne montre que la partie dessinée.
            // La hauteur est celle de la dernière image copiée dans la texture, pas celle des réglages en cours.
            const int height = Panel::contentHeight(m_shownLayout, m_shownInfo, m_shownStatus);
            quad.subImage.imageRect.extent = {Panel::kWidth, height};
            quad.size = {m_config.width, m_config.width * static_cast<float>(height) / static_cast<float>(Panel::kWidth)};
        }
    }

    // Montre : au poignet, ou en bas à droite du champ de vision si elle n'est pas sur une main (ou si
    // les manettes ne sont pas disponibles dans ce jeu).
    if (wantWatch && m_shownWatch) {
        XrSpace space = m_viewSpace;
        XrPosef pose = {};
        const Pose view = WatchViewPose();
        pose.position = {view.position.x, view.position.y, view.position.z};
        pose.orientation = {view.orientation.x, view.orientation.y, view.orientation.z, view.orientation.w};
        const WatchConfig& w = m_config.watch;
        const HandResult hand = handQuad(w.anchor, w.placement, w.along, w.lift, w.gazeOnly, displayTime, m_watchLooked, space, pose);
        if (hand != HandResult::Hide) {
            XrCompositionLayerQuad& quad = baseQuad();
            quad.space = space;
            quad.pose = pose;
            quad.subImage.imageRect.offset = {0, kWatchTop};
            quad.subImage.imageRect.extent = {WatchFace::kSize, WatchFace::kSize};
            quad.size = {w.size, w.size};
        }
    }
    return count;
}

// Calque accroché à une main. Sans suivi des manettes (non demandé au lancement du jeu, ou jeu qui ne
// déclare pas de manettes), le calque reste devant les yeux.
Overlay::HandResult Overlay::handQuad(Anchor anchor, HandPlacement placement, float along, float lift, bool gazeOnly, XrTime displayTime,
                                      bool& looked, XrSpace& space, XrPosef& pose) {
    if (anchor == Anchor::View) return HandResult::NotOnHand;
    const int hand = anchor == Anchor::LeftHand ? 0 : 1;
    const Pose local = HandPlacementPose(hand == 0 ? Hand::Left : Hand::Right, placement, along, lift);
    return handQuadAt(hand, local, gazeOnly, displayTime, looked, space, pose);
}

Overlay::HandResult Overlay::handQuadAt(int hand, const Pose& local, bool gazeOnly, XrTime displayTime, bool& looked, XrSpace& space, XrPosef& pose) {
    if (!m_hands || !m_next.LocateSpace) return HandResult::NotOnHand;
    const XrSpace handSpace = m_hands->handSpace(m_session, hand);
    if (handSpace == XR_NULL_HANDLE) return HandResult::NotOnHand;

    // Où est la main par rapport à la tête, à l'instant d'affichage ? Main non suivie (manette posée,
    // hors du champ des capteurs) : le calque disparaît plutôt que de rester figé n'importe où.
    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(m_next.LocateSpace(handSpace, m_viewSpace, displayTime, &location)) ||
        !(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) || !(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        looked = false;
        return HandResult::Hide;
    }
    if (gazeOnly) {
        const Pose handInView = {{location.pose.position.x, location.pose.position.y, location.pose.position.z},
                                 {location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z, location.pose.orientation.w}};
        looked = IsLookedAt(Compose(handInView, local), looked);
        if (!looked) return HandResult::Hide;
    }
    space = handSpace;
    pose.position = {local.position.x, local.position.y, local.position.z};
    pose.orientation = {local.orientation.x, local.orientation.y, local.orientation.z, local.orientation.w};
    return HandResult::Show;
}

// --- Fenêtre capturée ------------------------------------------------------------------------------

// Lit la dernière image de la capture et la copie dans la texture de la fenêtre.
void Overlay::updateWindow(double now, XrTime displayTime) {
    (void)displayTime;
    // XRPerfSensors ne capture que tant que quelqu'un veut l'image : signal renouvelé deux fois par seconde.
    if (m_windowWantedAt < 0.0 || now - m_windowWantedAt >= 0.5) {
        m_windowShare.markWanted();
        m_windowWantedAt = now;
    }
    // Nouvelle image : lue seulement si la précédente est déjà dans la texture (une copie à la fois).
    if (!m_windowPending && !m_windowImage.acquired) {
        int width = 0, height = 0;
        if (m_windowShare.readNew(m_windowPixels, width, height)) {
            m_windowWidth = width;
            m_windowHeight = height;
            m_windowPending = true;
        }
    }
    if (!m_windowPending && !m_windowImage.acquired) return;
    if (!ensureWindowSwapchain()) {
        m_windowFailed = true;
        Log("Fenetre capturee desactivee pour cette session (swapchain indisponible).");
        return;
    }
    uploadWindow();
    if (m_windowErrors >= kMaxUploadErrors) {
        m_windowFailed = true;
        Log("Fenetre capturee desactivee pour cette session (echecs repetes de mise a jour de la texture).");
    }
}

// Swapchain de la fenêtre : à la taille maximale de l'image capturée, créée à la première image reçue.
bool Overlay::ensureWindowSwapchain() {
    if (m_windowSwapchain != XR_NULL_HANDLE) return true;
    if (m_windowTried) return false;
    m_windowTried = true;
    const int64_t format = chooseFormat(true, m_windowFormatBgra, m_windowFormatSrgb);
    if (format == 0) return false;
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = kWindowMaxWidth;
    info.height = kWindowMaxHeight;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    const XrResult result = m_next.CreateSwapchain(m_session, &info, &m_windowSwapchain);
    if (XR_FAILED(result)) {
        Log("xrCreateSwapchain (fenetre capturee) a echoue (%d).", static_cast<int>(result));
        m_windowSwapchain = XR_NULL_HANDLE;
        return false;
    }
    if (!m_gfx->enumerateImages(m_next, m_windowSwapchain, 1)) {
        Log("Impossible de recuperer les images de la swapchain de la fenetre capturee.");
        m_next.DestroySwapchain(m_windowSwapchain);
        m_windowSwapchain = XR_NULL_HANDLE;
        return false;
    }
    Log("Fenetre capturee : swapchain creee (%dx%d, format DXGI %lld).", kWindowMaxWidth, kWindowMaxHeight, static_cast<long long>(format));
    return true;
}

void Overlay::uploadWindow() {
    const ImageState state = prepareImage(m_windowSwapchain, m_windowImage);
    if (state == ImageState::Error) {
        if (m_windowImage.waitTimeouts >= kMaxWaitTimeouts)
            m_windowErrors = kMaxUploadErrors;
        else
            ++m_windowErrors;
        return;
    }
    if (state == ImageState::NotYet || m_gfx->busy(1)) return;

    // L'image est en BGRA sRGB : conversion si la texture est dans un autre ordre ou linéaire. Elle est
    // copiée avec une colonne et une ligne transparentes de plus (dans la limite de la texture) : le
    // filtrage du bord droit et du bas ne mélange pas l'image avec un reste d'une image précédente, plus grande.
    const int width = m_windowWidth, height = m_windowHeight;
    const int paddedWidth = width < kWindowMaxWidth ? width + 1 : width;
    const int paddedHeight = height < kWindowMaxHeight ? height + 1 : height;
    m_windowConverted.assign(static_cast<size_t>(paddedWidth) * paddedHeight * 4, 0);
    const bool convert = !m_windowFormatBgra || !m_windowFormatSrgb;
    const uint8_t* linear = m_windowFormatSrgb ? nullptr : srgbToLinearTable();
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = m_windowPixels.data() + static_cast<size_t>(y) * width * 4;
        uint8_t* dst = m_windowConverted.data() + static_cast<size_t>(y) * paddedWidth * 4;
        if (!convert) {
            std::memcpy(dst, src, static_cast<size_t>(width) * 4);
            continue;
        }
        for (int x = 0; x < width; ++x, src += 4, dst += 4) {
            const uint8_t b = linear ? linear[src[0]] : src[0];
            const uint8_t g = linear ? linear[src[1]] : src[1];
            const uint8_t r = linear ? linear[src[2]] : src[2];
            dst[0] = m_windowFormatBgra ? b : r;
            dst[1] = g;
            dst[2] = m_windowFormatBgra ? r : b;
            dst[3] = src[3];
        }
    }
    const uint8_t* pixels = m_windowConverted.data();
    const UploadResult upload = m_gfx->upload(1, m_windowImage.index, pixels, paddedWidth, paddedHeight);
    if (upload == UploadResult::Busy) return;
    const XrResult released = releaseImage(m_windowSwapchain, m_windowImage);
    if (upload == UploadResult::Error || XR_FAILED(released)) {
        ++m_windowErrors;
        return;
    }
    m_windowErrors = 0;
    m_windowPending = false;
    m_windowHasContent = true;
    m_windowShownWidth = width;
    m_windowShownHeight = height;
}

// Repère de la pièce, pour la fenêtre posée dans l'espace : STAGE (le même d'un jeu à l'autre, celui de
// la pose enregistrée), sinon LOCAL.
bool Overlay::ensureWorldSpace() {
    if (m_worldSpace != XR_NULL_HANDLE) return true;
    if (m_worldTried) return false;
    m_worldTried = true;
    XrReferenceSpaceCreateInfo info = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    info.poseInReferenceSpace.orientation.w = 1.f;
    for (XrReferenceSpaceType type : {XR_REFERENCE_SPACE_TYPE_STAGE, XR_REFERENCE_SPACE_TYPE_LOCAL}) {
        info.referenceSpaceType = type;
        if (XR_SUCCEEDED(m_next.CreateReferenceSpace(m_session, &info, &m_worldSpace)) && m_worldSpace != XR_NULL_HANDLE) {
            m_worldIsLocal = type == XR_REFERENCE_SPACE_TYPE_LOCAL;
            if (m_worldIsLocal) Log("Fenetre capturee : pas d'espace STAGE, posee dans l'espace LOCAL (position non enregistree).");
            return true;
        }
        m_worldSpace = XR_NULL_HANDLE;
    }
    Log("Fenetre capturee : aucun repere de la piece disponible.");
    return false;
}

bool Overlay::headInWorld(XrTime displayTime, Pose& head) {
    if (!m_next.LocateSpace || m_viewSpace == XR_NULL_HANDLE || !ensureWorldSpace()) return false;
    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(m_next.LocateSpace(m_viewSpace, m_worldSpace, displayTime, &location)) ||
        !(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) || !(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return false;
    head = {{location.pose.position.x, location.pose.position.y, location.pose.position.z},
            {location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z, location.pose.orientation.w}};
    return true;
}

// Calque de la fenêtre : sur une main, sinon posé dans l'espace. Retourne false s'il n'est pas à montrer.
bool Overlay::windowQuad(XrTime displayTime, XrCompositionLayerQuad& quad) {
    // Pas encore d'image (capture qui démarre) : Ctrl+Alt+G attend la première.
    if (!m_windowHasContent || m_windowShownWidth <= 0 || m_windowShownHeight <= 0) return false;
    WindowConfig& w = m_config.window;
    const float height = w.width * static_cast<float>(m_windowShownHeight) / static_cast<float>(m_windowShownWidth);
    XrSpace space = XR_NULL_HANDLE;
    XrPosef pose = {};
    bool placed = false;
    if (w.anchor != WindowAnchor::World && !m_windowPlaceRequested) {
        const int hand = w.anchor == WindowAnchor::LeftHand ? 0 : 1;
        const Pose local = WindowHandPose(hand == 0 ? Hand::Left : Hand::Right, w.placement, w.along, w.lift, height);
        const HandResult result = handQuadAt(hand, local, w.gazeOnly, displayTime, m_windowLooked, space, pose);
        if (result == HandResult::Hide) return false;
        placed = result == HandResult::Show;
        // Manettes non suivies dans ce jeu : la fenêtre est posée dans l'espace.
    }
    if (!placed) {
        if (!ensureWorldSpace()) return false;
        Pose& world = m_worldIsLocal ? m_localWorld : w.world;
        bool& worldSet = m_worldIsLocal ? m_localWorldSet : w.worldSet;
        // Pas encore posée (ou Ctrl+Alt+G) : devant soi, maintenant.
        if (!worldSet || m_windowPlaceRequested) {
            Pose head;
            if (!headInWorld(displayTime, head)) return false;  // tête non suivie : nouvel essai à l'image suivante
            world = PlaceInFront(head, w.distance);
            worldSet = true;
            m_windowPlaceRequested = false;
            // Pose dans la pièce (STAGE) : enregistrée tout de suite, pour les jeux suivants.
            if (!m_worldIsLocal) SaveState(m_config);
        }
        space = m_worldSpace;
        pose.position = {world.position.x, world.position.y, world.position.z};
        pose.orientation = {world.orientation.x, world.orientation.y, world.orientation.z, world.orientation.w};
    }
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = space;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = m_windowSwapchain;
    quad.subImage.imageArrayIndex = 0;
    quad.subImage.imageRect.offset = {0, 0};
    quad.subImage.imageRect.extent = {m_windowShownWidth, m_windowShownHeight};
    quad.pose = pose;
    quad.size = {w.width, height};
    return true;
}

// Valeurs de la montre, relevées avec celles du panneau.
void Overlay::sampleWatch(double now, const PanelData& data, bool fpsValid, float fps) {
    WatchData& w = m_lastWatch;
    w.style = m_config.watch.style;
    w.elements = m_config.watch.elements;
    w.theme = data.theme;
    w.english = m_config.english;
    const int seconds = LocalClockSeconds();
    w.hour = seconds / 3600;
    w.minute = (seconds / 60) % 60;
    w.second = seconds % 60;
    w.sessionSeconds = now - m_sessionStart;
    w.fpsValid = fpsValid;
    w.fps = fps;
    w.targetFps = data.targetFps;
    for (int hand = 0; hand < 2; ++hand) {
        float level = 0.f;
        bool charging = false;
        if ((w.elements & kWatchBattery) && m_hands && m_hands->battery(m_session, hand, now, level, charging)) {
            w.battery[hand] = level;
            w.charging[hand] = charging;
        } else {
            w.battery[hand] = std::numeric_limits<float>::quiet_NaN();
            w.charging[hand] = false;
        }
    }
}

void Overlay::onLayerRejected(XrResult result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (++m_rejections >= kMaxRejections && !m_failed) {
        m_failed = true;
        Log("Le runtime refuse le calque de l'overlay (%d) : overlay desactive pour cette session.", static_cast<int>(result));
    }
}

}  // namespace xrpo
