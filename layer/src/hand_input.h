// Position des manettes et batterie, pour accrocher le panneau ou la montre à une main.
//
// OpenXR ne donne la position d'une manette qu'au travers d'« actions », le mécanisme des boutons du
// jeu, et certaines étapes ne peuvent être faites qu'une fois par session, par le jeu. La layer se
// glisse donc dans trois appels du jeu, sans jamais rien retirer :
//   - xrSuggestInteractionProfileBindings : à chaque profil de manette que le jeu déclare, elle ajoute
//     une seule liaison, la position de la poignée (grip/pose) de chaque main. Elle n'ajoute jamais de
//     profil que le jeu n'a pas déclaré : le runtime choisit son profil de manette parmi ceux-là ;
//   - xrAttachSessionActionSets : elle ajoute son petit groupe d'actions à ceux du jeu ;
//   - xrSyncActions : idem, à chaque mise à jour.
// Aucune action de bouton : la layer ne peut rien prendre au jeu. Si l'un de ces appels échoue avec
// notre ajout, il est refait tel que le jeu l'a fourni et la fonction est abandonnée pour la session.
//
// Batterie : extension XR_EXT_interaction_profile_battery_state_display, quand le runtime la propose
// (la layer l'active elle-même à la création de l'instance, voir layer.cpp).
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "xr.h"

namespace xrpo {

class HandInput {
  public:
    // `trackHands` : préparer les positions des mains ; `battery` : l'extension de batterie est active.
    HandInput(XrInstance instance, PFN_xrGetInstanceProcAddr getProcAddr, bool trackHands, bool battery);

    bool tracking() const { return m_trackHands.load(); }
    XrInstance instance() const { return m_instance; }

    // Remplacent les fonctions du même nom ; appellent `next` (la fonction du maillon suivant).
    // Une exception ne peut sortir qu'avant l'appel à `next`, jamais après (pas d'appel rejoué).
    XrResult suggestBindings(PFN_xrSuggestInteractionProfileBindings next, XrInstance instance,
                             const XrInteractionProfileSuggestedBinding* suggested);
    XrResult attach(PFN_xrAttachSessionActionSets next, XrSession session, const XrSessionActionSetsAttachInfo* info);
    XrResult sync(PFN_xrSyncActions next, XrSession session, const XrActionsSyncInfo* info);

    // Avant la destruction d'une session : oublie ses espaces (OpenXR les détruit avec elle).
    void sessionDestroying(XrSession session);

    // Espace de la poignée d'une main (0 gauche, 1 droite), ou XR_NULL_HANDLE si indisponible.
    XrSpace handSpace(XrSession session, int hand);

    // Batterie d'une main, de 0 à 1 (false si inconnue). Lue au plus toutes les deux secondes.
    bool battery(XrSession session, int hand, double now, float& level, bool& charging);

    // Pour le journal : ce qui a été mis en place.
    std::string describe();

  private:
    bool createActions();
    bool profileAccepted(XrPath profile);

    std::mutex m_mutex;
    const XrInstance m_instance;
    std::atomic<bool> m_trackHands;
    const bool m_batteryExtension;

    PFN_xrStringToPath m_stringToPath = nullptr;
    PFN_xrPathToString m_pathToString = nullptr;
    PFN_xrCreateActionSet m_createActionSet = nullptr;
    PFN_xrCreateAction m_createAction = nullptr;
    PFN_xrCreateActionSpace m_createActionSpace = nullptr;
    PFN_xrGetCurrentInteractionProfile m_getCurrentInteractionProfile = nullptr;

    XrActionSet m_actionSet = XR_NULL_HANDLE;
    XrAction m_poseAction = XR_NULL_HANDLE;
    XrPath m_handPaths[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrPath m_posePaths[2] = {XR_NULL_PATH, XR_NULL_PATH};
    bool m_actionsTried = false;
    std::vector<XrPath> m_profiles;         // profils auxquels la position des mains a été ajoutée
    std::vector<XrPath> m_refusedProfiles;  // profils sans position de main (manette de jeu...)

    XrSession m_session = XR_NULL_HANDLE;   // session à laquelle notre groupe d'actions est attaché
    XrSpace m_spaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool m_syncFailed = false;
    bool m_attachedOnce = false;            // groupe déjà attaché : ses liaisons ne peuvent plus changer

    double m_batteryRead[2] = {-1.0, -1.0};
    float m_batteryLevel[2] = {-1.f, -1.f};  // < 0 : inconnue
    bool m_batteryCharging[2] = {false, false};
};

}  // namespace xrpo
