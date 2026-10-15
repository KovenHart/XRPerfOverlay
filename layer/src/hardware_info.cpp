#include "hardware_info.h"

#include <dxgi.h>

#include "ascii.h"

namespace xrpo {

namespace {

// Nom d'une carte, lisible (ASCII, sans marques), à partir de sa description DXGI.
std::string AdapterName(const DXGI_ADAPTER_DESC1& desc) {
    return ModelName(AsciiText(desc.Description, sizeof(desc.Description) / sizeof(desc.Description[0]), 96));
}

// Fabrique DXGI, chargée à la demande : la DLL n'ajoute aucune dépendance à dxgi.dll (déjà chargée
// par tout jeu Direct3D, et par le runtime OpenXR dans les autres cas).
bool CreateFactory(Com<IDXGIFactory1>& factory) {
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!dxgi) dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return false;
    using CreateFn = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    return create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()))) && factory;
}

}  // namespace

std::string CpuModelName() {
    static const std::string name = [] {
        wchar_t value[128] = {};
        DWORD size = sizeof(value) - sizeof(wchar_t);  // place gardée pour le zéro final
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ,
                         nullptr, value, &size) != ERROR_SUCCESS)
            return std::string();
        return ModelName(AsciiText(value, sizeof(value) / sizeof(value[0]), 96));
    }();
    return name;
}

std::string GpuNameFromLuid(LUID luid) {
    Com<IDXGIFactory1> factory;
    if (!CreateFactory(factory)) return std::string();
    for (UINT i = 0; i < 16; ++i) {
        Com<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.put()) != S_OK) break;
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) return AdapterName(desc);
    }
    return std::string();
}

std::string MainGpuName() {
    Com<IDXGIFactory1> factory;
    if (!CreateFactory(factory)) return std::string();
    std::string best;
    SIZE_T bestMemory = 0;
    for (UINT i = 0; i < 16; ++i) {
        Com<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.put()) != S_OK) break;
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;  // rendu logiciel de Windows
        if (best.empty() || desc.DedicatedVideoMemory > bestMemory) {
            best = AdapterName(desc);
            bestMemory = desc.DedicatedVideoMemory;
        }
    }
    return best;
}

}  // namespace xrpo
