#include <vector>

#include <d3d11_4.h>  // ID3D11Multithread

#include "gfx.h"
#include "log.h"

namespace xrpo {

namespace {

class GfxD3D11 final : public Gfx {
  public:
    explicit GfxD3D11(ID3D11Device* device) {
        m_device.share(device);
        device->GetImmediateContext(m_context.put());
    }

    const char* name() const override { return "Direct3D 11"; }

    bool enumerateImages(const NextFunctions& next, XrSwapchain swapchain, int slot) override {
        if (slot < 0 || slot >= Gfx::kSwapchains) return false;
        uint32_t count = 0;
        if (XR_FAILED(next.EnumerateSwapchainImages(swapchain, 0, &count, nullptr)) || count == 0) return false;
        std::vector<XrSwapchainImageD3D11KHR> images(count);
        for (auto& image : images) {
            image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            image.next = nullptr;
            image.texture = nullptr;
        }
        if (XR_FAILED(next.EnumerateSwapchainImages(swapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))
            return false;
        std::vector<ID3D11Texture2D*>& textures = m_textures[slot];
        textures.clear();
        for (uint32_t i = 0; i < count && i < images.size(); ++i) {
            if (!images[i].texture) return false;
            textures.push_back(images[i].texture);  // appartiennent au runtime, valides tant que la swapchain existe
        }
        return !textures.empty();
    }

    // --- Mesure du temps GPU -----------------------------------------------------------------------
    // Requêtes d'horodatage sur le contexte immédiat du jeu. Ce contexte n'accepte pas d'appels
    // simultanés depuis plusieurs fils d'exécution, et rien n'oblige un jeu à ne pas s'en servir
    // pendant xrBeginFrame. Deux cas :
    //  - le jeu a activé la protection multi-fils de Direct3D 11 (ID3D11Multithread) : le repère de
    //    début est posé à xrBeginFrame, sous cette protection ;
    //  - sinon, il est posé à la fin du xrEndFrame précédent, sur le fil d'exécution qui soumet déjà
    //    les images (celui où la layer copie aussi son panneau). La durée mesurée va alors de la fin
    //    d'une image à la fin de la suivante sur le GPU.

    bool enableTiming() override {
        if (m_timing) return true;
        if (!m_device || !m_context) return false;
        if (SUCCEEDED(m_context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(m_multithread.put()))) && m_multithread &&
            m_multithread->GetMultithreadProtected()) {
            m_beginAtBeginFrame = true;
        } else {
            m_multithread.reset();
            m_beginAtBeginFrame = false;
        }
        for (TimingSlot& slot : m_slots) {
            D3D11_QUERY_DESC disjoint = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            D3D11_QUERY_DESC stamp = {D3D11_QUERY_TIMESTAMP, 0};
            if (FAILED(m_device->CreateQuery(&disjoint, slot.disjoint.put())) || FAILED(m_device->CreateQuery(&stamp, slot.begin.put())) ||
                FAILED(m_device->CreateQuery(&stamp, slot.end.put())))
                return false;
        }
        m_timing = true;
        return true;
    }

    void timingBegin() override {
        if (!m_timing) return;
        TimingSlot& slot = m_slots[m_writeSlot];
        // Deuxième xrBeginFrame sans xrEndFrame (image abandonnée) : la mesure en cours couvrirait
        // deux images, elle sera ignorée.
        if (m_frameBegun) slot.tainted = true;
        m_frameBegun = true;
        if (!m_beginAtBeginFrame) return;  // repère déjà posé à la fin de l'image précédente
        if (slot.begun || slot.pending) return;
        m_multithread->Enter();
        m_context->Begin(slot.disjoint.get());
        m_context->End(slot.begin.get());
        m_multithread->Leave();
        slot.begun = true;
    }

    void timingEnd(double now) override {
        if (!m_timing) return;
        m_frameBegun = false;
        TimingSlot& slot = m_slots[m_writeSlot];
        if (slot.begun) {
            if (m_multithread) m_multithread->Enter();
            m_context->End(slot.end.get());
            m_context->End(slot.disjoint.get());
            if (m_multithread) m_multithread->Leave();
            slot.begun = false;
            slot.pending = true;
            slot.endTime = now;
            m_writeSlot = (m_writeSlot + 1) % kSlots;
        }
        if (!m_beginAtBeginFrame) {
            // Repère de début de l'image suivante, posé ici, sur le fil d'exécution qui rend les images.
            TimingSlot& next = m_slots[m_writeSlot];
            if (!next.begun && !next.pending) {
                m_context->Begin(next.disjoint.get());
                m_context->End(next.begin.get());
                next.begun = true;
                next.tainted = false;
            }
        }
    }

    int timingCollect(GpuTiming* out, int capacity) override {
        int count = 0;
        while (m_timing && count < capacity) {
            TimingSlot& slot = m_slots[m_readSlot];
            if (!slot.pending) break;
            // DONOTFLUSH : on ne force jamais l'envoi du travail du jeu, on attend simplement qu'il soit fait.
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
            UINT64 begin = 0, end = 0;
            if (m_context->GetData(slot.disjoint.get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                m_context->GetData(slot.begin.get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                m_context->GetData(slot.end.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                break;
            slot.pending = false;
            const bool tainted = slot.tainted;
            slot.tainted = false;
            m_readSlot = (m_readSlot + 1) % kSlots;
            // Mesure faussée (image abandonnée au milieu, changement de fréquence du GPU) ou incohérente : ignorée.
            if (tainted || disjoint.Disjoint || disjoint.Frequency == 0 || end <= begin) continue;
            const double ms = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(disjoint.Frequency);
            if (ms < 1000.0) out[count++] = {slot.endTime, static_cast<float>(ms)};
        }
        return count;
    }

    void stopTiming() override {
        if (!m_timing) return;
        // Repère de début déjà posé pour l'image suivante : la requête ouverte est refermée.
        for (TimingSlot& slot : m_slots) {
            if (!slot.begun) continue;
            if (m_multithread) m_multithread->Enter();
            m_context->End(slot.end.get());
            m_context->End(slot.disjoint.get());
            if (m_multithread) m_multithread->Leave();
            slot.begun = false;
        }
        m_timing = false;
    }

    bool adapterLuid(LUID& luid) override {
        Com<IDXGIDevice> dxgiDevice;
        Com<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc = {};
        if (!m_device || FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDevice.put()))) || !dxgiDevice ||
            FAILED(dxgiDevice->GetAdapter(adapter.put())) || !adapter || FAILED(adapter->GetDesc(&desc)))
            return false;
        luid = desc.AdapterLuid;
        return true;
    }

    UploadResult upload(int slot, uint32_t imageIndex, const uint8_t* pixels, int width, int height) override {
        if (slot < 0 || slot >= Gfx::kSwapchains || imageIndex >= m_textures[slot].size() || !m_context || width <= 0 || height <= 0)
            return UploadResult::Error;
        ID3D11Texture2D* texture = m_textures[slot][imageIndex];
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        if (static_cast<UINT>(width) > desc.Width || static_cast<UINT>(height) > desc.Height) return UploadResult::Error;
        // Simple copie mémoire -> texture : ne change aucun état du contexte du jeu.
        // Appelée depuis xrEndFrame, donc sur le fil d'exécution qui soumet déjà les images.
        const D3D11_BOX box = {0, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height), 1};
        m_context->UpdateSubresource(texture, 0, &box, pixels, static_cast<UINT>(width) * 4, 0);
        return UploadResult::Ok;
    }

  private:
    static constexpr int kSlots = 8;  // images mesurées en attente de résultat au plus
    struct TimingSlot {
        Com<ID3D11Query> disjoint, begin, end;
        double endTime = 0.0;
        bool begun = false;    // repère de début posé, pas encore celui de fin
        bool pending = false;  // les deux repères sont posés, résultat pas encore relu
        bool tainted = false;  // la mesure couvre une image abandonnée : à ignorer
    };

    Com<ID3D11Device> m_device;
    Com<ID3D11DeviceContext> m_context;
    std::vector<ID3D11Texture2D*> m_textures[Gfx::kSwapchains];
    bool m_timing = false;
    bool m_beginAtBeginFrame = false;  // protection multi-fils active : repère de début à xrBeginFrame
    bool m_frameBegun = false;         // xrBeginFrame vu depuis le dernier xrEndFrame
    Com<ID3D11Multithread> m_multithread;
    TimingSlot m_slots[kSlots];
    int m_writeSlot = 0, m_readSlot = 0;
};

}  // namespace

Gfx* CreateGfxD3D11(ID3D11Device* device) {
    return device ? new GfxD3D11(device) : nullptr;
}

}  // namespace xrpo
