// XRPerfCapture.dll : capture d'une fenêtre du bureau (un chat, par exemple) pour l'afficher dans le
// casque. Chargée par XRPerfSensors.exe ; l'image est écrite dans la mémoire partagée
// Local\XRPerfOverlay.Window (shared_block.h), d'où la layer et XRPerfVR la lisent.
//
// La capture passe par l'API de capture de Windows (Windows.Graphics.Capture, Windows 10 1903 ou plus
// récent) : celle d'OBS et de Discord. La fenêtre peut être cachée par d'autres, pas réduite. Toute la
// capture se fait dans un fil d'exécution à part, qui possède les objets Windows Runtime et Direct3D ;
// les fonctions exportées ne font que lui transmettre la demande.
//
// L'image est recadrée (réglage), réduite si elle dépasse kWindowMaxWidth x kWindowMaxHeight, rendue
// opaque (la transparence des fenêtres n'est pas fiable), puis l'opacité choisie est appliquée (alpha
// prémultiplié).
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <roapi.h>
#include <winstring.h>

// En-têtes « ABI » du Windows Runtime (sans C++/WinRT) : communs au SDK de Windows et à MinGW.
#if defined(__MINGW32__)
// Dans MinGW, boolean et BYTE sont le même type : la seconde définition de IReference (inutile ici)
// serait refusée.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "shared_block.h"
#include "window_pixels.h"

namespace capture = ABI::Windows::Graphics::Capture;
namespace d3dwinrt = ABI::Windows::Graphics::DirectX::Direct3D11;

extern "C" {

// Réglages de la capture, transmis par XRPerfSensors (WindowCapture.cs : même disposition).
struct XrpcOptions {
    uint32_t size;      // sizeof(XrpcOptions), pour les versions futures
    float fps;          // images par seconde (1 à 15)
    float cropLeft;     // part de la fenêtre retirée de chaque côté, de 0 à 0,45
    float cropTop;
    float cropRight;
    float cropBottom;
    float opacity;      // 0,2 à 1
};

// État de la capture, pour la fenêtre de réglages.
struct XrpcStatus {
    int32_t state;      // XrpcState
    int32_t width;      // taille de la fenêtre capturée (avant recadrage)
    int32_t height;
    uint32_t frames;    // images écrites depuis le début
    int32_t error;      // HRESULT du dernier échec (state = Failed)
};

}  // extern "C"

namespace {

enum XrpcState : int32_t {
    Stopped = 0,
    Starting = 1,
    Capturing = 2,
    Minimized = 3,     // fenêtre réduite : Windows ne la dessine plus, la dernière image reste affichée
    WindowClosed = 4,  // fenêtre fermée (ou introuvable)
    Unsupported = 5,   // API de capture absente (Windows trop ancien)
    Failed = 6,        // échec de la capture de cette fenêtre (nouvel essai toutes les 3 s)
};

// IDirect3DDxgiInterfaceAccess (windows.graphics.directx.direct3d11.interop.h, absent de MinGW).
struct DxgiInterfaceAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};
const IID kDxgiInterfaceAccess = {0xa9b3d012, 0x3df2, 0x4ee3, {0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1}};

// Options de la session de capture, déclarées ici plutôt que prises dans les en-têtes : elles n'existent
// que dans les SDK de Windows récents (curseur : Windows 10 2004 ; bordure : Windows 11), et sont
// simplement ignorées par un Windows qui ne les connaît pas.
struct CaptureSessionCursor : IInspectable {  // IGraphicsCaptureSession2
    virtual HRESULT STDMETHODCALLTYPE get_IsCursorCaptureEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsCursorCaptureEnabled(boolean value) = 0;
};
const IID kCaptureSessionCursor = {0x2c39ae40, 0x7d2e, 0x5044, {0x80, 0x4e, 0x8b, 0x67, 0x99, 0xd4, 0xcf, 0x9e}};
struct CaptureSessionBorder : IInspectable {  // IGraphicsCaptureSession3
    virtual HRESULT STDMETHODCALLTYPE get_IsBorderRequired(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBorderRequired(boolean value) = 0;
};
const IID kCaptureSessionBorder = {0xf2cdd966, 0x22ae, 0x5ea1, {0x95, 0x96, 0x3a, 0x28, 0x93, 0x44, 0xc3, 0xbe}};

typedef HRESULT(WINAPI* PFN_CreateDirect3D11DeviceFromDXGIDevice)(IDXGIDevice*, IInspectable**);

template <typename T>
void SafeRelease(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

template <typename T>
HRESULT Factory(const wchar_t* className, T** out) {
    HSTRING name = nullptr;
    HRESULT hr = WindowsCreateString(className, static_cast<UINT32>(std::wcslen(className)), &name);
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory(name, __uuidof(T), reinterpret_cast<void**>(out));
    WindowsDeleteString(name);
    return hr;
}

// Ferme un objet du Windows Runtime qui le permet (pool d'images, session) avant de le libérer.
void CloseAndRelease(IUnknown* object) {
    if (!object) return;
    ABI::Windows::Foundation::IClosable* closable = nullptr;
    if (SUCCEEDED(object->QueryInterface(__uuidof(ABI::Windows::Foundation::IClosable), reinterpret_cast<void**>(&closable))) && closable) {
        closable->Close();
        closable->Release();
    }
    object->Release();
}

class Capturer {
  public:
    // Fin du programme sans xrpcStop : le fil d'exécution est abandonné (il est arrêté avec le processus).
    ~Capturer() {
        if (m_thread.joinable()) m_thread.detach();
    }

    void start(HWND window, uint8_t* share, const XrpcOptions& options) {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_window = window;
            m_options = options;
            m_share = share;
        }
        if (!m_thread.joinable()) {
            m_stop = false;
            m_status.state = Starting;
            m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            m_thread = std::thread([this] { run(); });
        } else if (m_wake) {
            SetEvent(m_wake);
        }
    }

    void setOptions(const XrpcOptions& options) {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_options = options;
        }
        if (m_wake) SetEvent(m_wake);  // nouveau recadrage ou nouvelle opacité : appliqués tout de suite
    }

    void stop() {
        if (!m_thread.joinable()) return;
        m_stop = true;
        if (m_wake) SetEvent(m_wake);
        m_thread.join();
        if (m_wake) CloseHandle(m_wake);
        m_wake = nullptr;
        std::lock_guard<std::mutex> lock(m_lock);
        m_status = XrpcStatus();
        m_status.state = Stopped;
    }

    XrpcStatus status() {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_status;
    }

  private:
    void setState(int32_t state, HRESULT error = S_OK) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_status.state = state;
        m_status.error = static_cast<int32_t>(error);
    }

    void run() {
        const HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
        HWND current = nullptr;
        double retryAt = 0.0;
        while (!m_stop) {
            HWND wanted;
            XrpcOptions options;
            uint8_t* share;
            {
                std::lock_guard<std::mutex> lock(m_lock);
                wanted = m_window;
                options = m_options;
                share = m_share;
            }
            const double now = static_cast<double>(GetTickCount64()) / 1000.0;
            if (wanted != current) {
                releaseCapture();
                current = wanted;
                retryAt = 0.0;
            }
            if (!m_session && now >= retryAt) {
                const HRESULT hr = createCapture(current);
                if (FAILED(hr)) {
                    retryAt = now + 3.0;
                    if (m_status.state != Unsupported && m_status.state != WindowClosed) setState(Failed, hr);
                }
            }
            if (m_session) {
                if (!IsWindow(current)) {
                    releaseCapture();
                    setState(WindowClosed);
                    retryAt = now + 3.0;
                } else {
                    grab(share, options, IsIconic(current) != 0);
                }
            }
            if (share) reinterpret_cast<volatile WindowShareHeaderView*>(share)->writerTickMs = GetTickCount64();
            const float fps = options.fps >= 1.f && options.fps <= 15.f ? options.fps : 5.f;
            WaitForSingleObject(m_wake, static_cast<DWORD>(1000.f / fps));
        }
        releaseCapture();
        releaseDevice();
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_share) reinterpret_cast<volatile WindowShareHeaderView*>(m_share)->writerTickMs = 0;
        }
        if (SUCCEEDED(init)) RoUninitialize();
    }

    // Même disposition que xrpo::WindowShareHeader (accès volatils).
    using WindowShareHeaderView = xrpo::WindowShareHeader;

    bool ensureDevice() {
        if (m_device && m_device->GetDeviceRemovedReason() != S_OK) releaseDevice();
        if (m_device) return true;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION, &m_device,
                                       nullptr, &m_context);
        if (FAILED(hr)) {
            releaseDevice();
            return false;
        }
        IDXGIDevice* dxgi = nullptr;
        IInspectable* inspectable = nullptr;
        const auto create = reinterpret_cast<PFN_CreateDirect3D11DeviceFromDXGIDevice>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "CreateDirect3D11DeviceFromDXGIDevice")));
        if (create && SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))) &&
            SUCCEEDED(create(dxgi, &inspectable)) && inspectable)
            inspectable->QueryInterface(__uuidof(d3dwinrt::IDirect3DDevice), reinterpret_cast<void**>(&m_winrtDevice));
        SafeRelease(inspectable);
        SafeRelease(dxgi);
        if (!m_winrtDevice) {
            releaseDevice();
            return false;
        }
        return true;
    }

    void releaseDevice() {
        releaseStaging();
        SafeRelease(m_last);
        m_lastWidth = m_lastHeight = 0;
        SafeRelease(m_winrtDevice);
        if (m_context) m_context->ClearState();
        SafeRelease(m_context);
        SafeRelease(m_device);
    }

    // Seulement la texture de lecture : la dernière image gardée (m_last) peut être celle en cours de
    // traitement quand le recadrage change la taille de la zone lue.
    void releaseStaging() {
        SafeRelease(m_staging);
        m_stagingWidth = m_stagingHeight = 0;
    }

    HRESULT createCapture(HWND window) {
        if (!window || !IsWindow(window)) {
            setState(WindowClosed);
            return E_INVALIDARG;
        }
        // Windows 10 1903 ou plus récent.
        capture::IGraphicsCaptureSessionStatics* sessionStatics = nullptr;
        boolean supported = false;
        HRESULT hr = Factory(L"Windows.Graphics.Capture.GraphicsCaptureSession", &sessionStatics);
        if (SUCCEEDED(hr)) hr = sessionStatics->IsSupported(&supported);
        SafeRelease(sessionStatics);
        if (FAILED(hr) || !supported) {
            setState(Unsupported, FAILED(hr) ? hr : E_NOTIMPL);
            return FAILED(hr) ? hr : E_NOTIMPL;
        }
        if (!ensureDevice()) return E_FAIL;

        IGraphicsCaptureItemInterop* interop = nullptr;
        hr = Factory(L"Windows.Graphics.Capture.GraphicsCaptureItem", &interop);
        if (SUCCEEDED(hr)) hr = interop->CreateForWindow(window, __uuidof(capture::IGraphicsCaptureItem), reinterpret_cast<void**>(&m_item));
        SafeRelease(interop);
        if (FAILED(hr) || !m_item) {
            SafeRelease(m_item);
            return FAILED(hr) ? hr : E_FAIL;
        }
        hr = m_item->get_Size(&m_poolSize);
        if (FAILED(hr) || m_poolSize.Width <= 0 || m_poolSize.Height <= 0) m_poolSize = {64, 64};

        capture::IDirect3D11CaptureFramePoolStatics2* poolStatics = nullptr;
        hr = Factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", &poolStatics);
        // « Free threaded » : les images se relèvent sans boucle de messages, depuis ce fil d'exécution.
        if (SUCCEEDED(hr))
            hr = poolStatics->CreateFreeThreaded(m_winrtDevice, ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, m_poolSize,
                                                 &m_pool);
        SafeRelease(poolStatics);
        if (SUCCEEDED(hr)) hr = m_pool->CreateCaptureSession(m_item, &m_session);
        if (FAILED(hr) || !m_session) {
            releaseCapture();
            return FAILED(hr) ? hr : E_FAIL;
        }
        // Sans le curseur de la souris ; sans bordure jaune quand Windows le permet (Windows 11).
        CaptureSessionCursor* cursor = nullptr;
        if (SUCCEEDED(m_session->QueryInterface(kCaptureSessionCursor, reinterpret_cast<void**>(&cursor))) && cursor) cursor->put_IsCursorCaptureEnabled(false);
        SafeRelease(cursor);
        CaptureSessionBorder* border = nullptr;
        if (SUCCEEDED(m_session->QueryInterface(kCaptureSessionBorder, reinterpret_cast<void**>(&border))) && border) border->put_IsBorderRequired(false);
        SafeRelease(border);
        hr = m_session->StartCapture();
        if (FAILED(hr)) {
            releaseCapture();
            return hr;
        }
        std::lock_guard<std::mutex> lock(m_lock);
        m_status.state = Starting;
        m_status.error = 0;
        m_status.width = m_poolSize.Width;
        m_status.height = m_poolSize.Height;
        return S_OK;
    }

    void releaseCapture() {
        SafeRelease(m_last);  // image de la fenêtre précédente
        m_lastWidth = m_lastHeight = 0;
        if (m_session) CloseAndRelease(m_session);
        m_session = nullptr;
        if (m_pool) CloseAndRelease(m_pool);
        m_pool = nullptr;
        SafeRelease(m_item);
    }

    // Dernière image arrivée depuis le passage précédent : recadrée, réduite et écrite dans la mémoire partagée.
    void grab(uint8_t* share, const XrpcOptions& options, bool minimized) {
        // Carte graphique réinitialisée (pilote relancé...) : tout est recréé au passage suivant.
        const HRESULT removed = m_device ? m_device->GetDeviceRemovedReason() : E_FAIL;
        if (removed != S_OK) {
            lostDevice(removed);
            return;
        }
        capture::IDirect3D11CaptureFrame* frame = nullptr;
        for (;;) {
            capture::IDirect3D11CaptureFrame* next = nullptr;
            if (FAILED(m_pool->TryGetNextFrame(&next)) || !next) break;
            if (frame) CloseAndRelease(frame);
            frame = next;
        }
        if (!frame) {
            // Fenêtre inchangée (Windows n'envoie une image que quand elle change) : la dernière est refaite
            // si le recadrage ou l'opacité ont changé depuis.
            if (m_last && share && !SameLook(options, m_applied)) {
                const HRESULT hr = process(m_last, m_lastWidth, m_lastHeight, share, options);
                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) lostDevice(hr);
            }
            if (minimized) setState(Minimized);
            return;
        }
        ABI::Windows::Graphics::SizeInt32 content = {0, 0};
        frame->get_ContentSize(&content);
        d3dwinrt::IDirect3DSurface* surface = nullptr;
        DxgiInterfaceAccess* access = nullptr;
        ID3D11Texture2D* texture = nullptr;
        if (SUCCEEDED(frame->get_Surface(&surface)) && surface &&
            SUCCEEDED(surface->QueryInterface(kDxgiInterfaceAccess, reinterpret_cast<void**>(&access))) && access)
            access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        HRESULT hr = S_OK;
        if (texture && share && keep(texture, content)) hr = process(m_last, m_lastWidth, m_lastHeight, share, options);
        SafeRelease(texture);
        SafeRelease(access);
        SafeRelease(surface);
        CloseAndRelease(frame);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            lostDevice(hr);
            return;
        }

        // Fenêtre redimensionnée : les images suivantes prennent sa nouvelle taille.
        if (content.Width > 0 && content.Height > 0 && (content.Width != m_poolSize.Width || content.Height != m_poolSize.Height)) {
            m_poolSize = content;
            m_pool->Recreate(m_winrtDevice, ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, m_poolSize);
        }
        std::lock_guard<std::mutex> lock(m_lock);
        m_status.state = minimized ? Minimized : Capturing;
        m_status.width = content.Width;
        m_status.height = content.Height;
    }

    static bool SameLook(const XrpcOptions& a, const XrpcOptions& b) {
        return a.cropLeft == b.cropLeft && a.cropTop == b.cropTop && a.cropRight == b.cropRight && a.cropBottom == b.cropBottom && a.opacity == b.opacity;
    }

    void lostDevice(HRESULT reason) {
        releaseCapture();
        releaseDevice();
        setState(Failed, reason);
    }

    // Copie de la partie dessinée de l'image reçue, gardée pour la refaire si les réglages changent.
    bool keep(ID3D11Texture2D* texture, ABI::Windows::Graphics::SizeInt32 content) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        const int width = std::min<int>(content.Width > 0 ? content.Width : static_cast<int>(desc.Width), static_cast<int>(desc.Width));
        const int height = std::min<int>(content.Height > 0 ? content.Height : static_cast<int>(desc.Height), static_cast<int>(desc.Height));
        if (width <= 0 || height <= 0) return false;
        if (!m_last || m_lastWidth != width || m_lastHeight != height) {
            SafeRelease(m_last);
            D3D11_TEXTURE2D_DESC copy = {};
            copy.Width = static_cast<UINT>(width);
            copy.Height = static_cast<UINT>(height);
            copy.MipLevels = 1;
            copy.ArraySize = 1;
            copy.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            copy.SampleDesc.Count = 1;
            copy.Usage = D3D11_USAGE_DEFAULT;
            if (FAILED(m_device->CreateTexture2D(&copy, nullptr, &m_last))) {
                m_last = nullptr;
                return false;
            }
            m_lastWidth = width;
            m_lastHeight = height;
        }
        const D3D11_BOX box = {0, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height), 1};
        m_context->CopySubresourceRegion(m_last, 0, 0, 0, 0, texture, 0, &box);
        return true;
    }

    HRESULT process(ID3D11Texture2D* texture, int fullWidth, int fullHeight, uint8_t* share, const XrpcOptions& options) {
        if (fullWidth <= 0 || fullHeight <= 0) return S_OK;

        // Recadrage (réglage), au moins 16 pixels gardés.
        const xrpo::CropRect crop = xrpo::WindowCrop(fullWidth, fullHeight, options.cropLeft, options.cropTop, options.cropRight, options.cropBottom);
        const int left = crop.left, top = crop.top, right = crop.right, bottom = crop.bottom;
        const int width = crop.width(), height = crop.height();

        if (!m_staging || m_stagingWidth != width || m_stagingHeight != height) {
            releaseStaging();
            D3D11_TEXTURE2D_DESC staging = {};
            staging.Width = static_cast<UINT>(width);
            staging.Height = static_cast<UINT>(height);
            staging.MipLevels = 1;
            staging.ArraySize = 1;
            staging.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            staging.SampleDesc.Count = 1;
            staging.Usage = D3D11_USAGE_STAGING;
            staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(m_device->CreateTexture2D(&staging, nullptr, &m_staging))) {
                m_staging = nullptr;
                return E_FAIL;
            }
            m_stagingWidth = width;
            m_stagingHeight = height;
        }
        const D3D11_BOX box = {static_cast<UINT>(left), static_cast<UINT>(top), 0, static_cast<UINT>(right), static_cast<UINT>(bottom), 1};
        m_context->CopySubresourceRegion(m_staging, 0, 0, 0, 0, texture, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT mappedResult = m_context->Map(m_staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(mappedResult) || !mapped.pData) return FAILED(mappedResult) ? mappedResult : E_FAIL;

        // Taille écrite : réduite (proportions gardées) si l'image dépasse la place prévue.
        int outWidth = 0, outHeight = 0;
        xrpo::WindowOutputSize(width, height, xrpo::kWindowMaxWidth, xrpo::kWindowMaxHeight, outWidth, outHeight);
        float opacity = options.opacity;
        if (!(opacity >= 0.2f)) opacity = opacity >= 0.f ? 0.2f : 1.f;
        if (opacity > 1.f) opacity = 1.f;
        const uint32_t alpha = static_cast<uint32_t>(std::lround(opacity * 255.f));

        volatile xrpo::WindowShareHeader* header = reinterpret_cast<volatile xrpo::WindowShareHeader*>(share);
        uint8_t* pixels = share + sizeof(xrpo::WindowShareHeader);
        // Séquence impaire pendant l'écriture : le lecteur sait qu'il doit relire.
        const uint32_t sequence = header->sequence | 1u;
        header->sequence = sequence;
        MemoryBarrier();
        xrpo::DownscaleWindowImage(static_cast<const uint8_t*>(mapped.pData), static_cast<int>(mapped.RowPitch), width, height, pixels, outWidth, outHeight, alpha);
        header->width = static_cast<uint32_t>(outWidth);
        header->height = static_cast<uint32_t>(outHeight);
        header->frame = header->frame + 1u;
        MemoryBarrier();
        header->sequence = sequence + 1u;
        m_context->Unmap(m_staging, 0);
        m_applied = options;
        std::lock_guard<std::mutex> lock(m_lock);
        ++m_status.frames;
        return S_OK;
    }

    std::mutex m_lock;  // demande et état, partagés avec les fonctions exportées
    HWND m_window = nullptr;
    XrpcOptions m_options = {sizeof(XrpcOptions), 5.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    uint8_t* m_share = nullptr;
    XrpcStatus m_status = {};

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    HANDLE m_wake = nullptr;

    // Objets du fil de capture.
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    d3dwinrt::IDirect3DDevice* m_winrtDevice = nullptr;
    capture::IGraphicsCaptureItem* m_item = nullptr;
    capture::IDirect3D11CaptureFramePool* m_pool = nullptr;
    capture::IGraphicsCaptureSession* m_session = nullptr;
    ABI::Windows::Graphics::SizeInt32 m_poolSize = {0, 0};
    ID3D11Texture2D* m_staging = nullptr;
    int m_stagingWidth = 0, m_stagingHeight = 0;
    ID3D11Texture2D* m_last = nullptr;  // dernière image reçue (partie dessinée), sur la carte
    int m_lastWidth = 0, m_lastHeight = 0;
    XrpcOptions m_applied = {};          // réglages de la dernière image écrite
};

Capturer g_capturer;
std::mutex g_apiLock;  // les fonctions exportées peuvent être appelées de plusieurs fils

bool ValidOptions(const XrpcOptions* options) { return options && options->size >= sizeof(XrpcOptions); }

}  // namespace

extern "C" {

__declspec(dllexport) int xrpcVersion() { return 1; }

// Commence (ou change de fenêtre) la capture. `share` : vue de la mémoire Local\XRPerfOverlay.Window,
// créée par l'appelant, d'au moins `shareBytes` octets ; elle doit rester valide jusqu'à xrpcStop.
// Retourne 0, ou -1 si les paramètres ne conviennent pas.
__declspec(dllexport) int xrpcStart(HWND window, void* share, uint64_t shareBytes, const XrpcOptions* options) {
    if (!window || !share || shareBytes < xrpo::kWindowShareBytes || !ValidOptions(options)) return -1;
    std::lock_guard<std::mutex> lock(g_apiLock);
    g_capturer.start(window, static_cast<uint8_t*>(share), *options);
    return 0;
}

__declspec(dllexport) void xrpcSetOptions(const XrpcOptions* options) {
    if (!ValidOptions(options)) return;
    std::lock_guard<std::mutex> lock(g_apiLock);
    g_capturer.setOptions(*options);
}

// Arrête la capture et attend la fin de son fil d'exécution (la mémoire partagée n'est plus touchée).
__declspec(dllexport) void xrpcStop() {
    std::lock_guard<std::mutex> lock(g_apiLock);
    g_capturer.stop();
}

__declspec(dllexport) void xrpcGetStatus(XrpcStatus* out) {
    if (!out) return;
    std::lock_guard<std::mutex> lock(g_apiLock);
    *out = g_capturer.status();
}

}  // extern "C"
