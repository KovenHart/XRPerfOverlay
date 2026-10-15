// XR Perf Overlay - API layer OpenXR.
//
// Principe : le chargeur OpenXR insère cette DLL entre le jeu et le runtime. Nous n'interceptons
// que six fonctions (création / destruction d'instance et de session, xrWaitFrame, xrBeginFrame,
// xrEndFrame) ; toutes les autres sont transmises telles quelles. Le seul changement visible par
// le runtime est un calque "quad" ajouté à la fin de la liste soumise par le jeu.
//
// S'il y a d'autres layers sous la nôtre (un outil de génération d'images, par exemple), un second
// maillon, purement compteur, est ajouté tout en bas de la chaîne pour mesurer ce que le runtime
// reçoit réellement (voir "Maillon de sortie").
//
// Règle de conduite : en cas de doute ou d'erreur, on s'efface et on laisse passer l'appel d'origine.

#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

#include "config.h"
#include "hand_input.h"
#include "log.h"
#include "output_tap.h"
#include "overlay.h"
#include "version.h"
#include "xr.h"

namespace {

using namespace xrpo;

const char kLayerName[] = "XR_APILAYER_NOVENDOR_perf_overlay";
const char kOutputLayerName[] = "XR_APILAYER_NOVENDOR_perf_overlay_output";
const uint32_t kMaxLayers = 64;
const size_t kMaxChain = 32;  // nombre maximal de layers sous la nôtre pour ajouter le maillon de sortie

HMODULE g_module = nullptr;

std::mutex g_mutex;  // protège les globales ci-dessous (création / destruction uniquement)
NextFunctions g_next;
bool g_active = false;  // false : la layer se contente de transmettre les appels
std::shared_ptr<Overlay> g_overlay;
std::shared_ptr<HandInput> g_hands;  // manettes et batterie (nul si rien n'est demandé)

std::shared_ptr<HandInput> hands() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_hands;
}

std::shared_ptr<Overlay> overlayFor(XrSession session) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return (g_overlay && g_overlay->session() == session) ? g_overlay : nullptr;
}

// Reconnaît, à son nom, une layer qui génère des images intermédiaires. OFXR Bridge s'enregistre
// sous le nom "XR_APILAYER_XRFrameBridge_diagnostic".
bool isFrameGenerationLayer(const char* layerName) {
    char lowered[XR_MAX_API_LAYER_NAME_SIZE];
    size_t i = 0;
    for (; layerName[i] && i + 1 < sizeof(lowered); ++i)
        lowered[i] = (layerName[i] >= 'A' && layerName[i] <= 'Z') ? static_cast<char>(layerName[i] - 'A' + 'a') : layerName[i];
    lowered[i] = '\0';
    for (const char* marker : {"framebridge", "ofxr", "xrfg", "framegen", "frame_gen", "framegeneration"})
        if (std::strstr(lowered, marker)) return true;
    return false;
}

// Ajoute `text` à la fin de `buffer` s'il y a la place (sinon ne fait rien).
void appendText(char* buffer, size_t capacity, const char* text) {
    const size_t used = std::strlen(buffer), length = std::strlen(text);
    if (used + length + 1 > capacity) return;
    std::memcpy(buffer + used, text, length + 1);
}

template <typename T>
void resolve(PFN_xrGetInstanceProcAddr getProcAddr, XrInstance instance, const char* name, T& function) {
    PFN_xrVoidFunction raw = nullptr;
    function = XR_SUCCEEDED(getProcAddr(instance, name, &raw)) ? reinterpret_cast<T>(raw) : nullptr;
}

// Le runtime propose-t-il cette extension ? En cas de doute : non. La question est posée au dernier
// maillon de la chaîne (le runtime, au travers du chargeur), jamais à une autre layer : avant la
// création de l'instance, une layer n'est pas tenue de répondre.
bool RuntimeOffersExtension(const XrApiLayerNextInfo* chain, const char* extension) {
    const XrApiLayerNextInfo* last = chain;
    for (int depth = 0; last && last->next; ++depth) {
        if (depth > 64 || last->next->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            last->next->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION || last->next->structSize != sizeof(XrApiLayerNextInfo))
            return false;
        last = last->next;
    }
    if (!last || !last->nextGetInstanceProcAddr) return false;
    const PFN_xrGetInstanceProcAddr getProcAddr = last->nextGetInstanceProcAddr;
    PFN_xrEnumerateInstanceExtensionProperties enumerate = nullptr;
    resolve(getProcAddr, XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", enumerate);
    if (!enumerate) return false;
    uint32_t count = 0;
    if (XR_FAILED(enumerate(nullptr, 0, &count, nullptr)) || count == 0 || count > 1024) return false;
    std::vector<XrExtensionProperties> properties(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    if (XR_FAILED(enumerate(nullptr, count, &count, properties.data()))) return false;
    for (uint32_t i = 0; i < count && i < properties.size(); ++i)
        if (std::strcmp(properties[i].extensionName, extension) == 0) return true;
    return false;
}

// Création de l'instance avec notre extension en plus ; si elle échoue, quelle qu'en soit la raison
// (un runtime ou une layer peut refuser l'extension avec un autre code), nouvel essai avec la demande
// d'origine du jeu : la layer ne doit jamais être la cause d'un échec de démarrage.
XrResult createWithFallback(PFN_xrCreateApiLayerInstance create, const XrInstanceCreateInfo* original, const XrInstanceCreateInfo* extended,
                            const XrApiLayerCreateInfo* layerInfo, XrInstance* instance, bool& batteryExtension) {
    const XrResult result = create(extended, layerInfo, instance);
    if (extended == original || XR_SUCCEEDED(result)) return result;
    const XrResult retry = create(original, layerInfo, instance);
    if (XR_SUCCEEDED(retry)) {
        batteryExtension = false;
        Log("Extension de batterie refusee a la creation de l'instance (%d) : instance creee sans elle.", static_cast<int>(result));
    } else {
        // Refus sans rapport avec notre extension (version d'OpenXR, extension du jeu absente, runtime
        // pas encore prêt...) : beaucoup de jeux essaient ainsi plusieurs demandes à la suite.
        Log("Creation de l'instance refusee (%d, et %d sans l'extension de batterie) : erreur rendue telle quelle au jeu.", static_cast<int>(result),
            static_cast<int>(retry));
    }
    return retry;
}

// ---------------------------------------------------------------------------------------------
// Fonctions interceptées
// ---------------------------------------------------------------------------------------------

XrResult XRAPI_CALL hook_xrDestroyInstance(XrInstance instance) {
    PFN_xrDestroyInstance next = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        next = g_next.DestroyInstance;
        if (g_overlay && g_overlay->instance() == instance) g_overlay.reset();
        if (g_hands && g_hands->instance() == instance) g_hands.reset();
    }
    return next ? next(instance) : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL hook_xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
    const XrResult result = g_next.CreateSession(instance, createInfo, session);
    if (XR_FAILED(result) || !g_active || !session) return result;
    try {
        auto overlay = std::make_shared<Overlay>(g_next, LoadConfig(g_module), instance, hands());
        overlay->onSessionCreated(*session, createInfo);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_overlay = std::move(overlay);
    } catch (...) {
        Log("Exception a la creation de la session : overlay inactif.");
    }
    return result;
}

XrResult XRAPI_CALL hook_xrDestroySession(XrSession session) {
    std::shared_ptr<Overlay> overlay;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_overlay && g_overlay->session() == session) {
            overlay = std::move(g_overlay);
            g_overlay.reset();
        }
    }
    if (overlay) {
        try {
            overlay->onSessionDestroying();
        } catch (...) {
        }
        overlay.reset();
    }
    if (auto h = hands()) h->sessionDestroying(session);
    return g_next.DestroySession(session);
}

// Manettes : la layer ajoute sa propre action (position des mains) à celles du jeu, voir hand_input.h.
// Les méthodes de HandInput ne laissent passer aucune exception une fois l'appel du jeu transmis :
// le repli ci-dessous ne peut donc jamais rejouer un appel déjà fait.
XrResult XRAPI_CALL hook_xrSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding* suggestedBindings) {
    if (auto h = hands()) {
        try {
            return h->suggestBindings(g_next.SuggestInteractionProfileBindings, instance, suggestedBindings);
        } catch (...) {
        }
    }
    return g_next.SuggestInteractionProfileBindings(instance, suggestedBindings);
}

XrResult XRAPI_CALL hook_xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo* attachInfo) {
    if (auto h = hands()) {
        try {
            return h->attach(g_next.AttachSessionActionSets, session, attachInfo);
        } catch (...) {
        }
    }
    return g_next.AttachSessionActionSets(session, attachInfo);
}

XrResult XRAPI_CALL hook_xrSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo) {
    if (auto h = hands()) {
        try {
            return h->sync(g_next.SyncActions, session, syncInfo);
        } catch (...) {
        }
    }
    return g_next.SyncActions(session, syncInfo);
}

XrResult XRAPI_CALL hook_xrWaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
    const XrResult result = g_next.WaitFrame(session, frameWaitInfo, frameState);
    if (XR_SUCCEEDED(result) && g_active && frameState) {
        try {
            if (auto overlay = overlayFor(session)) overlay->onWaitFrame(*frameState);
        } catch (...) {
        }
    }
    return result;
}

XrResult XRAPI_CALL hook_xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
    const XrResult result = g_next.BeginFrame(session, frameBeginInfo);
    if (XR_SUCCEEDED(result) && g_active) {
        try {
            if (auto overlay = overlayFor(session)) overlay->onBeginFrame();
        } catch (...) {
        }
    }
    return result;
}

XrResult XRAPI_CALL hook_xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    if (!g_active || !frameEndInfo) return g_next.EndFrame(session, frameEndInfo);

    // Image sans aucun calque (chargement, jeu masqué) : elle est comptée dans les mesures mais on n'y
    // ajoute rien, le runtime doit pouvoir afficher son propre écran d'attente.
    const bool allowLayer = frameEndInfo->layerCount > 0 && frameEndInfo->layerCount < kMaxLayers && frameEndInfo->layers != nullptr;

    std::shared_ptr<Overlay> overlay;
    XrCompositionLayerQuad quads[Overlay::kMaxQuads] = {};
    int added = 0;
    try {
        overlay = overlayFor(session);
        if (overlay) added = overlay->onEndFrame(quads, frameEndInfo->displayTime, allowLayer);
    } catch (...) {
        added = 0;
    }
    if (added <= 0 || frameEndInfo->layerCount + static_cast<uint32_t>(added) > kMaxLayers) {
        const XrResult result = g_next.EndFrame(session, frameEndInfo);
        if (overlay && XR_SUCCEEDED(result)) overlay->onFrameSubmitted(*frameEndInfo);
        return result;
    }

    // Copie de la liste du jeu + nos calques en dernier (donc dessinés par-dessus).
    const XrCompositionLayerBaseHeader* layers[kMaxLayers];
    for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) layers[i] = frameEndInfo->layers[i];
    for (int i = 0; i < added; ++i) layers[frameEndInfo->layerCount + i] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[i]);
    XrFrameEndInfo patched = *frameEndInfo;
    patched.layerCount = frameEndInfo->layerCount + static_cast<uint32_t>(added);
    patched.layers = layers;

    const XrResult result = g_next.EndFrame(session, &patched);
    if (XR_SUCCEEDED(result)) {
        overlay->onLayerAccepted();
        overlay->onFrameSubmitted(*frameEndInfo);
        return result;
    }

    // Ces erreurs ne dépendent pas de notre calque (session arrêtée, ordre des appels, temps invalide...) :
    // le jeu aurait eu la même sans nous, on la lui rend telle quelle.
    if (result == XR_ERROR_SESSION_NOT_RUNNING || result == XR_ERROR_CALL_ORDER_INVALID || result == XR_ERROR_SESSION_LOST ||
        result == XR_ERROR_INSTANCE_LOST || result == XR_ERROR_TIME_INVALID || result == XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED) {
        return result;
    }

    // Sinon notre calque est peut-être en cause : on soumet à nouveau l'image exactement comme le jeu
    // l'a fournie, pour ne jamais être la raison d'une image perdue. Au bout de quelques refus,
    // l'overlay se désactive pour la session.
    overlay->onLayerRejected(result);
    const XrResult retry = g_next.EndFrame(session, frameEndInfo);
    if (XR_SUCCEEDED(retry)) overlay->onFrameSubmitted(*frameEndInfo);
    // Si le runtime avait déjà clos l'image lors du premier appel, le jeu reçoit l'erreur d'origine.
    return retry == XR_ERROR_CALL_ORDER_INVALID ? result : retry;
}

// ---------------------------------------------------------------------------------------------
// Maillon de sortie : placé juste avant le runtime, il compte ce que celui-ci reçoit
// ---------------------------------------------------------------------------------------------
//
// Ces trois fonctions ne modifient rien : elles transmettent l'appel puis mettent à jour des
// compteurs atomiques. Elles peuvent être appelées depuis n'importe quel fil d'exécution (un outil
// de génération d'images soumet souvent ses images depuis le sien).

struct OutputFunctions {
    PFN_xrWaitFrame WaitFrame = nullptr;
    PFN_xrEndFrame EndFrame = nullptr;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
};
OutputFunctions g_output;
PFN_xrGetInstanceProcAddr g_outputGetInstanceProcAddr = nullptr;  // xrGetInstanceProcAddr du runtime

uint64_t swapchainKey(XrSwapchain swapchain) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(swapchain));
}

XrResult XRAPI_CALL output_xrWaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
    const XrResult result = g_output.WaitFrame(session, frameWaitInfo, frameState);
    // Durée d'affichage annoncée par le runtime lui-même, avant qu'une autre layer ne la modifie.
    // On retient la plus courte (bornée à 250 Hz) : c'est celle du casque.
    if (XR_SUCCEEDED(result) && frameState && frameState->predictedDisplayPeriod >= 4000000) {
        const int64_t period = frameState->predictedDisplayPeriod;
        const int64_t known = g_outputTap.periodNs.load(std::memory_order_relaxed);
        if (known == 0 || period < known) g_outputTap.periodNs.store(period, std::memory_order_relaxed);
    }
    return result;
}

XrResult XRAPI_CALL output_xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* releaseInfo) {
    const XrResult result = g_output.ReleaseSwapchainImage(swapchain, releaseInfo);
    // Une image de cette swapchain vient d'être rendue au runtime : sa prochaine soumission montrera
    // un contenu nouveau. (La texture de notre propre panneau n'est pas une image du jeu.)
    if (XR_SUCCEEDED(result) && !t_ownSwapchainCall) g_outputTap.swapchains.onRelease(swapchainKey(swapchain));
    return result;
}

XrResult XRAPI_CALL output_xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    const XrResult result = g_output.EndFrame(session, frameEndInfo);
    if (XR_FAILED(result) || !frameEndInfo || frameEndInfo->layerCount == 0 || !frameEndInfo->layers) return result;
    g_outputTap.submitted.fetch_add(1, std::memory_order_relaxed);

    // L'image compte comme nouvelle si l'une des swapchains de la scène a reçu une image depuis sa
    // dernière soumission ; resoumise à l'identique (répétition), elle n'est pas une image de plus
    // pour l'œil. Les panneaux 2D (quads) ne décident que s'il n'y a pas de scène 3D.
    // Lecture faite après l'appel réussi : le runtime vient de valider ces structures.
    bool hasProjection = false, projectionFresh = false, quadFresh = false;
    for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
        const XrCompositionLayerBaseHeader* layer = frameEndInfo->layers[i];
        if (!layer) continue;
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            hasProjection = true;
            for (uint32_t v = 0; projection->views && v < projection->viewCount && v < 8; ++v)
                projectionFresh |= g_outputTap.swapchains.consume(swapchainKey(projection->views[v].subImage.swapchain));
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            quadFresh |= g_outputTap.swapchains.consume(swapchainKey(reinterpret_cast<const XrCompositionLayerQuad*>(layer)->subImage.swapchain));
        }
    }
    if (hasProjection ? projectionFresh : quadFresh) g_outputTap.fresh.fetch_add(1, std::memory_order_relaxed);
    return result;
}

XrResult XRAPI_CALL output_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    const PFN_xrGetInstanceProcAddr next = g_outputGetInstanceProcAddr;
    if (!next) {
        if (function) *function = nullptr;
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
    const XrResult result = next(instance, name, function);
    if (XR_FAILED(result) || !name || !function || !*function) return result;
    if (g_output.WaitFrame && std::strcmp(name, "xrWaitFrame") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(output_xrWaitFrame);
    else if (g_output.EndFrame && std::strcmp(name, "xrEndFrame") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(output_xrEndFrame);
    else if (g_output.ReleaseSwapchainImage && std::strcmp(name, "xrReleaseSwapchainImage") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(output_xrReleaseSwapchainImage);
    return result;
}

// Appelé par la dernière layer de la chaîne, à la place de la fonction de création du runtime.
XrResult XRAPI_CALL output_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* apiLayerInfo,
                                                     XrInstance* instance) {
    if (!apiLayerInfo || !apiLayerInfo->nextInfo || std::strcmp(apiLayerInfo->nextInfo->layerName, kOutputLayerName) != 0 ||
        !apiLayerInfo->nextInfo->nextGetInstanceProcAddr || !apiLayerInfo->nextInfo->nextCreateApiLayerInstance) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    XrApiLayerCreateInfo chained = *apiLayerInfo;
    chained.nextInfo = apiLayerInfo->nextInfo->next;
    g_outputTap.reached.store(true, std::memory_order_relaxed);
    const XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(info, &chained, instance);
    if (XR_FAILED(result) || !instance) return result;

    const PFN_xrGetInstanceProcAddr getProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
    OutputFunctions output;
    resolve(getProcAddr, *instance, "xrWaitFrame", output.WaitFrame);
    resolve(getProcAddr, *instance, "xrEndFrame", output.EndFrame);
    resolve(getProcAddr, *instance, "xrReleaseSwapchainImage", output.ReleaseSwapchainImage);
    if (output.WaitFrame && output.EndFrame && output.ReleaseSwapchainImage) {
        g_output = output;
        g_outputTap.installed.store(true, std::memory_order_relaxed);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// Chaînage avec le chargeur OpenXR
// ---------------------------------------------------------------------------------------------

XrResult XRAPI_CALL layer_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
    *function = nullptr;
    if (!g_next.GetInstanceProcAddr) return XR_ERROR_FUNCTION_UNSUPPORTED;  // pas encore d'instance

    const XrResult result = g_next.GetInstanceProcAddr(instance, name, function);
    if (XR_FAILED(result) || !*function || !g_active) return result;
    std::shared_ptr<HandInput> handInput = hands();
    const bool handHooks = handInput && handInput->tracking();

    // On ne remplace que des fonctions que le maillon suivant fournit réellement.
    struct Hook {
        const char* name;
        PFN_xrVoidFunction function;
        bool available;
    };
    const Hook hooks[] = {
        {"xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction>(hook_xrDestroyInstance), g_next.DestroyInstance != nullptr},
        {"xrCreateSession", reinterpret_cast<PFN_xrVoidFunction>(hook_xrCreateSession), g_next.CreateSession != nullptr},
        {"xrDestroySession", reinterpret_cast<PFN_xrVoidFunction>(hook_xrDestroySession), g_next.DestroySession != nullptr},
        {"xrWaitFrame", reinterpret_cast<PFN_xrVoidFunction>(hook_xrWaitFrame), g_next.WaitFrame != nullptr},
        {"xrBeginFrame", reinterpret_cast<PFN_xrVoidFunction>(hook_xrBeginFrame), g_next.BeginFrame != nullptr},
        {"xrEndFrame", reinterpret_cast<PFN_xrVoidFunction>(hook_xrEndFrame), g_next.EndFrame != nullptr},
        // Seulement si le suivi des manettes est prévu (réglages lus au lancement du jeu).
        {"xrSuggestInteractionProfileBindings", reinterpret_cast<PFN_xrVoidFunction>(hook_xrSuggestInteractionProfileBindings),
         g_next.SuggestInteractionProfileBindings != nullptr && handHooks},
        {"xrAttachSessionActionSets", reinterpret_cast<PFN_xrVoidFunction>(hook_xrAttachSessionActionSets),
         g_next.AttachSessionActionSets != nullptr && handHooks},
        {"xrSyncActions", reinterpret_cast<PFN_xrVoidFunction>(hook_xrSyncActions), g_next.SyncActions != nullptr && handHooks},
    };
    for (const Hook& hook : hooks) {
        if (hook.available && std::strcmp(name, hook.name) == 0) {
            *function = hook.function;
            break;
        }
    }
    return result;
}

XrResult XRAPI_CALL layer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* apiLayerInfo,
                                                    XrInstance* instance) {
    if (!apiLayerInfo || apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
        apiLayerInfo->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION || apiLayerInfo->structSize != sizeof(XrApiLayerCreateInfo) ||
        !apiLayerInfo->nextInfo || apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
        apiLayerInfo->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
        apiLayerInfo->nextInfo->structSize != sizeof(XrApiLayerNextInfo) || std::strcmp(apiLayerInfo->nextInfo->layerName, kLayerName) != 0 ||
        !apiLayerInfo->nextInfo->nextGetInstanceProcAddr || !apiLayerInfo->nextInfo->nextCreateApiLayerInstance) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    // Réglages et programme courant : lus avant de créer l'instance, car ils décident de la chaîne.
    Config config;
    std::wstring exe;
    bool excluded = false;
    try {
        config = LoadConfig(g_module);
        exe = CurrentExeName();
        for (const std::wstring& name : config.exclude) excluded = excluded || name == exe;
    } catch (...) {
        excluded = true;
    }

    // On passe la main au maillon suivant en retirant notre entrée de la chaîne.
    XrApiLayerCreateInfo chained = *apiLayerInfo;
    chained.nextInfo = apiLayerInfo->nextInfo->next;

    // Batterie des manettes (montre) : l'extension n'est demandée que si le runtime la propose et que
    // le jeu ne l'a pas déjà demandée. La liste du jeu est recopiée, jamais modifiée.
    const XrInstanceCreateInfo* createInfo = info;
    XrInstanceCreateInfo withBattery = {};
    std::vector<const char*> extensions;
    bool batteryExtension = false;
    try {
        if (!excluded && info && WantsBattery(config)) {
            bool requested = false;
            for (uint32_t i = 0; info->enabledExtensionNames && i < info->enabledExtensionCount; ++i)
                requested = requested || std::strcmp(info->enabledExtensionNames[i], XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME) == 0;
            if (requested) {
                batteryExtension = true;
            } else if (RuntimeOffersExtension(apiLayerInfo->nextInfo, XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME)) {
                extensions.assign(info->enabledExtensionNames, info->enabledExtensionNames + info->enabledExtensionCount);
                extensions.push_back(XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME);
                withBattery = *info;
                withBattery.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                withBattery.enabledExtensionNames = extensions.data();
                createInfo = &withBattery;
                batteryExtension = true;
            }
        }
    } catch (...) {
        createInfo = info;
        batteryExtension = false;
    }

    // S'il y a d'autres layers sous la nôtre, on recopie leur liste en y ajoutant, tout au bout, notre
    // maillon de sortie : la dernière layer l'appellera à la place du runtime, et lui appellera le runtime.
    // La liste d'origine, qui appartient au chargeur, n'est pas modifiée.
    XrApiLayerNextInfo nodes[kMaxChain + 1];
    size_t below = 0;
    bool spliceable = !excluded && config.measureOutput != 0;
    bool framegenBelow = false;
    char belowNames[256] = "";
    for (const XrApiLayerNextInfo* node = chained.nextInfo; node; node = node->next) {
        if (below >= kMaxChain || node->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            node->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION || node->structSize != sizeof(XrApiLayerNextInfo)) {
            spliceable = false;  // format inattendu : on ne touche à rien
            break;
        }
        nodes[below++] = *node;
        framegenBelow = framegenBelow || isFrameGenerationLayer(node->layerName);
        if (belowNames[0]) appendText(belowNames, sizeof(belowNames), ", ");
        appendText(belowNames, sizeof(belowNames), node->layerName);
    }
    // En mode automatique, le maillon n'est ajouté que si un outil de génération d'images est présent :
    // sans lui, il n'y aurait rien de plus à mesurer.
    if (config.measureOutput < 0 && !framegenBelow) spliceable = false;
    g_outputTap.installed.store(false, std::memory_order_relaxed);
    g_outputTap.reached.store(false, std::memory_order_relaxed);

    XrResult result = XR_ERROR_INITIALIZATION_FAILED;
    if (spliceable && below > 0) {
        for (size_t i = 0; i + 1 < below; ++i) nodes[i].next = &nodes[i + 1];
        XrApiLayerNextInfo& last = nodes[below - 1];
        XrApiLayerNextInfo& tap = nodes[below];
        tap = last;  // mêmes champs de structure, mêmes fonctions suivantes (celles du runtime)
        std::memset(tap.layerName, 0, sizeof(tap.layerName));
        appendText(tap.layerName, sizeof(tap.layerName), kOutputLayerName);
        tap.next = nullptr;
        g_outputGetInstanceProcAddr = last.nextGetInstanceProcAddr;
        g_outputTap.periodNs.store(0, std::memory_order_relaxed);
        last.nextGetInstanceProcAddr = output_xrGetInstanceProcAddr;
        last.nextCreateApiLayerInstance = output_xrCreateApiLayerInstance;
        last.next = &tap;

        XrApiLayerCreateInfo extended = chained;
        extended.nextInfo = &nodes[0];
        result = createWithFallback(apiLayerInfo->nextInfo->nextCreateApiLayerInstance, info, createInfo, &extended, instance, batteryExtension);
        if (XR_SUCCEEDED(result)) {
            // D'autres layers détiennent maintenant des pointeurs vers nos fonctions : la DLL doit rester
            // chargée jusqu'à la fin du programme, même après la destruction de l'instance.
            PinModule(g_module);
        } else if (g_outputTap.reached.load(std::memory_order_relaxed)) {
            // Notre maillon a bien été atteint : l'échec vient du runtime ou d'une layer après coup, il
            // se serait produit sans nous. On le rend tel quel, sans rien rejouer.
            g_outputTap.installed.store(false, std::memory_order_relaxed);
        } else {
            // Une layer a refusé la chaîne avant d'arriver à notre maillon : nouvel essai avec la chaîne
            // d'origine, pour ne jamais être la raison d'un échec de démarrage.
            g_outputTap.installed.store(false, std::memory_order_relaxed);
            Log("Creation d'instance refusee avec le maillon de sortie (%d) : nouvel essai sans lui.", static_cast<int>(result));
            result = createWithFallback(apiLayerInfo->nextInfo->nextCreateApiLayerInstance, info, createInfo, &chained, instance, batteryExtension);
        }
    } else {
        result = createWithFallback(apiLayerInfo->nextInfo->nextCreateApiLayerInstance, info, createInfo, &chained, instance, batteryExtension);
    }
    if (XR_FAILED(result) || !instance) return result;
    if (below > 0)
        Log("Layers sous la notre : %s%s.", belowNames,
            g_outputTap.installed.load(std::memory_order_relaxed) ? " ; mesure en sortie de chaine active" : "");

    // La table est préparée à part puis copiée d'un coup : si une autre instance est déjà en service,
    // ses appels ne voient jamais de pointeur nul.
    const PFN_xrGetInstanceProcAddr getProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
    NextFunctions next;
    next.GetInstanceProcAddr = getProcAddr;
    resolve(getProcAddr, *instance, "xrDestroyInstance", next.DestroyInstance);
    resolve(getProcAddr, *instance, "xrCreateSession", next.CreateSession);
    resolve(getProcAddr, *instance, "xrDestroySession", next.DestroySession);
    resolve(getProcAddr, *instance, "xrWaitFrame", next.WaitFrame);
    resolve(getProcAddr, *instance, "xrBeginFrame", next.BeginFrame);
    resolve(getProcAddr, *instance, "xrEndFrame", next.EndFrame);
    resolve(getProcAddr, *instance, "xrCreateReferenceSpace", next.CreateReferenceSpace);
    resolve(getProcAddr, *instance, "xrDestroySpace", next.DestroySpace);
    resolve(getProcAddr, *instance, "xrEnumerateSwapchainFormats", next.EnumerateSwapchainFormats);
    resolve(getProcAddr, *instance, "xrCreateSwapchain", next.CreateSwapchain);
    resolve(getProcAddr, *instance, "xrDestroySwapchain", next.DestroySwapchain);
    resolve(getProcAddr, *instance, "xrEnumerateSwapchainImages", next.EnumerateSwapchainImages);
    resolve(getProcAddr, *instance, "xrAcquireSwapchainImage", next.AcquireSwapchainImage);
    resolve(getProcAddr, *instance, "xrWaitSwapchainImage", next.WaitSwapchainImage);
    resolve(getProcAddr, *instance, "xrReleaseSwapchainImage", next.ReleaseSwapchainImage);
    resolve(getProcAddr, *instance, "xrGetSystemProperties", next.GetSystemProperties);
    resolve(getProcAddr, *instance, "xrGetInstanceProperties", next.GetInstanceProperties);
    resolve(getProcAddr, *instance, "xrEnumerateViewConfigurationViews", next.EnumerateViewConfigurationViews);
    resolve(getProcAddr, *instance, "xrLocateSpace", next.LocateSpace);
    resolve(getProcAddr, *instance, "xrSuggestInteractionProfileBindings", next.SuggestInteractionProfileBindings);
    resolve(getProcAddr, *instance, "xrAttachSessionActionSets", next.AttachSessionActionSets);
    resolve(getProcAddr, *instance, "xrSyncActions", next.SyncActions);

    // Manettes et batterie : seulement si la montre ou l'accroche du panneau le demandent.
    std::shared_ptr<HandInput> handInput;
    if (!excluded && (WantsHandTracking(config) || batteryExtension)) {
        try {
            const bool track = WantsHandTracking(config) && next.LocateSpace && next.SuggestInteractionProfileBindings &&
                               next.AttachSessionActionSets && next.SyncActions;
            handInput = std::make_shared<HandInput>(*instance, getProcAddr, track, batteryExtension);
            Log("Manettes : %s.", handInput->describe().c_str());
        } catch (...) {
            handInput.reset();
        }
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_next = next;
    g_hands = std::move(handInput);

    // Décide si l'overlay s'active dans ce processus.
    g_active = false;
    try {
        const bool hooksUsable = g_next.DestroyInstance && g_next.CreateSession && g_next.DestroySession && g_next.WaitFrame &&
                                 g_next.BeginFrame && g_next.EndFrame && g_next.overlayUsable();
        g_active = !excluded && hooksUsable;
        const char* application = info ? info->applicationInfo.applicationName : "?";
        Log("Instance OpenXR creee pour \"%s\" (%s) : overlay %s (XR Perf Overlay %s).", application, Narrow(exe).c_str(),
            excluded ? "desactive (processus exclu dans le .ini)" : (hooksUsable ? "pret" : "desactive (fonctions OpenXR manquantes)"), XRPO_VERSION);
    } catch (...) {
        g_active = false;
    }
    return result;
}

}  // namespace

// Point d'entrée appelé par le chargeur OpenXR (nom exporté via exports.def).
extern "C" XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo, const char* layerName,
                                                                   XrNegotiateApiLayerRequest* apiLayerRequest) {
    if (!loaderInfo || !layerName || !apiLayerRequest || std::strcmp(layerName, kLayerName) != 0 ||
        loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO || loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
        apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest) ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        XR_VERSION_MAJOR(loaderInfo->minApiVersion) > XR_VERSION_MAJOR(XR_CURRENT_API_VERSION) ||
        XR_VERSION_MAJOR(loaderInfo->maxApiVersion) < XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr = layer_xrGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = layer_xrCreateApiLayerInstance;
    return XR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
    } else if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
        // Le processus se termine sans avoir détruit sa session : les autres fils d'exécution ont déjà
        // été arrêtés, peut-être au milieu de notre code. On ne libère donc rien (ni verrou, ni objet
        // Direct3D) : l'overlay est volontairement abandonné, Windows récupère tout.
        g_active = false;
        new std::shared_ptr<Overlay>(std::move(g_overlay));
    }
    return TRUE;
}
