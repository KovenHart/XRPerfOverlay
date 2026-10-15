// VrBackend réel : SteamVR par openvr_api.dll (chargée à la connexion depuis le dossier du programme)
// et Windows (processus, présence de la layer).
#pragma once

#include <memory>
#include <string>

#include "vr_backend.h"

namespace xrpo {

// `dllFolder` : dossier où se trouve openvr_api.dll (celui de XRPerfVR.exe).
std::unique_ptr<VrBackend> CreateOpenVrBackend(const std::wstring& dllFolder);

}  // namespace xrpo
