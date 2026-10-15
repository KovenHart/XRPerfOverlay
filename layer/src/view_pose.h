// Placement des calques devant les yeux, commun à la layer OpenXR et au programme pour les jeux
// SteamVR (OpenVR) : même emplacement, même orientation, quel que soit le jeu.
//
// Repère de la tête : -Z devant les yeux, +Y vers le haut, +X vers la droite (identique en OpenXR et
// en OpenVR). Un calque est vu de face le long de son +Z.
#pragma once

#include <cmath>

#include "config.h"
#include "pose_math.h"

namespace xrpo {

// Orientation qui amène la normale d'un calque placé en `position` (+Z) vers les yeux (origine).
inline Quat FacingViewer(Vec3 position) {
    const float length = std::sqrt(position.x * position.x + position.y * position.y + position.z * position.z);
    if (length <= 0.f) return Quat();
    const float nx = -position.x / length, ny = -position.y / length, nz = -position.z / length;
    // Rotation la plus courte de Z vers n : axe = Z x n, w = 1 + Z.n.
    const float qx = -ny, qy = nx, qw = 1.f + nz;
    const float norm = std::sqrt(qx * qx + qy * qy + qw * qw);
    if (norm <= 1e-6f) return Quat();
    return {qx / norm, qy / norm, 0.f, qw / norm};
}

// Panneau devant les yeux : emplacement (coin, écarts, distance) des réglages.
inline Pose PanelViewPose(const Config& c) {
    float x = 0.f, y = 0.f;
    switch (c.position) {
        case Corner::TopLeft: x = -c.offsetX; y = c.offsetY; break;
        case Corner::Top: y = c.offsetY; break;
        case Corner::TopRight: x = c.offsetX; y = c.offsetY; break;
        case Corner::BottomRight: x = c.offsetX; y = -c.offsetY; break;
        case Corner::Bottom: y = -c.offsetY; break;
        case Corner::BottomLeft:
        default: x = -c.offsetX; y = -c.offsetY; break;
    }
    Pose pose;
    pose.position = {x, y, -c.distance};
    if (c.faceViewer) pose.orientation = FacingViewer(pose.position);
    return pose;
}

// Montre qui n'est pas sur une main (ou manettes indisponibles) : en bas à droite du champ de vision.
inline Pose WatchViewPose() {
    Pose pose;
    pose.position = {0.24f, -0.24f, -0.75f};
    pose.orientation = FacingViewer(pose.position);
    return pose;
}

// Fenêtre capturée accrochée à une main : mêmes emplacements que le panneau. Au-dessus de la manette,
// elle est posée par son bord inférieur (une grande fenêtre ne traverse pas la main). `height` : hauteur
// de la fenêtre (m).
inline Pose WindowHandPose(Hand hand, HandPlacement placement, float along, float lift, float height) {
    Pose pose = HandPlacementPose(hand, placement, along, lift);
    if (placement == HandPlacement::Above) pose.position = pose.position + Rotate(pose.orientation, {0.f, height * 0.5f, 0.f});
    return pose;
}

// Fenêtre posée dans l'espace par Ctrl+Alt+G : à `distance` mètres dans la direction du regard, tournée
// vers les yeux et droite (son haut vers le haut de la pièce). `head` : pose de la tête dans le repère
// de la pièce (+Y vers le haut).
inline Pose PlaceInFront(const Pose& head, float distance) {
    const Vec3 forward = Rotate(head.orientation, {0.f, 0.f, -1.f});
    Pose pose;
    pose.position = head.position + forward * distance;
    const Vec3 Z = forward * -1.f;  // face de la fenêtre vers les yeux
    Vec3 X = Cross({0.f, 1.f, 0.f}, Z);
    const float length = Length(X);
    // Regard presque vertical : la gauche et la droite sont celles de la tête.
    X = length > 1e-3f ? X * (1.f / length) : Rotate(head.orientation, {1.f, 0.f, 0.f});
    const Vec3 Y = Cross(Z, X);
    pose.orientation = FromBasis(X, Y, Z);
    return pose;
}

}  // namespace xrpo
