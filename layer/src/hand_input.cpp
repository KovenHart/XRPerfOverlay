#include "hand_input.h"

#include <cstdio>
#include <cstring>

#include "log.h"

namespace xrpo {

namespace {

template <typename T>
void resolve(PFN_xrGetInstanceProcAddr getProcAddr, XrInstance instance, const char* name, T& function) {
    PFN_xrVoidFunction raw = nullptr;
    function = getProcAddr && XR_SUCCEEDED(getProcAddr(instance, name, &raw)) ? reinterpret_cast<T>(raw) : nullptr;
}

const char* const kHandNames[2] = {"/user/hand/left", "/user/hand/right"};
const char* const kPoseNames[2] = {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"};

}  // namespace

HandInput::HandInput(XrInstance instance, PFN_xrGetInstanceProcAddr getProcAddr, bool trackHands, bool battery)
    : m_instance(instance), m_trackHands(trackHands), m_batteryExtension(battery) {
    resolve(getProcAddr, instance, "xrStringToPath", m_stringToPath);
    resolve(getProcAddr, instance, "xrPathToString", m_pathToString);
    resolve(getProcAddr, instance, "xrCreateActionSet", m_createActionSet);
    resolve(getProcAddr, instance, "xrCreateAction", m_createAction);
    resolve(getProcAddr, instance, "xrCreateActionSpace", m_createActionSpace);
    resolve(getProcAddr, instance, "xrGetCurrentInteractionProfile", m_getCurrentInteractionProfile);
    if (!m_stringToPath || !m_createActionSet || !m_createAction || !m_createActionSpace) m_trackHands = false;
    if (m_stringToPath)
        for (int hand = 0; hand < 2; ++hand)
            if (XR_FAILED(m_stringToPath(instance, kHandNames[hand], &m_handPaths[hand]))) m_handPaths[hand] = XR_NULL_PATH;
}

// Groupe d'actions de la layer : une seule action, la position de la poignée de chaque main.
bool HandInput::createActions() {
    if (m_actionsTried) return m_actionSet != XR_NULL_HANDLE;
    m_actionsTried = true;
    for (int hand = 0; hand < 2; ++hand) {
        if (m_handPaths[hand] == XR_NULL_PATH || XR_FAILED(m_stringToPath(m_instance, kPoseNames[hand], &m_posePaths[hand]))) {
            m_trackHands = false;
            return false;
        }
    }
    XrActionSetCreateInfo setInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(setInfo.actionSetName, sizeof(setInfo.actionSetName), "xrperfoverlay");
    std::snprintf(setInfo.localizedActionSetName, sizeof(setInfo.localizedActionSetName), "XR Perf Overlay");
    setInfo.priority = 0;
    if (XR_FAILED(m_createActionSet(m_instance, &setInfo, &m_actionSet))) {
        m_actionSet = XR_NULL_HANDLE;
        m_trackHands = false;
        Log("Suivi des manettes : creation du groupe d'actions refusee par le runtime.");
        return false;
    }
    XrActionCreateInfo actionInfo = {XR_TYPE_ACTION_CREATE_INFO};
    std::snprintf(actionInfo.actionName, sizeof(actionInfo.actionName), "hand_pose");
    std::snprintf(actionInfo.localizedActionName, sizeof(actionInfo.localizedActionName), "Position de la main (overlay)");
    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    actionInfo.countSubactionPaths = 2;
    actionInfo.subactionPaths = m_handPaths;
    if (XR_FAILED(m_createAction(m_actionSet, &actionInfo, &m_poseAction))) {
        m_poseAction = XR_NULL_HANDLE;
        m_trackHands = false;
        Log("Suivi des manettes : creation de l'action refusee par le runtime.");
        return false;
    }
    return true;
}

bool HandInput::profileAccepted(XrPath profile) {
    for (XrPath known : m_profiles)
        if (known == profile) return true;
    return false;
}

XrResult HandInput::suggestBindings(PFN_xrSuggestInteractionProfileBindings next, XrInstance instance,
                                    const XrInteractionProfileSuggestedBinding* suggested) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Rien d'inattendu n'est modifié : autre instance, structure invalide, liste démesurée, ou trop tard
    // (le groupe d'actions est déjà attaché à une session, plus aucune liaison n'est acceptée).
    // Une liste vide est une erreur du jeu : elle doit lui revenir telle quelle, pas devenir valide
    // grâce à notre ajout.
    if (!m_trackHands || instance != m_instance || !suggested || suggested->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING ||
        suggested->countSuggestedBindings == 0 || suggested->countSuggestedBindings > 4096 || !suggested->suggestedBindings ||
        m_attachedOnce || !createActions())
        return next(instance, suggested);

    std::vector<XrActionSuggestedBinding> bindings(suggested->suggestedBindings, suggested->suggestedBindings + suggested->countSuggestedBindings);
    for (int hand = 0; hand < 2; ++hand) bindings.push_back({m_poseAction, m_posePaths[hand]});
    XrInteractionProfileSuggestedBinding extended = *suggested;  // même chaîne `next`
    extended.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    extended.suggestedBindings = bindings.data();
    // À partir d'ici, plus aucune exception ne sort : l'appel du jeu ne doit jamais être rejoué.
    const XrResult result = next(instance, &extended);
    if (XR_SUCCEEDED(result)) {
        try {
            if (!profileAccepted(suggested->interactionProfile)) m_profiles.push_back(suggested->interactionProfile);
        } catch (...) {
        }
        return result;
    }
    // Refusé avec notre ajout (profil sans poignée, comme une manette de jeu) : l'appel du jeu, tel quel.
    const XrResult original = next(instance, suggested);
    if (XR_SUCCEEDED(original)) {
        try {
            bool known = false;
            for (XrPath refused : m_refusedProfiles) known = known || refused == suggested->interactionProfile;
            if (!known) {
                m_refusedProfiles.push_back(suggested->interactionProfile);
                char name[XR_MAX_PATH_LENGTH] = "?";
                uint32_t length = 0;
                if (m_pathToString) m_pathToString(m_instance, suggested->interactionProfile, sizeof(name), &length, name);
                Log("Suivi des manettes : profil %s sans position de main (%d), laisse tel quel.", name, static_cast<int>(result));
            }
        } catch (...) {
        }
    }
    return original;
}

XrResult HandInput::attach(PFN_xrAttachSessionActionSets next, XrSession session, const XrSessionActionSetsAttachInfo* info) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_trackHands || m_actionSet == XR_NULL_HANDLE || m_profiles.empty() || m_session != XR_NULL_HANDLE || !info ||
        info->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO || info->countActionSets == 0 || info->countActionSets > 1024 || !info->actionSets)
        return next(session, info);

    std::vector<XrActionSet> sets(info->actionSets, info->actionSets + info->countActionSets);
    sets.push_back(m_actionSet);
    XrSessionActionSetsAttachInfo extended = *info;
    extended.countActionSets = static_cast<uint32_t>(sets.size());
    extended.actionSets = sets.data();
    // À partir d'ici, plus aucune exception ne sort : l'appel du jeu ne doit jamais être rejoué.
    const XrResult result = next(session, &extended);
    if (XR_FAILED(result)) {
        const XrResult original = next(session, info);
        // Abandon seulement si c'est bien notre ajout qui gênait (l'appel du jeu seul réussit) ; une
        // erreur du jeu lui-même lui est rendue, et il pourra réessayer avec notre groupe.
        if (XR_SUCCEEDED(original)) {
            m_trackHands = false;
            try {
                Log("Suivi des manettes : ajout du groupe d'actions refuse (%d) ; abandonne, le jeu n'est pas touche.", static_cast<int>(result));
            } catch (...) {
            }
        }
        return original;
    }
    m_session = session;
    m_attachedOnce = true;
    m_syncFailed = false;
    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo spaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = m_poseAction;
        spaceInfo.subactionPath = m_handPaths[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.f;
        if (XR_FAILED(m_createActionSpace(session, &spaceInfo, &m_spaces[hand]))) m_spaces[hand] = XR_NULL_HANDLE;
    }
    try {
        Log("Suivi des manettes actif (%u profil(s) de manette, espaces %s / %s).", static_cast<unsigned>(m_profiles.size()),
            m_spaces[0] ? "gauche" : "-", m_spaces[1] ? "droite" : "-");
    } catch (...) {
    }
    return result;
}

XrResult HandInput::sync(PFN_xrSyncActions next, XrSession session, const XrActionsSyncInfo* info) {
    XrActionSet ours = XR_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_trackHands && !m_syncFailed && session == m_session) ours = m_actionSet;
    }
    if (ours == XR_NULL_HANDLE || !info || info->type != XR_TYPE_ACTIONS_SYNC_INFO || info->countActiveActionSets > 1024 ||
        (info->countActiveActionSets > 0 && !info->activeActionSets))
        return next(session, info);

    XrActiveActionSet local[16];
    std::vector<XrActiveActionSet> heap;
    XrActiveActionSet* active = local;
    const uint32_t count = info->countActiveActionSets + 1;
    if (count > 16) {
        heap.resize(count);
        active = heap.data();
    }
    for (uint32_t i = 0; i < info->countActiveActionSets; ++i) active[i] = info->activeActionSets[i];
    active[count - 1] = {ours, XR_NULL_PATH};
    XrActionsSyncInfo extended = *info;
    extended.countActiveActionSets = count;
    extended.activeActionSets = active;
    // À partir d'ici, plus aucune exception ne sort : l'appel du jeu ne doit jamais être rejoué.
    const XrResult result = next(session, &extended);
    if (result == XR_ERROR_ACTIONSET_NOT_ATTACHED || result == XR_ERROR_PATH_UNSUPPORTED || result == XR_ERROR_PATH_INVALID ||
        result == XR_ERROR_VALIDATION_FAILURE || result == XR_ERROR_HANDLE_INVALID) {
        // Peut-être à cause de nous : on arrête d'ajouter notre groupe et on refait l'appel du jeu. Si
        // l'appel du jeu seul échoue de la même façon, l'erreur vient de lui ; on cesse quand même
        // l'ajout, pour ne pas doubler chacun de ses appels.
        const XrResult original = next(session, info);
        if (XR_SUCCEEDED(original) || original == result) {
            try {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_syncFailed)
                    Log("Suivi des manettes : mise a jour refusee avec notre groupe (%d, seul : %d) ; abandonne.", static_cast<int>(result),
                        static_cast<int>(original));
                m_syncFailed = true;
            } catch (...) {
            }
        }
        return original;
    }
    return result;
}

void HandInput::sessionDestroying(XrSession session) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // La batterie lue appartenait à cette session (y compris quand seule la batterie est utilisée).
    for (int hand = 0; hand < 2; ++hand) {
        m_batteryRead[hand] = -1.0;
        m_batteryLevel[hand] = -1.f;
    }
    if (session != m_session) return;
    // Les espaces d'action disparaissent avec la session. Le groupe d'actions, lui, appartient à
    // l'instance : si le jeu ouvre une nouvelle session, il sera de nouveau ajouté aux siens.
    m_spaces[0] = m_spaces[1] = XR_NULL_HANDLE;
    m_session = XR_NULL_HANDLE;
}

XrSpace HandInput::handSpace(XrSession session, int hand) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_trackHands || m_syncFailed || session != m_session || hand < 0 || hand > 1) return XR_NULL_HANDLE;
    return m_spaces[hand];
}

bool HandInput::battery(XrSession session, int hand, double now, float& level, bool& charging) {
    if (!m_batteryExtension || !m_getCurrentInteractionProfile || hand < 0 || hand > 1) return false;
    XrPath handPath = XR_NULL_PATH;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_handPaths[hand] == XR_NULL_PATH) return false;
        if (m_batteryRead[hand] >= 0.0 && now - m_batteryRead[hand] < 2.0) {
            if (m_batteryLevel[hand] < 0.f) return false;
            level = m_batteryLevel[hand];
            charging = m_batteryCharging[hand];
            return true;
        }
        m_batteryRead[hand] = now;
        handPath = m_handPaths[hand];
    }
    // Appel au runtime hors du verrou : il ne doit pas retarder la mise à jour des actions du jeu.
    XrBatteryStateDisplayEXT state = {XR_TYPE_BATTERY_STATE_DISPLAY_EXT};
    XrInteractionProfileState profile = {XR_TYPE_INTERACTION_PROFILE_STATE};
    profile.next = &state;
    float newLevel = -1.f;
    bool newCharging = false;
    // Échoue tant que le jeu n'a pas attaché ses actions : la batterie est simplement inconnue.
    if (XR_SUCCEEDED(m_getCurrentInteractionProfile(session, handPath, &profile)) && profile.interactionProfile != XR_NULL_PATH &&
        (state.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_VALID_BIT_EXT) && !(state.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_NO_BATTERY_BIT_EXT) &&
        state.batteryLevel >= 0.f && state.batteryLevel <= 1.f) {
        newLevel = state.batteryLevel;
        newCharging = (state.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_CHARGING_BIT_EXT) != 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batteryLevel[hand] = newLevel;
    m_batteryCharging[hand] = newCharging;
    if (newLevel < 0.f) return false;
    level = newLevel;
    charging = newCharging;
    return true;
}

std::string HandInput::describe() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string text = m_trackHands ? "suivi des manettes prevu" : "suivi des manettes inactif";
    text += m_batteryExtension ? ", batterie OpenXR disponible" : ", batterie OpenXR indisponible";
    return text;
}

}  // namespace xrpo
