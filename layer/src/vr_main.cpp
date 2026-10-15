// XRPerfVR.exe : panneau, montre et rapports de XR Perf Overlay dans les jeux SteamVR (OpenVR).
// Lancé et arrêté par XRPerfSensors ; sans fenêtre. Voir vr_host.h.
//
//   XRPerfVR.exe [--parent <pid>] [--after-quit]
//     --parent     : s'arrête proprement quand ce processus (XRPerfSensors) se termine.
//     --after-quit : relancé juste après une fermeture de SteamVR : attendre d'abord qu'il soit parti.
// Arrêt demandé : événement nommé Local\XRPerfOverlay.VR.Stop.
// Code de sortie 2 : SteamVR s'est fermé et a demandé aux applications de quitter (à relancer avec
// --after-quit) ; 0 : arrêt demandé.
#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "log.h"
#include "openvr_backend.h"
#include "version.h"
#include "vr_host.h"

namespace {

std::wstring ExeFolder() {
    std::vector<wchar_t> path(32768);
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    std::wstring folder(path.data(), n < path.size() ? n : 0);
    const size_t slash = folder.find_last_of(L"\\/");
    if (slash != std::wstring::npos) folder.erase(slash);
    return folder;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace xrpo;

    // Une seule instance par session Windows.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\XRPerfOverlay.VR");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, L"Local\\XRPerfOverlay.VR.Stop");
    if (stop) ResetEvent(stop);  // signal d'arrêt resté d'une instance précédente

    // Processus parent à surveiller.
    HANDLE parent = nullptr;
    bool afterQuit = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--parent") == 0 && i + 1 < argc) {
            const unsigned long pid = std::wcstoul(argv[i + 1], nullptr, 10);
            if (pid != 0) parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
        }
        if (std::wcscmp(argv[i], L"--after-quit") == 0) afterQuit = true;
    }
    if (argv) LocalFree(argv);

    // L'installation range ce programme dans <dossier>\sensors, à côté de XRPerfSensors ; le .ini de
    // la layer est dans le dossier au-dessus.
    const std::wstring exeFolder = ExeFolder();
    std::wstring iniFolder = exeFolder;
    const size_t slash = exeFolder.find_last_of(L"\\/");
    if (slash != std::wstring::npos && FileExists(exeFolder.substr(0, slash) + L"\\XRPerfOverlay.ini")) iniFolder = exeFolder.substr(0, slash);

    // Le jeu passe avant : ce programme ne fait presque rien, mais jamais au détriment de l'image.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    Log("XRPerfVR %s demarre : attente de SteamVR (jeux OpenVR).", XRPO_VERSION);

    int exitCode = 0;
    {
        std::unique_ptr<VrBackend> backend = CreateOpenVrBackend(exeFolder);
        VrHost host(*backend, iniFolder, afterQuit);
        HANDLE waits[2];
        DWORD waitCount = 0;
        if (stop) waits[waitCount++] = stop;
        if (parent) waits[waitCount++] = parent;
        for (;;) {
            host.tick();
            // SteamVR a demandé aux applications de quitter et le programme s'y est engagé : il se termine,
            // XRPerfSensors le relance aussitôt en attente du prochain lancement de SteamVR.
            if (host.quitAcknowledged()) {
                exitCode = 2;
                break;
            }
            // Pause d'environ 20 ms, interrompue par une demande d'arrêt ou la fin de XRPerfSensors.
            const DWORD result = waitCount ? WaitForMultipleObjects(waitCount, waits, FALSE, 20) : (Sleep(20), WAIT_TIMEOUT);
            if (result != WAIT_TIMEOUT) break;
        }
        host.stop();
    }
    Log(exitCode == 2 ? "XRPerfVR arrete (fermeture de SteamVR)." : "XRPerfVR arrete.");
    if (parent) CloseHandle(parent);
    if (stop) CloseHandle(stop);
    ReleaseMutex(single);
    CloseHandle(single);
    return exitCode;
}
