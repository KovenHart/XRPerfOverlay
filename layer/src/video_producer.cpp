#include "video_producer.h"

#include <d3d11_4.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstring>
#include <string>

#include "config.h"
#include "log.h"
#include "xr.h"

namespace xrpo {

namespace {

// Numéro des textures partagées, propre à ce processus : deux sessions de suite (ou deux fournisseurs)
// n'ont jamais les mêmes noms, même si l'enregistreur tient encore les anciennes textures ouvertes.
std::atomic<uint32_t> g_generation{0};

uint32_t NextGeneration() { return ++g_generation; }

void Barrier() { MemoryBarrier(); }

}  // namespace

bool VideoFormatIsLinear(int64_t format) {
    switch (static_cast<DXGI_FORMAT>(format)) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return false;
        default:
            return true;
    }
}

// --- Partie commune : bloc partagé, rôle de fournisseur, publication -----------------------------------

VideoProducer::VideoProducer(uint32_t kind) : m_kind(kind) {}

VideoProducer::~VideoProducer() {
    // Les classes dérivées libèrent leurs textures dans leur destructeur ; ici, le bloc seulement.
    if (m_block && m_producing) {
        const LONG pid = static_cast<LONG>(GetCurrentProcessId());
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&m_block->producerPid), 0, pid);
    }
    if (m_block) UnmapViewOfFile(const_cast<VideoShareHeader*>(m_block));
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
}

bool VideoProducer::openBlock() {
    const unsigned long long now = GetTickCount64();
    if (m_lastOpenAttempt != 0 && now - m_lastOpenAttempt < 2000) return false;  // une tentative toutes les 2 s au plus
    m_lastOpenAttempt = now;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kVideoShareName);
    if (!mapping) return false;
    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kVideoShareBytes);
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    volatile VideoShareHeader* block = static_cast<volatile VideoShareHeader*>(view);
    if (block->magic != kVideoShareMagic || block->version != kVideoShareVersion) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        return false;
    }
    m_mapping = mapping;
    m_block = block;
    return true;
}

void VideoProducer::writeGame() {
    if (!m_block) return;
    std::wstring name = m_game.empty() ? CurrentExeName(false) : m_game;
    if (name.size() > 4 && (name.compare(name.size() - 4, 4, L".exe") == 0 || name.compare(name.size() - 4, 4, L".EXE") == 0))
        name.resize(name.size() - 4);
    const size_t n = name.size() < 63 ? name.size() : 63;
    for (size_t i = 0; i < n; ++i) m_block->game[i] = static_cast<uint16_t>(name[i]);
    m_block->game[n] = 0;
}

void VideoProducer::setGame(const std::wstring& name) {
    if (name == m_game) return;
    m_game = name;
    if (m_producing) writeGame();
}

void VideoProducer::release(bool keepBlock) {
    if (m_block && m_producing) {
        // Plus d'image valide : l'enregistreur ne doit plus ouvrir ces textures.
        m_block->latest = 0;
        Barrier();
        const LONG pid = static_cast<LONG>(GetCurrentProcessId());
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&m_block->producerPid), 0, pid);
    }
    m_producing = false;
    m_latest = m_pending = -1;
    if (m_hasResources) {
        freeResources();
        m_hasResources = false;
    }
    if (!keepBlock && m_block) {
        UnmapViewOfFile(const_cast<VideoShareHeader*>(m_block));
        m_block = nullptr;
        if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
    }
}

void VideoProducer::shutdown() { release(false); }

// Textures de l'œil et des écrans plats libérées (l'enregistreur ne doit plus les ouvrir).
void VideoProducer::freeResources() {
    releaseResources();
    for (int k = 0; k < kVideoLayers; ++k) {
        m_layer[k] = LayerTextures();
        if (m_block) m_block->layerTextures[k].generation = 0;
    }
    m_pendingLayerCount = 0;
}

// Abandon pour la session : le rôle est rendu tout de suite, les textures plus tard (sans attendre).
void VideoProducer::fail() {
    m_failed = true;
    if (m_block && m_producing) {
        m_block->latest = 0;
        Barrier();
        const LONG pid = static_cast<LONG>(GetCurrentProcessId());
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&m_block->producerPid), 0, pid);
    }
    m_producing = false;
    m_latest = m_pending = -1;
}

bool VideoProducer::active(int& eye) {
    if (m_failed) {
        // Échec : textures libérées dès que la carte graphique n'y copie plus (jamais d'attente ici).
        if (m_hasResources && idle()) {
            freeResources();
            m_hasResources = false;
        }
        return false;
    }
    if (!m_block && !openBlock()) return false;
    volatile VideoShareHeader* b = m_block;
    const unsigned long long tick = GetTickCount64();
    const bool recording = b->wanted == 1 && VideoAge(tick, b->recorderTickMs) < 3000;
    if (!recording) {
        if (m_producing) {
            // Le rôle est rendu tout de suite ; les textures, dès que la carte graphique n'y copie plus.
            b->latest = 0;
            Barrier();
            const LONG pid = static_cast<LONG>(GetCurrentProcessId());
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&b->producerPid), 0, pid);
            m_producing = false;
            m_latest = m_pending = -1;
            Log("Video : enregistrement termine.");
        }
        if (m_hasResources && idle()) {
            freeResources();
            m_hasResources = false;
        }
        return false;
    }
    const uint32_t pid = GetCurrentProcessId();
    if (!m_producing || b->producerPid != pid) {
        const uint32_t current = b->producerPid;
        if (current != pid) {
            if (current != 0 && VideoAge(tick, b->producerTickMs) < 2000) return false;  // un autre fournit déjà les images
            if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&b->producerPid), static_cast<LONG>(pid), static_cast<LONG>(current)) !=
                static_cast<LONG>(current))
                return false;
        }
        m_producing = true;
        m_latest = m_pending = -1;
        m_throttle.reset();
        b->producerKind = m_kind;
        b->latest = 0;
        if (m_hasResources) {
            // Textures d'un enregistrement précédent : l'enregistreur les retrouve sous le même nom.
            b->width = m_width;
            b->height = m_height;
            b->format = m_format;
            b->adapterLuid = adapterLuid();
            b->generation = m_generation;
        }
        writeGame();
        Log("Video : enregistrement demande, ce processus fournit les images.");
    }
    if (b->session != m_session) {
        m_session = b->session;
        m_throttle.reset();
    }
    b->producerTickMs = tick;
    eye = b->eye == 1 ? 1 : 0;
    return true;
}

bool VideoProducer::due(double now) {
    if (!m_producing || !m_block) return false;
    const float fps = m_block->fps;
    return m_throttle.take(now, fps > 0.f && fps <= 240.f ? fps : 60.f);
}

bool VideoProducer::copy(const VideoSource& source) {
    if (m_failed || !m_producing || !m_block || !source.texture || source.width == 0 || source.height == 0) return false;
    volatile VideoShareHeader* b = m_block;
    const uint32_t format = sourceFormat(source);
    // Textures à la taille de la zone copiée (l'œil enregistré, pas toute l'image du jeu) ; elles ne font
    // que grandir pendant une session : une résolution dynamique qui baisse ne les recrée pas.
    const bool sameFormat = m_hasResources && format == m_format;
    if (!sameFormat || source.width > m_width || source.height > m_height) {
        VideoSource sized = source;
        if (sameFormat) {
            sized.width = source.width > m_width ? source.width : m_width;
            sized.height = source.height > m_height ? source.height : m_height;
        }
        if (m_hasResources) {
            if (!idle()) return false;  // copies encore en cours : on recrée à une image suivante, sans attendre
            b->latest = 0;
            Barrier();
            freeResources();
            m_hasResources = false;
            m_latest = m_pending = -1;
        }
        const uint32_t generation = NextGeneration();
        uint32_t created = 0;
        if (!createResources(sized, generation, created)) {
            m_hasResources = true;  // libérées plus tard, sans attendre (active)
            fail();
            return false;
        }
        m_hasResources = true;
        m_generation = generation;
        m_width = sized.width;
        m_height = sized.height;
        m_format = format;
        b->width = sized.width;
        b->height = sized.height;
        b->format = created;
        b->adapterLuid = adapterLuid();
        Barrier();
        b->generation = generation;
        Barrier();
        Log("Video : textures partagees %ux%u (format DXGI %u).", sized.width, sized.height, created);
    }

    // Texture libre : ni la dernière publiée (l'enregistreur peut la prendre), ni une qu'il lit encore.
    // Une copie pas encore publiée garde sa texture (elle n'est ni la dernière ni lue).
    int slot = -1;
    for (int k = 0; k <= kVideoSlots && slot < 0; ++k) {
        const int candidate = k == 0 ? m_pending : (m_latest + k + kVideoSlots) % kVideoSlots;
        if (candidate < 0 || candidate == m_latest) continue;
        Barrier();
        if (b->slotBusy[candidate] != 0 || !slotReady(candidate)) continue;
        slot = candidate;
    }
    m_pending = -1;
    if (slot < 0) return false;  // enregistreur en retard : cette image est sautée

    const uint64_t value = ++m_frame;
    if (!copyTo(slot, source, value)) {
        if (++m_errors >= 3) {
            Log("Video : copies en echec, enregistrement abandonne pour cette session.");
            fail();
        }
        return false;
    }
    m_errors = 0;
    m_pending = slot;
    m_pendingFrame = value;
    return true;
}

void VideoProducer::copyLayers(const VideoLayerSource* layers, int count) {
    m_pendingLayerCount = 0;
    if (m_failed || !m_producing || !m_block || m_pending < 0 || !layers || count <= 0) return;
    volatile VideoShareHeader* b = m_block;
    VideoSource sources[kVideoLayers];
    int indices[kVideoLayers];
    int copies = 0;
    for (int i = 0; i < count && i < kVideoLayers; ++i) {
        const VideoLayerSource& l = layers[i];
        const VideoSource& source = l.source;
        if (!source.texture || source.width == 0 || source.height == 0) continue;
        LayerTextures& t = m_layer[i];
        const uint32_t format = sourceFormat(source);
        const uint32_t viewFormat = static_cast<uint32_t>(l.viewFormat);
        // Textures de cet écran : grandissent seulement, recréées si le format change (sans attendre : un
        // écran dont les textures sont encore en copie est omis à cette image).
        const bool same = t.generation != 0 && t.format == format;
        if (!same || source.width > t.width || source.height > t.height) {
            if (t.generation != 0) {
                if (!idle()) continue;
                b->layerTextures[i].generation = 0;
                Barrier();
                releaseLayerResources(i);
                t = LayerTextures();
            }
            VideoSource sized = source;
            if (same) {
                sized.width = source.width > t.width ? source.width : t.width;
                sized.height = source.height > t.height ? source.height : t.height;
            }
            const uint32_t generation = NextGeneration();
            uint32_t created = 0;
            if (!createLayerResources(i, sized, generation, created)) {
                releaseLayerResources(i);
                continue;  // cet écran seulement (l'image de l'œil reste)
            }
            t.generation = generation;
            t.width = sized.width;
            t.height = sized.height;
            t.format = format;
            volatile VideoLayerTexture& d = b->layerTextures[i];
            d.width = sized.width;
            d.height = sized.height;
            d.format = created;
            Barrier();
            d.generation = generation;
            Log("Video : ecran plat %d du jeu, textures partagees %ux%u (format DXGI %u).", i, sized.width, sized.height, created);
        }
        t.viewFormat = viewFormat;
        b->layerTextures[i].viewFormat = viewFormat;
        sources[copies] = source;
        indices[copies] = i;
        VideoLayerInfo& info = m_pendingLayers[i];
        info.generation = t.generation;
        info.flags = l.flags;
        info.width = source.width;
        info.height = source.height;
        for (int c = 0; c < 12; ++c) info.corners[c] = l.corners[c];
        ++copies;
    }
    if (copies == 0) return;
    const uint64_t value = ++m_frame;
    if (!copyLayersTo(m_pending, sources, indices, copies, value)) return;
    // L'enregistreur attend ce numéro : l'image de l'œil (copiée avant, sur la même file) et les écrans.
    m_pendingFrame = value;
    m_pendingLayerCount = kVideoLayers;
    for (int i = 0; i < kVideoLayers; ++i) {
        bool used = false;
        for (int c = 0; c < copies; ++c) used = used || indices[c] == i;
        if (!used) m_pendingLayers[i].generation = 0;
    }
}

void VideoProducer::publish(const VideoFrameInfo& info) {
    const int slot = m_pending;
    m_pending = -1;
    const int layerCount = m_pendingLayerCount;
    m_pendingLayerCount = 0;
    if (slot < 0 || !m_producing || !m_block || info.width == 0 || info.height == 0) return;
    volatile VideoShareHeader* b = m_block;
    for (int i = 0; i < kVideoLayers; ++i) {
        volatile VideoLayerInfo& d = b->layers[slot][i];
        if (layerCount == 0 || m_pendingLayers[i].generation == 0) {
            d.generation = 0;
            continue;
        }
        const VideoLayerInfo& l = m_pendingLayers[i];
        d.flags = l.flags;
        d.width = l.width;
        d.height = l.height;
        for (int c = 0; c < 12; ++c) d.corners[c] = l.corners[c];
        d.generation = l.generation;
    }
    volatile VideoSlotInfo& s = b->slots[slot];
    const uint32_t x = info.x < m_width ? info.x : 0, y = info.y < m_height ? info.y : 0;
    s.rectX = x;
    s.rectY = y;
    s.rectWidth = info.width <= m_width - x ? info.width : m_width - x;
    s.rectHeight = info.height <= m_height - y ? info.height : m_height - y;
    s.tanLeft = info.tanLeft;
    s.tanRight = info.tanRight;
    s.tanUp = info.tanUp;
    s.tanDown = info.tanDown;
    s.centerX = info.centerX;
    s.centerY = info.centerY;
    s.linear = info.linear ? 1u : 0u;
    s.panelFlags = info.panelFlags;
    for (int i = 0; i < 12; ++i) s.panelCorners[i] = info.panelCorners[i];
    const float eye[4] = {info.eyeOrientation.x, info.eyeOrientation.y, info.eyeOrientation.z, info.eyeOrientation.w};
    const float head[4] = {info.headOrientation.x, info.headOrientation.y, info.headOrientation.z, info.headOrientation.w};
    for (int i = 0; i < 4; ++i) {
        s.eyeOrientation[i] = eye[i];
        s.headOrientation[i] = head[i];
    }
    s.poseValid = info.poseValid ? 1u : 0u;
    s.imageState = info.imageState;
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    b->latestQpc = static_cast<uint64_t>(qpc.QuadPart);
    Barrier();
    b->latest = VideoLatest(m_pendingFrame, slot);  // une seule écriture : numéro et texture ensemble
    m_latest = slot;
}

uint32_t VideoProducer::overlayMode() const {
    if (!m_producing || !m_block || m_kind == kVideoSteamVr) return kVideoOverlayOff;  // miroir SteamVR : panneau déjà dans l'image
    const uint32_t mode = m_block->overlay;
    return mode == kVideoOverlayPlace || mode == kVideoOverlayCorner ? mode : kVideoOverlayOff;
}

void VideoProducer::publishPanel(const uint8_t* pixels, int width, int height, int stride) {
    if (!m_producing || !m_block || !pixels || width <= 0 || height <= 0 || stride < width) return;
    if (width > kVideoPanelMaxWidth) width = kVideoPanelMaxWidth;
    if (height > kVideoPanelMaxHeight) height = kVideoPanelMaxHeight;
    volatile VideoShareHeader* b = m_block;
    // Lecture sans verrou de l'autre côté : numéro impair pendant l'écriture, l'enregistreur relit s'il a changé.
    const uint32_t sequence = b->panelSequence;
    b->panelSequence = sequence | 1u;
    Barrier();
    uint8_t* out = reinterpret_cast<uint8_t*>(const_cast<VideoShareHeader*>(b)) + kVideoPanelOffset;
    for (int row = 0; row < height; ++row)
        std::memcpy(out + static_cast<size_t>(row) * kVideoPanelMaxWidth * 4, pixels + static_cast<size_t>(row) * stride * 4, static_cast<size_t>(width) * 4);
    b->panelWidth = static_cast<uint32_t>(width);
    b->panelHeight = static_cast<uint32_t>(height);
    Barrier();
    b->panelSequence = (sequence | 1u) + 1u;
}

double VideoProducer::recordingSeconds() {
    if (!m_block && !openBlock()) return -1.0;
    volatile VideoShareHeader* b = m_block;
    const unsigned long long tick = GetTickCount64();
    const unsigned long long start = b->recordStartTickMs;
    const int64_t elapsed = VideoAge(tick, start);
    if (b->wanted != 1 || start == 0 || VideoAge(tick, b->recorderTickMs) >= 3000 || elapsed < 0) return -1.0;
    return static_cast<double>(elapsed) / 1000.0;
}

// --- Direct3D 11 (jeu en Direct3D 11, ou XRPerfVR) -----------------------------------------------------

namespace {

class VideoProducerD3D11 final : public VideoProducer {
  public:
    VideoProducerD3D11(ID3D11Device* device, uint32_t kind) : VideoProducer(kind) {
        m_device.share(device);
        if (device) device->GetImmediateContext(m_context.put());
        if (device) device->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(m_device5.put()));
        if (m_context) m_context->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(m_context4.put()));
        if (!m_device5 || !m_context4) {
            Log("Video : Direct3D 11.4 indisponible (fences), pas d'enregistrement video dans ce jeu.");
            m_failed = true;
        }
    }

    ~VideoProducerD3D11() override { releaseResources(); }

  protected:
    uint32_t sourceFormat(const VideoSource& source) override {
        D3D11_TEXTURE2D_DESC desc = {};
        static_cast<ID3D11Texture2D*>(source.texture)->GetDesc(&desc);
        return static_cast<uint32_t>(desc.Format);
    }

    bool createResources(const VideoSource& source, uint32_t generation, uint32_t& format) override {
        D3D11_TEXTURE2D_DESC sourceDesc = {};
        static_cast<ID3D11Texture2D*>(source.texture)->GetDesc(&sourceDesc);
        if (sourceDesc.SampleDesc.Count > 1) {
            Log("Video : images du jeu multi-echantillonnees (MSAA), non prises en charge.");
            return false;
        }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = source.width;
        desc.Height = source.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = sourceDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        const DWORD pid = GetCurrentProcessId();
        wchar_t name[96];
        for (int i = 0; i < kVideoSlots; ++i) {
            HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_textures[i].put());
            Com<IDXGIResource1> resource;
            if (SUCCEEDED(hr)) hr = m_textures[i]->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(resource.put()));
            VideoTextureName(name, 96, pid, generation, i);
            if (SUCCEEDED(hr))
                hr = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, name, &m_handles[i]);
            if (FAILED(hr)) {
                Log("Video : texture partagee impossible (format DXGI %u, erreur 0x%08lx).", static_cast<unsigned>(desc.Format),
                    static_cast<unsigned long>(hr));
                return false;
            }
        }
        HRESULT hr = m_device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), reinterpret_cast<void**>(m_fence.put()));
        VideoTextureName(name, 96, pid, generation, -1);
        if (SUCCEEDED(hr)) hr = m_fence->CreateSharedHandle(nullptr, GENERIC_ALL, name, &m_fenceHandle);
        if (FAILED(hr)) {
            Log("Video : fence partage impossible (erreur 0x%08lx).", static_cast<unsigned long>(hr));
            return false;
        }
        format = static_cast<uint32_t>(desc.Format);
        return true;
    }

    void releaseResources() override {
        for (int i = 0; i < kVideoSlots; ++i) {
            m_textures[i].reset();
            if (m_handles[i]) CloseHandle(m_handles[i]);
            m_handles[i] = nullptr;
        }
        for (int k = 0; k < kVideoLayers; ++k) releaseLayerResources(k);
        m_fence.reset();
        if (m_fenceHandle) CloseHandle(m_fenceHandle);
        m_fenceHandle = nullptr;
    }

    bool createLayerResources(int layer, const VideoSource& source, uint32_t generation, uint32_t& format) override {
        D3D11_TEXTURE2D_DESC sourceDesc = {};
        static_cast<ID3D11Texture2D*>(source.texture)->GetDesc(&sourceDesc);
        if (sourceDesc.SampleDesc.Count > 1 || !m_fence) return false;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = source.width;
        desc.Height = source.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = sourceDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        wchar_t name[96];
        for (int i = 0; i < kVideoSlots; ++i) {
            HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_layerTextures[layer][i].put());
            Com<IDXGIResource1> resource;
            if (SUCCEEDED(hr)) hr = m_layerTextures[layer][i]->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(resource.put()));
            VideoTextureName(name, 96, GetCurrentProcessId(), generation, i);
            if (SUCCEEDED(hr))
                hr = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, name, &m_layerHandles[layer][i]);
            if (FAILED(hr)) {
                Log("Video : texture d'ecran plat impossible (format DXGI %u, erreur 0x%08lx).", static_cast<unsigned>(desc.Format),
                    static_cast<unsigned long>(hr));
                return false;
            }
        }
        format = static_cast<uint32_t>(desc.Format);
        return true;
    }

    void releaseLayerResources(int layer) override {
        for (int i = 0; i < kVideoSlots; ++i) {
            m_layerTextures[layer][i].reset();
            if (m_layerHandles[layer][i]) CloseHandle(m_layerHandles[layer][i]);
            m_layerHandles[layer][i] = nullptr;
        }
    }

    bool copyLayersTo(int slot, const VideoSource* sources, const int* layers, int count, uint64_t fenceValue) override {
        if (!m_fence) return false;
        for (int i = 0; i < count; ++i) {
            ID3D11Texture2D* target = m_layerTextures[layers[i]][slot].get();
            if (!target) return false;
            const VideoSource& source = sources[i];
            D3D11_BOX box = {source.x, source.y, 0, source.x + source.width, source.y + source.height, 1};
            m_context->CopySubresourceRegion(target, 0, 0, 0, 0, static_cast<ID3D11Texture2D*>(source.texture), source.subresource, &box);
        }
        if (FAILED(m_context4->Signal(m_fence.get(), fenceValue))) return false;
        m_context->Flush();
        return true;
    }

    bool copyTo(int slot, const VideoSource& source, uint64_t fenceValue) override {
        if (!m_textures[slot] || !m_fence) return false;
        D3D11_BOX box = {source.x, source.y, 0, source.x + source.width, source.y + source.height, 1};
        m_context->CopySubresourceRegion(m_textures[slot].get(), 0, 0, 0, 0, static_cast<ID3D11Texture2D*>(source.texture), source.subresource, &box);
        if (FAILED(m_context4->Signal(m_fence.get(), fenceValue))) return false;
        // La copie part tout de suite vers la carte graphique : l'enregistreur n'attend pas la fin de l'image.
        m_context->Flush();
        return true;
    }

    uint64_t adapterLuid() override {
        Com<IDXGIDevice> dxgiDevice;
        Com<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc = {};
        if (FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDevice.put()))) || !dxgiDevice ||
            FAILED(dxgiDevice->GetAdapter(adapter.put())) || !adapter || FAILED(adapter->GetDesc(&desc)))
            return 0;
        return static_cast<uint64_t>(desc.AdapterLuid.LowPart) | (static_cast<uint64_t>(static_cast<uint32_t>(desc.AdapterLuid.HighPart)) << 32);
    }

  private:
    Com<ID3D11Device> m_device;
    Com<ID3D11Device5> m_device5;
    Com<ID3D11DeviceContext> m_context;
    Com<ID3D11DeviceContext4> m_context4;
    Com<ID3D11Texture2D> m_textures[kVideoSlots];
    HANDLE m_handles[kVideoSlots] = {};
    Com<ID3D11Fence> m_fence;
    HANDLE m_fenceHandle = nullptr;
    Com<ID3D11Texture2D> m_layerTextures[kVideoLayers][kVideoSlots];
    HANDLE m_layerHandles[kVideoLayers][kVideoSlots] = {};
};

// --- Direct3D 12 (jeu en Direct3D 12) ------------------------------------------------------------------

class VideoProducerD3D12 final : public VideoProducer {
  public:
    VideoProducerD3D12(ID3D12Device* device, ID3D12CommandQueue* queue) : VideoProducer(kVideoOpenXrD3D12) {
        m_device.share(device);
        m_queue.share(queue);
        if (!device || !queue) {
            m_failed = true;
        } else if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
            // Les transitions « render target » n'existent que sur une file graphique.
            Log("Video : file de commandes du jeu non graphique, pas d'enregistrement video dans ce jeu.");
            m_failed = true;
        }
    }

    ~VideoProducerD3D12() override { releaseResources(); }

  protected:
    uint32_t sourceFormat(const VideoSource& source) override {
        return static_cast<uint32_t>(static_cast<ID3D12Resource*>(source.texture)->GetDesc().Format);
    }

    bool createResources(const VideoSource& source, uint32_t generation, uint32_t& format) override {
        const D3D12_RESOURCE_DESC sourceDesc = static_cast<ID3D12Resource*>(source.texture)->GetDesc();
        if (sourceDesc.SampleDesc.Count > 1) {
            Log("Video : images du jeu multi-echantillonnees (MSAA), non prises en charge.");
            return false;
        }
        // Textures créées par Direct3D 11 (sur la même carte) puis ouvertes ici : l'enregistreur (Direct3D 11)
        // refuse une texture créée par Direct3D 12 dans certains formats (E_INVALIDARG avec un format sans type,
        // celui des swapchains de beaucoup de runtimes) ; dans l'autre sens, le partage est toujours accepté.
        if (!ensureD3D11()) return false;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = source.width;
        desc.Height = source.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = sourceDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        const DWORD pid = GetCurrentProcessId();
        wchar_t name[96];
        for (int i = 0; i < kVideoSlots; ++i) {
            HRESULT hr = m_d3d11->CreateTexture2D(&desc, nullptr, m_shared11[i].put());
            Com<IDXGIResource1> resource;
            if (SUCCEEDED(hr)) hr = m_shared11[i]->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(resource.put()));
            VideoTextureName(name, 96, pid, generation, i);
            if (SUCCEEDED(hr))
                hr = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, name, &m_handles[i]);
            if (SUCCEEDED(hr)) hr = m_device->OpenSharedHandle(m_handles[i], __uuidof(ID3D12Resource), reinterpret_cast<void**>(m_textures[i].put()));
            if (SUCCEEDED(hr))
                hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                      reinterpret_cast<void**>(m_allocators[i].put()));
            if (SUCCEEDED(hr))
                hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[i].get(), nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                 reinterpret_cast<void**>(m_lists[i].put()));
            if (SUCCEEDED(hr)) hr = m_lists[i]->Close();
            m_slotValue[i] = 0;
            if (FAILED(hr)) {
                Log("Video : texture partagee impossible (format DXGI %u, erreur 0x%08lx).", static_cast<unsigned>(desc.Format),
                    static_cast<unsigned long>(hr));
                return false;
            }
        }
        HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void**>(m_fence.put()));
        VideoTextureName(name, 96, pid, generation, -1);
        if (SUCCEEDED(hr)) hr = m_device->CreateSharedHandle(m_fence.get(), nullptr, GENERIC_ALL, name, &m_fenceHandle);
        if (FAILED(hr)) {
            Log("Video : fence partage impossible (erreur 0x%08lx).", static_cast<unsigned long>(hr));
            return false;
        }
        m_lastValue = 0;
        format = static_cast<uint32_t>(desc.Format);
        return true;
    }

    // Appareil Direct3D 11 sur la carte du jeu, seulement pour créer les textures partagées (une fois, au
    // premier enregistrement ; chargé à la demande : un jeu Direct3D 12 n'a pas toujours d3d11.dll).
    bool ensureD3D11() {
        if (m_d3d11) return true;
        using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
        HMODULE dxgi = LoadLibraryW(L"dxgi.dll"), d3d11 = LoadLibraryW(L"d3d11.dll");
        const auto createFactory = dxgi ? reinterpret_cast<CreateFactoryFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
        const auto createDevice = d3d11 ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(d3d11, "D3D11CreateDevice"))) : nullptr;
        if (!createFactory || !createDevice) {
            Log("Video : Direct3D 11 indisponible (textures partagees d'un jeu Direct3D 12).");
            return false;
        }
        Com<IDXGIFactory1> factory;
        if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put())))) return false;
        const LUID luid = m_device->GetAdapterLuid();
        Com<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) break;
            adapter.reset();
        }
        if (!adapter) {
            Log("Video : carte graphique du jeu introuvable pour Direct3D 11.");
            return false;
        }
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        const HRESULT hr = createDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2, D3D11_SDK_VERSION, m_d3d11.put(), nullptr, nullptr);
        if (FAILED(hr)) {
            Log("Video : appareil Direct3D 11 impossible (erreur 0x%08lx).", static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }

    void releaseResources() override {
        // Copies encore en cours sur la file du jeu : on attend qu'elles finissent (une seconde au plus).
        if (m_fence && m_lastValue > 0 && m_fence->GetCompletedValue() < m_lastValue) {
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (event) {
                if (SUCCEEDED(m_fence->SetEventOnCompletion(m_lastValue, event))) WaitForSingleObject(event, 1000);
                CloseHandle(event);
            }
        }
        for (int i = 0; i < kVideoSlots; ++i) {
            m_lists[i].reset();
            m_allocators[i].reset();
            m_layerLists[i].reset();
            m_layerAllocators[i].reset();
            m_textures[i].reset();
            m_shared11[i].reset();
            if (m_handles[i]) CloseHandle(m_handles[i]);
            m_handles[i] = nullptr;
            m_slotValue[i] = 0;
        }
        for (int k = 0; k < kVideoLayers; ++k) releaseLayerResources(k);
        m_fence.reset();
        if (m_fenceHandle) CloseHandle(m_fenceHandle);
        m_fenceHandle = nullptr;
        m_lastValue = 0;
    }

    bool slotReady(int slot) override { return m_fence && m_fence->GetCompletedValue() >= m_slotValue[slot]; }

    // Écrans plats : textures créées par Direct3D 11 puis ouvertes ici, comme celles de l'œil.
    bool createLayerResources(int layer, const VideoSource& source, uint32_t generation, uint32_t& format) override {
        const D3D12_RESOURCE_DESC sourceDesc = static_cast<ID3D12Resource*>(source.texture)->GetDesc();
        if (sourceDesc.SampleDesc.Count > 1 || !m_fence || !ensureD3D11()) return false;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = source.width;
        desc.Height = source.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = sourceDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        wchar_t name[96];
        for (int i = 0; i < kVideoSlots; ++i) {
            HRESULT hr = m_d3d11->CreateTexture2D(&desc, nullptr, m_layerShared11[layer][i].put());
            Com<IDXGIResource1> resource;
            if (SUCCEEDED(hr)) hr = m_layerShared11[layer][i]->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(resource.put()));
            VideoTextureName(name, 96, GetCurrentProcessId(), generation, i);
            if (SUCCEEDED(hr))
                hr = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, name, &m_layerHandles[layer][i]);
            if (SUCCEEDED(hr))
                hr = m_device->OpenSharedHandle(m_layerHandles[layer][i], __uuidof(ID3D12Resource), reinterpret_cast<void**>(m_layerTextures[layer][i].put()));
            if (FAILED(hr)) {
                Log("Video : texture d'ecran plat impossible (format DXGI %u, erreur 0x%08lx).", static_cast<unsigned>(desc.Format),
                    static_cast<unsigned long>(hr));
                return false;
            }
        }
        format = static_cast<uint32_t>(desc.Format);
        return true;
    }

    void releaseLayerResources(int layer) override {
        for (int i = 0; i < kVideoSlots; ++i) {
            m_layerTextures[layer][i].reset();
            m_layerShared11[layer][i].reset();
            if (m_layerHandles[layer][i]) CloseHandle(m_layerHandles[layer][i]);
            m_layerHandles[layer][i] = nullptr;
        }
    }

    // Une liste de commandes par slot pour les écrans (la copie de l'œil a la sienne) ; chaque copie entre
    // ses transitions (une même texture du jeu peut servir à deux écrans).
    bool copyLayersTo(int slot, const VideoSource* sources, const int* layers, int count, uint64_t fenceValue) override {
        if (!m_fence) return false;
        if (!m_layerAllocators[slot]) {
            HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                          reinterpret_cast<void**>(m_layerAllocators[slot].put()));
            if (SUCCEEDED(hr))
                hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_layerAllocators[slot].get(), nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                 reinterpret_cast<void**>(m_layerLists[slot].put()));
            if (SUCCEEDED(hr)) hr = m_layerLists[slot]->Close();
            if (FAILED(hr)) {
                m_layerLists[slot].reset();
                m_layerAllocators[slot].reset();
                return false;
            }
        }
        // Le slot vient d'être choisi libre (slotReady) : sa liste précédente est terminée.
        if (FAILED(m_layerAllocators[slot]->Reset()) || FAILED(m_layerLists[slot]->Reset(m_layerAllocators[slot].get(), nullptr))) return false;
        ID3D12GraphicsCommandList* list = m_layerLists[slot].get();
        for (int i = 0; i < count; ++i) {
            ID3D12Resource* target = m_layerTextures[layers[i]][slot].get();
            if (!target) {
                list->Close();
                return false;
            }
            const VideoSource& source = sources[i];
            ID3D12Resource* src = static_cast<ID3D12Resource*>(source.texture);
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource = src;
            barriers[0].Transition.Subresource = source.subresource;
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition.pResource = target;
            barriers[1].Transition.Subresource = 0;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            list->ResourceBarrier(2, barriers);
            D3D12_TEXTURE_COPY_LOCATION to = {};
            to.pResource = target;
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION from = {};
            from.pResource = src;
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.SubresourceIndex = source.subresource;
            D3D12_BOX box = {source.x, source.y, 0, source.x + source.width, source.y + source.height, 1};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
                const D3D12_RESOURCE_STATES before = barrier.Transition.StateBefore;
                barrier.Transition.StateBefore = barrier.Transition.StateAfter;
                barrier.Transition.StateAfter = before;
            }
            list->ResourceBarrier(2, barriers);
        }
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list};
        m_queue->ExecuteCommandLists(1, lists);
        if (FAILED(m_queue->Signal(m_fence.get(), fenceValue))) return false;
        m_slotValue[slot] = fenceValue;
        m_lastValue = fenceValue;
        return true;
    }

    bool idle() override { return !m_fence || m_fence->GetCompletedValue() >= m_lastValue; }

    bool copyTo(int slot, const VideoSource& source, uint64_t fenceValue) override {
        if (!m_textures[slot] || !m_fence) return false;
        ID3D12Resource* src = static_cast<ID3D12Resource*>(source.texture);
        if (FAILED(m_allocators[slot]->Reset()) || FAILED(m_lists[slot]->Reset(m_allocators[slot].get(), nullptr))) return false;
        ID3D12GraphicsCommandList* list = m_lists[slot].get();
        // Image du jeu rendue au runtime : en état « render target » (règle OpenXR pour Direct3D 12).
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = src;
        barriers[0].Transition.Subresource = source.subresource;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_textures[slot].get();
        barriers[1].Transition.Subresource = 0;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(2, barriers);
        D3D12_TEXTURE_COPY_LOCATION to = {};
        to.pResource = m_textures[slot].get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION from = {};
        from.pResource = src;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = source.subresource;
        D3D12_BOX box = {source.x, source.y, 0, source.x + source.width, source.y + source.height, 1};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
        for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
            const D3D12_RESOURCE_STATES before = barrier.Transition.StateBefore;
            barrier.Transition.StateBefore = barrier.Transition.StateAfter;
            barrier.Transition.StateAfter = before;
        }
        list->ResourceBarrier(2, barriers);
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list};
        m_queue->ExecuteCommandLists(1, lists);
        if (FAILED(m_queue->Signal(m_fence.get(), fenceValue))) return false;
        m_slotValue[slot] = fenceValue;
        m_lastValue = fenceValue;
        return true;
    }

    uint64_t adapterLuid() override {
        const LUID luid = m_device->GetAdapterLuid();
        return static_cast<uint64_t>(luid.LowPart) | (static_cast<uint64_t>(static_cast<uint32_t>(luid.HighPart)) << 32);
    }

  private:
    Com<ID3D12Device> m_device;
    Com<ID3D12CommandQueue> m_queue;
    Com<ID3D12Resource> m_textures[kVideoSlots];
    Com<ID3D11Device> m_d3d11;                  // crée les textures partagées (voir createResources)
    Com<ID3D11Texture2D> m_shared11[kVideoSlots];
    HANDLE m_handles[kVideoSlots] = {};
    Com<ID3D12CommandAllocator> m_allocators[kVideoSlots];
    Com<ID3D12GraphicsCommandList> m_lists[kVideoSlots];
    uint64_t m_slotValue[kVideoSlots] = {};
    Com<ID3D12Fence> m_fence;
    HANDLE m_fenceHandle = nullptr;
    uint64_t m_lastValue = 0;
    Com<ID3D12CommandAllocator> m_layerAllocators[kVideoSlots];
    Com<ID3D12GraphicsCommandList> m_layerLists[kVideoSlots];
    Com<ID3D12Resource> m_layerTextures[kVideoLayers][kVideoSlots];
    Com<ID3D11Texture2D> m_layerShared11[kVideoLayers][kVideoSlots];
    HANDLE m_layerHandles[kVideoLayers][kVideoSlots] = {};
};

}  // namespace

std::unique_ptr<VideoProducer> VideoProducer::CreateD3D11(ID3D11Device* device, uint32_t kind) {
    return std::unique_ptr<VideoProducer>(new VideoProducerD3D11(device, kind));
}

std::unique_ptr<VideoProducer> VideoProducer::CreateD3D12(ID3D12Device* device, ID3D12CommandQueue* queue) {
    return std::unique_ptr<VideoProducer>(new VideoProducerD3D12(device, queue));
}

}  // namespace xrpo
