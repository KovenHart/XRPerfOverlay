# XR Perf Overlay : développement

*English: [DEVELOPMENT.md](DEVELOPMENT.md)* · [Retour à l'accueil](../README.md)

Compiler depuis les sources, construire et publier le paquet, organisation du code. Pour simplement utiliser XR Perf Overlay, voir l'[accueil](../README.md).

## Les programmes

| Dossier | Rôle |
|---|---|
| `layer/` | La *API layer* OpenXR (DLL C++) chargée dans le jeu : mesure les FPS et affiche le panneau. |
| `sensors/` | `XRPerfSensors.exe` (C#), lancé à part : lit les capteurs du PC et les transmet à la layer ; sa fenêtre (interface dans `sensors/ui/`) règle l'overlay et montre les rapports. |
| `layer/src/vr_*.cpp` | `XRPerfVR.exe` (C++), lancé par XRPerfSensors : panneau, montre et rapports dans les jeux SteamVR (OpenVR). |
| `layer/src/window_capture.cpp` | `XRPerfCapture.dll` (C++), chargée par XRPerfSensors : capture de la fenêtre affichée dans le casque. |

## Deux façons de l'obtenir

- **Paquet de distribution** (`XRPerfOverlay-<version>-win64.zip`, page « Releases » du dépôt) : le programme déjà compilé. Rien à installer à part lui : ni Visual Studio, ni SDK, ni runtime .NET (inclus). Extraire l'archive, double-cliquer sur `Installer.bat`. Le fichier `LISEZ-MOI.txt` du paquet guide l'utilisateur.
- **Sources** (ce dépôt) : `Installer.bat` compile d'abord, ce qui demande les outils ci-dessous.

Dans les deux cas, le pilote PawnIO est facultatif (température et fréquence du processeur seulement).

## Prérequis (compilation depuis les sources)

- Windows 10 ou 11, 64 bits
- Visual Studio 2022 (ou plus récent) avec les charges **« Développement Desktop en C++ »** et **« Développement .NET Desktop »**
  (à défaut de la seconde, le SDK .NET seul suffit : `winget install Microsoft.DotNet.SDK.10`). Sans SDK .NET, `build.bat` compile quand même la layer, mais `Installer.bat` s'arrête : il n'installe que l'ensemble complet.
- Pour la température et la fréquence du **processeur** : le pilote **PawnIO**, à installer une fois (`winget install namazso.PawnIO`, ou https://pawnio.eu). C'est le pilote qu'utilise LibreHardwareMonitor depuis sa version 0.9.5. Sans lui, tout le reste fonctionne et ces deux valeurs affichent `--`.
- Une connexion internet à la première compilation (NuGet télécharge LibreHardwareMonitorLib).

## Installer à la main (sans Installer.bat)

`build.bat` compile seulement et rassemble le résultat dans `dist\` :

```
dist\
  XR_APILAYER_NOVENDOR_perf_overlay.dll    la layer
  XR_APILAYER_NOVENDOR_perf_overlay.json   sa déclaration pour OpenXR
  XRPerfOverlay.ini                        les réglages
  install.bat / uninstall.bat
  sensors\                                 le programme de capteurs (XRPerfSensors.exe et ses bibliothèques)
```

Ce dossier `dist\` se copie tel quel (y compris **son** sous-dossier `sensors\`, à ne pas confondre avec le dossier `sensors\` des sources, à côté de `build.bat`) à l'emplacement voulu, de préférence un dossier où vous avez le droit d'écrire, pour que les raccourcis puissent mémoriser leur état dans le `.ini`. `install.bat` y enregistre la layer ; `install.bat -AutoStartSensors` fait en plus démarrer `XRPerfSensors` à l'ouverture de session, sans invite UAC. Sans cela, lancer `sensors\XRPerfSensors.exe` avant de jouer (icône dans la zone de notification, clic droit → Quitter).

Avec UEVR, choisir **OpenXR** comme runtime dans l'injecteur : en mode OpenVR, le jeu ne passe pas par OpenXR et la layer n'est pas chargée.

Désinstallation : `uninstall.bat` dans le dossier d'installation, puis supprimer ce dossier.

## Version et distribution

**Numéro de version.** Il est écrit à un seul endroit, le fichier `VERSION` à la racine (forme `1.2.3`). La compilation le reporte partout : ressource de version de la DLL (propriétés du fichier, onglet Détails), XRPerfSensors (fenêtre, menu et info-bulle de l'icône), journal de la layer (ligne « Instance OpenXR créée »), en-tête de chaque rapport de session, et fichier `VERSION` du dossier d'installation. Si la DLL et XRPerfSensors installés ne viennent pas de la même version, XRPerfSensors le signale au démarrage.

**Construire le paquet sur son PC.** Double-cliquer sur `Creer-paquet.bat` : il compile (comme `build.bat`), publie XRPerfSensors en mode autonome (runtime .NET inclus ; il faut Internet pour que NuGet le télécharge), puis produit `release\XRPerfOverlay-<version>-win64.zip` et son empreinte SHA-256. Le script `scripts\package.ps1` fait le travail ; il vérifie que la DLL porte bien le numéro du fichier `VERSION`, que XRPerfSensors est bien autonome et que le paquet contient tout ce que l'installateur attend.

Contenu du paquet :

```
XRPerfOverlay-<version>\
  Installer.bat           installation ou mise à jour (rien à compiler)
  LISEZ-MOI.txt           guide de l'utilisateur (README-EN.txt : le même en anglais)
  README.md, CHANGELOG.md, docs\   accueil, nouveautés, guide et captures (en français et en anglais)
  LICENSE.txt, THIRD-PARTY.md
  scripts\setup.ps1
  programme\              ce qui est copié dans le dossier d'installation (DLL, .json, .ini, VERSION,
                          install / uninstall, sensors\ avec le runtime .NET)
```

**Compilation automatique (GitHub Actions).** Une fois le projet sur GitHub, le fichier `.github/workflows/build.yml` compile la layer avec MSVC et XRPerfSensors sur une machine Windows à chaque envoi, lance les tests de `tests/preview.cpp`, construit le paquet avec Windows PowerShell 5.1 et le joint à l'exécution (onglet Actions, rubrique Artifacts). Pour publier une version : mettre le numéro dans `VERSION`, valider, puis `git tag v1.0.0` et `git push origin v1.0.0`. Le workflow vérifie que l'étiquette correspond au fichier `VERSION` et crée un **brouillon** de version avec le paquet : il reste à le relire et à le publier depuis la page « Releases ».

**Signature.** Les fichiers ne sont pas signés : Windows affiche « éditeur inconnu » au premier lancement de `Installer.bat`, ce que `LISEZ-MOI.txt` explique. L'installateur retire ensuite la marque « provenant d'Internet » des fichiers installés, pour que XRPerfSensors ne redemande pas de confirmation à chaque lancement.

**Licence.** Le code du projet est sous licence MIT (`LICENSE`). Les composants tiers et leurs licences sont dans `THIRD-PARTY.md`.

## État de vérification

Ce qui a été vérifié avant livraison, et ce qui ne l'a pas été :

- La layer C++ compile et se lie pour Windows 64 bits (compilation croisée clang / MinGW-w64, avertissements traités comme des erreurs).
- Sa logique OpenXR a été exécutée contre un faux chargeur et un faux runtime (boucle normale, raccourcis, image de swapchain pas prête, refus du calque par le runtime, destruction et recréation de session, session Vulkan, programme exclu), sous AddressSanitizer.
- La montre et l'accroche aux manettes passent par le banc de test : déclarations des commandes (ajout pour une manette, rien pour une manette de jeu), groupe d'actions ajouté à celui du jeu, mises à jour, deux calques, main non suivie, affichage au regard, panneau sur une main, nouvelle session, jeu sans actions, groupe refusé par le runtime, extension de batterie demandée, refusée ou absente. Les trois styles de montre sont dessinés et comparés, et les positions sur la main vérifiées par le calcul (axes, orientation vers les yeux). Le placement réel sur des manettes n'a pas été vu.
- La fenêtre capturée passe par les deux bancs de test : dans la layer (swapchain à part, image copiée seulement quand elle change, conversion de format, pose devant soi à la première image et par `Ctrl+Alt+G`, espace STAGE ou LOCAL, tête non suivie, sur une main, main perdue, jeu sans manettes, session recréée) et dans XRPerfVR (posée, `Ctrl+Alt+G`, `Ctrl+Alt+F`, sur une main par les réglages, renvoi de l'image après une perte des textures). Le recadrage et la réduction de l'image sont testés par le calcul. Essayée sur un vrai PC (Windows 11, casque Pimax, SteamVR 2.16) : capture d'une fenêtre Discord, aperçu et recadrage dans l'onglet Fenêtre (1.4), affichage dans un jeu OpenXR (mod VR de Diablo II) et dans un jeu OpenVR (DoE), au poignet et posée dans l'espace.
- XRPerfVR (jeux OpenVR) compile pour Windows avec l'en-tête officiel d'OpenVR 2.15.6 ; sa logique est exécutée contre un faux SteamVR (attente de SteamVR sans le lancer, connexion différée, jeu au premier plan, panneau identique à celui de la layer avec le temps GPU mesuré, montre au poignet, batterie, affichage au regard, réglages en direct, raccourcis, jeux OpenXR laissés à la layer, jeu Vulkan, layer qui se signale en retard, processus exclu, changement de jeu, fermeture normale et brutale de SteamVR, numérotation des images qui repart de zéro, échec de connexion, calques refusés), sous AddressSanitizer. Les conversions entre poses et matrices d'OpenVR sont testées par le calcul. Le code de XRPerfVR et son lancement par XRPerfSensors ont été relus de façon indépendante, et les défauts trouvés corrigés. Essayé avec SteamVR 2.16 et un casque Pimax (jeu OpenVR) : connexion, suivi du jeu, temps GPU mesuré, rapport, montre au poignet, batterie, fermeture et relance de SteamVR. L'image des calques est envoyée à SteamVR sous forme de texture Direct3D 11 (depuis la 1.3.2 : l'envoi d'une image brute faisait clignoter le panneau, puis le figeait), essayée elle aussi dans un casque.
- Le dessin du panneau et le calcul des statistiques sont testés (`tests/preview.cpp`), y compris le rapport de session (valeurs exactes du relevé minute par minute et de la répartition). Côté layer, le banc de test vérifie aussi l'estimation par défaut, la mesure réelle avec une fausse carte, son absence (API sans mesure), une mesure qui s'interrompt et un pilote qui ne renvoie rien.
- La lecture et l'analyse des rapports (`sensors/SessionReport.cs`) sont exécutées telles quelles sur des rapports produits par le code C++ : fichiers complets, avec mesure, mixtes, tronqués et illisibles.
- L'ensemble du code a été relu une seconde fois de façon indépendante (protocole OpenXR, Direct3D 12, scripts, programme C#) et les défauts trouvés ont été corrigés.
- Le programme C# est compilé avec le compilateur Roslyn contre les bibliothèques Windows Forms, LibreHardwareMonitorLib 0.9.6 et le SDK WebView2. Deux parties sans fenêtre sont exécutées telles quelles : le bloc de réglages écrit par le C# est relu par le C++ (même disposition, mêmes valeurs par défaut), et l'aperçu hors jeu est appelé depuis le code C# sur la bibliothèque compilée. La lecture des rapports et leur mise en forme pour l'interface (`SessionReport.cs`, `ReportJson.cs`) sont exécutées sur des rapports français et anglais produits par le code C++.
- L'interface (`sensors/ui/`) est exécutée dans Chromium, le moteur de WebView2, avec un programme simulé (`sensors/ui/dev/mock.js`) qui lui envoie de vraies données : panneaux et montres dessinés par la layer, rapports analysés par le vrai code C#. Toutes les pages ont été vues en français et en anglais, en thème sombre et clair, en grand et en petit format. La fenêtre WebView2 (`MainWindow.cs`) de la 1.5 a été utilisée sur un vrai PC sous Windows 11.
- Rapports en mode manuel (1.6) : la décision (mode, raccourci, changement de mode en jeu, témoin) est testée dans `tests/preview.cpp` ; les deux bancs de test (layer et XRPerfVR) font démarrer et arrêter des enregistrements au raccourci (rapport gardé, trop court, terminé à la fin de la session, mode changé en direct). Le témoin est dessiné dans les trois présentations. Pas encore essayé en jeu.
- Activation de la layer (1.6, `LayerRegistry.cs`) : compilée et relue, pas encore essayée sous Windows (écriture dans le registre, mise à jour qui garde la layer désactivée).
- Le script de paquet (`scripts/package.ps1`) est exécuté avec une doublure de `dotnet` : contenu et archive du paquet, vérification du mode autonome, échec de la publication. Le paquet obtenu est ensuite installé par `setup.ps1` dans ses essais. Le workflow GitHub Actions est vérifié pour sa syntaxe seulement : sa première exécution réelle se fera sur GitHub.
- Le script d'installation (`scripts/setup.ps1`) est essayé avec des doublures à la place du registre, des tâches planifiées et de la demande d'élévation : première installation, mise à jour d'un dossier abîmé, jeu encore ouvert, compilation incomplète, installation sur place, archive extraite dans le dossier d'installation, échec en cours de route puis reprise. Les scripts ont aussi été relus une fois de façon indépendante.
- Essai réel : la layer compilée avec Visual Studio (MSVC) fonctionne en jeu sur un casque Pimax, aux côtés d'OFXR Bridge et d'autres mods, à travers Pimax OpenXR comme à travers SteamVR. XRPerfSensors, ses fenêtres de réglages et de rapports (celles de la 1.4, puis la fenêtre de la 1.5), le panneau sur le bureau, la montre, l'accroche aux manettes, la batterie (sous SteamVR), la ligne session / batterie, XRPerfVR (jeux OpenVR) et la fenêtre capturée ont été utilisés en jeu.
- **Non vérifié** : les autres runtimes (Meta, Virtual Desktop, Windows Mixed Reality...) et les casques d'autres marques. Les deux chemins Direct3D ont été vus en jeu : Direct3D 11 (DoE) et Direct3D 12 (mod VR de Diablo II). La mesure réelle du temps GPU (requêtes Direct3D 11, listes de commandes Direct3D 12) est compilée et relue, mais n'a jamais tourné sur une vraie carte : à vérifier en jeu, en comparant avec l'estimation. `Installer.bat`, `build.bat` et les parties de l'installation propres à Windows (élévation, registre, tâche planifiée) ont été utilisés sur un vrai PC sous Windows 11 ; la compilation automatique de GitHub n'a pas encore tourné. La cadence après génération d'images et l'horloge ont été confirmées en jeu avec OFXR Bridge. Le nom que chaque runtime annonce pour chaque casque n'a pas été relevé : seul le journal dira ce que le vôtre renvoie.

## Organisation du code

```
layer/src/layer.cpp           négociation avec le chargeur OpenXR, fonctions interceptées
layer/src/overlay.cpp         logique par session : mesures, swapchain, calque quad, raccourcis
layer/src/gfx_d3d11.cpp       copie du panneau vers la texture, mesure du temps GPU (Direct3D 11)
layer/src/gfx_d3d12.cpp       idem (Direct3D 12)
layer/src/panel.cpp           dessin du panneau et de la montre en mémoire (mise en page, couleurs)
layer/src/hand_input.cpp      position des manettes (ajout aux actions du jeu) et batterie
layer/src/pose_math.h         emplacements sur la main, test « le poignet est-il regardé ? »
layer/src/ascii.h             réduction d'un nom (casque, runtime) aux caractères affichables
layer/src/stats.h             FPS, AVG, MIN, MAX, temps CPU
layer/src/frame_timer.h       appariement xrWaitFrame / xrBeginFrame / xrEndFrame, cadence demandée
layer/src/output_tap.h        compteurs du maillon de sortie de chaîne (cadence après génération d'images)
layer/src/report_control.h    quand écrire un rapport : automatique, manuel (Ctrl+Alt+S, témoin REC) ou jamais
layer/src/session_stats.h     cumul sur toute la session et mise en forme du rapport texte (relu par SessionReport.cs)
layer/src/summary_file.cpp    fichier du résumé (gardé ouvert, réécrit sur place)
layer/src/panel_share.cpp     copie du panneau vers la fenêtre du bureau
layer/src/layer_presence.cpp  signal « la layer mesure / affiche dans ce jeu », pour XRPerfVR
layer/src/vr_host.cpp         XRPerfVR : jeux OpenVR (attente de SteamVR, sessions, mesures, calques, rapports)
layer/src/openvr_backend.cpp  XRPerfVR : dialogue avec SteamVR (openvr_api.dll) et Windows
layer/src/vr_main.cpp         XRPerfVR : programme (lancé et arrêté par XRPerfSensors)
layer/src/window_capture.cpp  XRPerfCapture.dll : capture d'une fenêtre (API de capture de Windows) vers la mémoire partagée
layer/src/window_pixels.h     recadrage, réduction et opacité de l'image capturée
layer/src/window_share.cpp    lecture de l'image capturée (layer et XRPerfVR)
sensors/WindowCapture.cs      choix de la fenêtre, mémoire partagée, pilotage de la capture
sensors/VrCompanion.cs        lancement, relance et arrêt de XRPerfVR
sensors/PanelWindow.cs        fenêtre miroir du panneau (OBS, aperçu hors jeu) et réglages propres au PC (XRPerfSensors.ini)
sensors/OverlaySettings.cs    réglages du casque : lecture / écriture du .ini, envoi à la layer, aperçu dessiné par la layer
sensors/MainWindow.cs         fenêtre principale : WebView2, messages avec l'interface, aperçus, capteurs, plein écran
sensors/SettingsJson.cs       réglages échangés avec l'interface (JSON) et leurs bornes
sensors/ReportStore.cs        rapports : lecture des dossiers, session précédente, ouverture, suppression
sensors/ReportJson.cs         rapports mis en forme pour l'interface (analyses dans la langue choisie)
sensors/SessionReport.cs      lecture (français ou anglais) et analyse des rapports, lecture « simple » en phrases (sans interface)
sensors/LayerRegistry.cs      activation de la layer dans le registre OpenXR (relance élevée « --layer on|off »)
sensors/Loc.cs                langue des textes du programme (celle de Windows, français ou anglais)
sensors/ui/                   interface : index.html, app.css, app.js (pages), charts.js (courbes), i18n.js (textes FR / EN), polices
sensors/ui/dev/               programme simulé pour mettre l'interface au point dans un navigateur (non livré)
layer/src/settings_reader.cpp lecture des réglages envoyés par XRPerfSensors
layer/src/settings_apply.h    application bornée de ces réglages, noms des présentations et des couleurs
layer/src/frame_history.h     historique des temps d'image, CPU et GPU (courbes de la présentation graphique)
layer/src/preview_api.cpp     aperçu hors jeu, fonction exportée par la DLL pour XRPerfSensors
layer/src/sensors_reader.cpp  lecture de la mémoire partagée
layer/src/shared_block.h      format de la mémoire partagée (doit rester aligné sur sensors/Program.cs)
sensors/Program.cs            lecture des capteurs et icône de notification
sensors/XRPerfSensors.ico     icône du programme (fichier .exe, zone de notification, fenêtre du bureau)
VERSION                       numéro de version (seule source, repris par la DLL, XRPerfSensors et le paquet)
Installer.bat                 installation en un double-clic (compile d'abord s'il est lancé depuis les sources)
Creer-paquet.bat              construit le paquet de distribution (release\XRPerfOverlay-<version>-win64.zip)
scripts/package.ps1           assemblage du paquet (publication autonome de XRPerfSensors, archive, empreinte)
.github/workflows/build.yml   compilation, tests et paquet automatiques sur GitHub ; brouillon de version sur étiquette
docs/LISEZ-MOI.txt            guide de l'utilisateur joint au paquet (docs/README-EN.txt : en anglais)
layer/src/version.rc          ressource de version de la DLL (numéros générés depuis VERSION)
layer/src/hardware_info.cpp   modèles du processeur (registre) et de la carte graphique (DXGI) pour les rapports
scripts/setup.ps1             copie dans le dossier d'installation, enregistrement, relance des capteurs
scripts/install.ps1           enregistrement de la layer auprès d'OpenXR (uninstall.ps1 : l'inverse)
tests/preview.cpp             tests des statistiques + aperçu du panneau en image (cmake -DXRPO_BUILD_TESTS=ON)
tools/gen_font.py             régénère layer/src/font_data.h (seulement pour changer de police)
tools/gen_icon.py             redessine sensors/XRPerfSensors.ico (seulement pour changer l'icône)
```

Pour modifier l'apparence du panneau, tout est dans `panel.cpp` ; l'aperçu permet de voir le résultat sans lancer de jeu.

Licences des composants tiers : voir `THIRD-PARTY.md`.
