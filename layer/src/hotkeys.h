// Raccourcis clavier (Ctrl+Alt+touche) qui modifient l'overlay en jeu, communs à la layer OpenXR et
// au programme pour les jeux SteamVR. Lus avec GetAsyncKeyState : ils fonctionnent quelle que soit
// la fenêtre active.
#pragma once

#include <windows.h>

#include "config.h"

namespace xrpo {

class Hotkeys {
  public:
    static constexpr int kCount = 9;
    // Ce qu'a changé un appel de poll().
    enum Change : unsigned {
        None = 0,
        Visibility = 1,
        Placement = 2,
        ResetStats = 4,
        Size = 8,
        Opacity = 16,
        WindowVisibility = 32,  // fenêtre capturée affichée ou masquée
        WindowPlace = 64,       // fenêtre capturée à poser devant soi : l'appelant calcule sa pose
    };

    // À appeler régulièrement (à chaque image ou plus souvent). Modifie `config` et retourne les
    // changements faits ; ResetStats est à faire par l'appelant (remise à zéro des statistiques).
    unsigned poll(Config& config, double now) {
        unsigned changes = None;
        // Ordre : afficher/masquer, emplacement, remise à zéro, taille +, taille -, opacité +, opacité -,
        // fenêtre capturée affichée/masquée, fenêtre posée devant soi.
        const bool modifiers = pressed(VK_CONTROL) && pressed(VK_MENU);
        const int keys[kCount] = {config.keyToggle,    config.keyPosition,    config.keyReset,        config.keySizeUp,      config.keySizeDown,
                                  config.keyOpacityUp, config.keyOpacityDown, config.keyWindowToggle, config.keyWindowPlace};
        for (int i = 0; i < kCount; ++i) {
            const bool down = modifiers && pressed(keys[i]);
            bool trigger = down && !m_keyDown[i];
            if (trigger) m_keyRepeat[i] = now + 0.4;  // délai avant répétition
            // Les réglages progressifs (taille, opacité) se répètent tant que la touche reste enfoncée.
            if (down && m_keyDown[i] && i >= 3 && i <= 6 && now >= m_keyRepeat[i]) {
                trigger = true;
                m_keyRepeat[i] = now + 0.12;
            }
            m_keyDown[i] = down;
            if (!trigger) continue;

            switch (i) {
                case 0:
                    config.visible = !config.visible;
                    changes |= Visibility;
                    break;
                case 1:
                    config.position = static_cast<Corner>((static_cast<int>(config.position) + 1) % static_cast<int>(Corner::Count));
                    changes |= Placement;
                    break;
                case 2:
                    changes |= ResetStats;
                    continue;  // rien à mémoriser
                case 3:
                case 4: {
                    // Pas de 5 % : la taille change tout de suite, le panneau garde son centre.
                    const float width = config.width * (i == 3 ? 1.05f : 1.f / 1.05f);
                    config.width = width < kMinWidth ? kMinWidth : (width > kMaxWidth ? kMaxWidth : width);
                    changes |= Size;
                    break;
                }
                case 7:
                    config.window.enabled = !config.window.enabled;
                    changes |= WindowVisibility;
                    break;
                case 8:
                    // Posée dans l'espace, affichée : la pose elle-même est calculée par l'appelant.
                    config.window.anchor = WindowAnchor::World;
                    config.window.enabled = true;
                    changes |= WindowPlace;
                    break;
                default: {
                    const float opacity = config.globalOpacity + (i == 5 ? 0.05f : -0.05f);
                    config.globalOpacity =
                        opacity < kMinGlobalOpacity ? kMinGlobalOpacity : (opacity > kMaxGlobalOpacity ? kMaxGlobalOpacity : opacity);
                    changes |= Opacity;
                    break;
                }
            }
            m_stateDirty = true;
            m_stateDirtySince = now;
        }
        return changes;
    }

    // L'écriture du .ini attend une demi-seconde de calme : pas d'accès disque à chaque pas pendant
    // qu'une touche est maintenue. Retourne true quand il est temps d'écrire (une seule fois).
    bool saveDue(double now) {
        if (!m_stateDirty || now - m_stateDirtySince < 0.5) return false;
        m_stateDirty = false;
        return true;
    }

    // Changement pas encore écrit (à écrire en fin de session).
    bool takePending() {
        const bool pending = m_stateDirty;
        m_stateDirty = false;
        return pending;
    }

    // Ctrl+Alt+`virtualKey` enfoncés en ce moment (0 : raccourci désactivé). Sert au raccourci des
    // rapports en mode manuel, relevé par celui qui écrit le rapport.
    static bool comboDown(int virtualKey) { return virtualKey > 0 && pressed(VK_CONTROL) && pressed(VK_MENU) && pressed(virtualKey); }

  private:
    static bool pressed(int virtualKey) { return virtualKey > 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0; }

    bool m_keyDown[kCount] = {};
    double m_keyRepeat[kCount] = {};
    bool m_stateDirty = false;
    double m_stateDirtySince = 0.0;
};

}  // namespace xrpo
