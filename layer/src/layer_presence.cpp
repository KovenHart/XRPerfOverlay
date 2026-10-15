#include "layer_presence.h"

#include <windows.h>

#include <cwchar>

namespace xrpo {

namespace {

void name(wchar_t (&out)[96], const wchar_t* what, uint32_t pid) {
    std::swprintf(out, 96, L"Local\\XRPerfOverlay.Layer.%ls.%lu", what, static_cast<unsigned long>(pid));
}

void toggle(void*& handle, bool wanted, const wchar_t* what) {
    if (wanted == (handle != nullptr)) return;
    if (!wanted) {
        CloseHandle(static_cast<HANDLE>(handle));  // dernier handle fermé : l'objet disparaît
        handle = nullptr;
        return;
    }
    wchar_t text[96];
    name(text, what, GetCurrentProcessId());
    handle = CreateEventW(nullptr, TRUE, FALSE, text);  // échec sans conséquence : XRPerfVR croira la layer absente
}

bool exists(const wchar_t* what, uint32_t pid) {
    if (pid == 0) return false;
    wchar_t text[96];
    name(text, what, pid);
    HANDLE handle = OpenEventW(SYNCHRONIZE, FALSE, text);
    // Jeu lancé en administrateur : l'objet existe mais n'est pas accessible depuis XRPerfVR. Il
    // compte quand même comme présent (sinon le jeu aurait deux panneaux et deux rapports).
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;
    CloseHandle(handle);
    return true;
}

}  // namespace

void LayerPresence::set(bool measuring, bool displaying) {
    toggle(m_measuring, measuring, L"Mesure");
    toggle(m_displaying, displaying, L"Affichage");
}

bool LayerMeasuresIn(uint32_t pid) { return exists(L"Mesure", pid); }
bool LayerDisplaysIn(uint32_t pid) { return exists(L"Affichage", pid); }

}  // namespace xrpo
