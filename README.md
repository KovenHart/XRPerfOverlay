# XR Perf Overlay

**🇬🇧 [English version](README.en.md)** · 🇫🇷 Français

Un panneau de performances affiché dans le casque pour tous les jeux OpenXR (dont les jeux lancés avec UEVR), et aussi pour les jeux SteamVR qui ne passent pas par OpenXR (OpenVR) :

- **FPS** instantané, moyenne (AVG), minimum et maximum
- **CPU** : charge, frametime, température, fréquence
- **GPU** : charge, frametime, température, fréquence, VRAM

En option : une montre au poignet, et une fenêtre du bureau (le chat d'un stream, Discord...) affichée dans le casque. Un rapport est écrit à chaque session de jeu (ou au raccourci `Ctrl+Alt+S`), avec son analyse et ses courbes. La page Vidéo enregistre ce que vous voyez dans le casque (H.264, H.265 ou AV1 par la carte graphique, avec le son du jeu et le micro, le panneau et la stabilisation en option), sans OBS. La page Image ajoute en option netteté (CAS, RCAS, LumaSharpen), réglages des couleurs, faux HDR, débandage et mode daltonien, avec des presets, dans le casque ou dans la vidéo. Tous les raccourcis sont modifiables. Pour streamer, le direct part sans OBS sur **Twitch**, **YouTube** ou les deux en même temps (H.264, et H.265 ou AV1 pour YouTube) ; ou une source OBS « XR Perf (casque) » reçoit cette même image, sans double encodage. La page Configuration fait le diagnostic du PC pour la VR : runtime OpenXR, layers installées (à activer, désactiver ou réordonner), matériel, réglages de Windows.

![Aperçu du panneau](docs/apercu.png)

Tout se règle dans la fenêtre de XRPerfSensors, même en cours de partie :

![Tableau de bord de XRPerfSensors](docs/interface-tableau-de-bord.png)

![Rapport d'une session](docs/interface-rapport.png)

Il passe par le mécanisme officiel d'OpenXR : rien n'est injecté dans le jeu, aucune fonction Direct3D n'est détournée. Il fonctionne aux côtés d'UEVR, d'OFXR Bridge et des autres mods.

## Installation

1. Téléchargez `XRPerfOverlay-<version>-win64.zip` dans la [dernière version](../../releases/latest).
2. Extrayez toute l'archive (clic droit, « Extraire tout »).
3. Double-cliquez sur **`Installer.bat`**.
   - Windows peut afficher « Windows a protégé votre ordinateur » : le programme n'est pas signé. Cliquez sur « Informations complémentaires », puis « Exécuter quand même ».
   - Acceptez la demande d'autorisation d'administrateur : elle sert à déclarer la layer auprès d'OpenXR.
4. Choisissez si XRPerfSensors (le programme qui lit les capteurs) démarre avec Windows.

Le programme s'installe dans `C:\XRPerfOverlay`. Pour une **mise à jour**, faites la même chose : vos réglages et vos rapports sont conservés. Le dossier extrait peut être supprimé ensuite.

**Ce qu'il faut** : Windows 10 ou 11, 64 bits, et un jeu OpenXR en Direct3D 11 ou 12, ou un jeu SteamVR. Rien d'autre à installer : le runtime .NET est inclus, et le moteur WebView2 de la fenêtre est déjà dans Windows 11 et Windows 10 à jour.

**Facultatif** : le pilote [PawnIO](https://pawnio.eu/), pour la température et la fréquence du processeur (`winget install namazso.PawnIO`). Sans lui, ces deux valeurs affichent `--`.

**Avec UEVR**, choisissez **OpenXR** comme runtime dans l'injecteur.

**Désinstallation** : lancez `uninstall.bat` dans `C:\XRPerfOverlay`, puis supprimez ce dossier.

## Utilisation

Lancez un jeu VR : le panneau apparaît dans le casque. Double-cliquez sur l'icône de XRPerfSensors près de l'horloge pour ouvrir sa fenêtre (réglages, rapports).

| Raccourci | Action |
|---|---|
| `Ctrl + Alt + O` | Afficher / masquer le panneau |
| `Ctrl + Alt + P` | Changer d'emplacement |
| `Ctrl + Alt + R` | Remettre à zéro AVG / MIN / MAX |
| `Ctrl + Alt + Page précédente` / `Page suivante` | Agrandir / réduire le panneau |
| `Ctrl + Alt + Début` / `Fin` | Panneau plus opaque / plus transparent |
| `Ctrl + Alt + F` | Afficher / masquer la fenêtre du bureau |
| `Ctrl + Alt + G` | Poser la fenêtre du bureau devant soi |
| `Ctrl + Alt + V` | Vidéo : démarrer / arrêter l'enregistrement ou le direct |
| `Ctrl + Alt + S` | Rapport en mode manuel : démarrer / arrêter l'enregistrement |

Tous ces raccourcis se changent dans la fenêtre de XRPerfSensors.

## En savoir plus

- [Guide d'utilisation](docs/GUIDE.md) : chaque page de la fenêtre, la montre, les jeux SteamVR, les rapports, ce que mesure chaque valeur, le dépannage.
- [CHANGELOG](CHANGELOG.md) : les nouveautés de chaque version.
- [Développement](docs/DEVELOPPEMENT.md) : compiler depuis les sources, construire le paquet, organisation du code.

Licence MIT ([LICENSE](LICENSE)). Composants tiers : [THIRD-PARTY.md](THIRD-PARTY.md).
