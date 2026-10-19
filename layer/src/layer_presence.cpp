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

// Mémoire du témoin : deux valeurs de 32 bits, état puis durée en secondes (une lecture à cheval sur
// une écriture ne fausse qu'un affichage).
struct RecordShare {
    volatile LONG state;
    volatile float seconds;
};

void LayerPresence::set(bool measuring, bool displaying) {
    toggle(m_measuring, measuring, L"Mesure");
    toggle(m_displaying, displaying, L"Affichage");
    if (!measuring && m_recordMapping) {
        UnmapViewOfFile(m_recordView);
        CloseHandle(static_cast<HANDLE>(m_recordMapping));
        m_recordView = m_recordMapping = nullptr;
    }
}

void LayerPresence::setRecord(int state, float seconds) {
    if (!m_recordMapping) {
        if (state == 0) return;  // rien à signaler : pas de mémoire créée
        wchar_t text[96];
        name(text, L"Enregistrement", GetCurrentProcessId());
        const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(RecordShare), text);
        if (!mapping) return;
        void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(RecordShare));
        if (!view) {
            CloseHandle(mapping);
            return;
        }
        m_recordMapping = mapping;
        m_recordView = view;
    }
    auto* share = static_cast<RecordShare*>(m_recordView);
    share->seconds = seconds;
    share->state = state;
}

bool LayerMeasuresIn(uint32_t pid) { return exists(L"Mesure", pid); }
bool LayerDisplaysIn(uint32_t pid) { return exists(L"Affichage", pid); }

bool LayerRecordIn(uint32_t pid, int& state, float& seconds) {
    if (pid == 0) return false;
    wchar_t text[96];
    name(text, L"Enregistrement", pid);
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, text);
    if (!mapping) return false;
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(RecordShare));
    bool ok = false;
    if (view) {
        const auto* share = static_cast<const RecordShare*>(view);
        state = static_cast<int>(share->state);
        seconds = share->seconds;
        ok = true;
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);
    return ok;
}

}  // namespace xrpo
