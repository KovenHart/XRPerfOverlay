# Changelog

*English: [CHANGELOG.en.md](CHANGELOG.en.md)*

Les nouveautés de chaque version d'XR Perf Overlay, la plus récente en premier.

## 1.6.0 (2026-10-19)

- **Désactiver la layer OpenXR** sans la désinstaller : interrupteur « Jeux OpenXR (layer) » dans XRPerfSensors (page Panneau, tableau de bord) et case « Layer OpenXR activée » dans le menu de l'icône. Désactivée, la layer n'est plus chargée du tout dans les jeux OpenXR ; un bandeau le rappelle sur le tableau de bord. Une mise à jour la laisse désactivée.
- **Rapports au raccourci** : page Mesures, « Écrire un rapport » à chaque session, au raccourci ou jamais. Au raccourci, `Ctrl+Alt+S` démarre l'enregistrement (point rouge et « REC » avec la durée en haut du panneau), puis l'arrête et écrit le rapport de ce passage seulement. Le mode change aussitôt, même en jeu. Fonctionne aussi dans les jeux SteamVR. Dans `XRPerfOverlay.ini` : `mode=` dans `[summary]`, touche `record=` dans `[hotkeys]`.
- Les rapports faits au raccourci sont marqués « manuel » dans la page Rapports.

## 1.5.0

Publiée avec la 1.6.0.

- **Nouvelle fenêtre XRPerfSensors** : une seule fenêtre avec un menu à gauche (tableau de bord, panneau, apparence, montre, fenêtre, miroir du bureau, mesures, rapports, interface). Plein écran avec `F11`. Couleurs au choix : comme le panneau, six thèmes ou trois couleurs à vous ; taille du texte, animations. Elle utilise le moteur WebView2 de Microsoft.
- **Tableau de bord** : le panneau tel qu'il apparaît dans le casque, les interrupteurs rapides, le processeur, la carte graphique et la mémoire vidéo en direct, la dernière session.
- **Rapports** : l'analyse et les courbes minute par minute sur la même page. La lecture simple est illustrée (grille de 100 images, barres de ce qui a limité, jauges de température et de mémoire vidéo). La comparaison de deux sessions montre les écarts en barres, les deux grilles d'images et les courbes superposées.
- **Anglais** : tout est traduit (fenêtre, menu de l'icône, panneau et montre dans le casque, rapports, installateur, documentation). Par défaut, la langue de Windows ; à changer dans la page Interface.
- Les anciennes fenêtres de réglages et de rapports sont remplacées par la nouvelle fenêtre.

## 1.4.3 (2026-10-15)

- Fenêtre du bureau dans le casque : correction quand le recadrage change la taille de l'image capturée.

## 1.4.2 (2026-10-15)

- Fenêtre du bureau : réglage du recadrage plus lisible et aperçu rafraîchi plus souvent.

## 1.4.1 (2026-10-15)

- Jeux SteamVR : XRPerfVR se reconnecte à un SteamVR relancé aussitôt après sa fermeture (fin de la configuration de la pièce, par exemple).

## 1.4.0 (2026-10-15)

- **Fenêtre du bureau dans le casque** : une fenêtre ouverte sur le PC (chat d'un stream, Discord...) affichée en lecture seule, posée dans la pièce ou sur une main. `Ctrl+Alt+F` l'affiche ou la masque, `Ctrl+Alt+G` la pose devant soi. Recadrage, taille, opacité et nombre d'images par seconde réglables. Dans les jeux OpenXR et SteamVR.

## 1.3.2 (2026-10-14)

- Jeux SteamVR : le panneau ne clignote plus et ne se fige plus (image envoyée à SteamVR en texture Direct3D 11).

## 1.3.1 (2026-10-14)

- Ligne « session / batterie » en option en bas du panneau : temps de session et batterie des manettes.

## 1.3.0 (2026-10-14)

- **Jeux SteamVR (OpenVR)** : le panneau, la montre et les rapports fonctionnent aussi dans les jeux qui ne passent pas par OpenXR, grâce à XRPerfVR, lancé par XRPerfSensors. Le temps GPU y est mesuré par SteamVR.

## 1.2.3 (2026-10-13)

- Correction : un jeu dont la première demande à OpenXR était refusée (pour une raison sans rapport avec la layer) recevait une erreur trompeuse.

## 1.2.2 (2026-10-13)

- Ligne casque / résolution : l'écart avec la résolution recommandée par le runtime est indiqué (par exemple `x1.21`), et repris dans les rapports.

## 1.2.1 (2026-10-13)

- Analyse des rapports : signale une résolution changée par le jeu en cours de partie, et des réglages différents (résolution, casque, runtime, API graphique) entre deux sessions comparées.

## 1.2.0 (2026-10-13)

- **Montre** au poignet ou devant les yeux : heure, temps de session, batterie des manettes, FPS, en trois styles. Au poignet, elle n'apparaît que quand on la regarde (option).
- Le panneau peut être accroché à une main.

## 1.1.0 (2026-10-12)

- Modèles du processeur et de la carte graphique dans les rapports (option).
- Fenêtre des rapports : lecture « Simple » en phrases, lecture « Détaillée » avec des constats, comparaison de deux sessions.

## 1.0.0 (2026-10-12)

- Première version : panneau de performances dans le casque pour les jeux OpenXR (FPS, CPU, GPU, VRAM), présentations et couleurs au choix, raccourcis clavier, temps GPU estimé ou mesuré, miroir du panneau sur le bureau pour OBS, rapport à chaque session, fenêtre de réglages, installateur et paquet prêt à installer.
