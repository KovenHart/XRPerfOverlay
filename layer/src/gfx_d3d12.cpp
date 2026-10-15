#include <cstring>
#include <vector>

#include "gfx.h"
#include "log.h"

namespace xrpo {

namespace {

class GfxD3D12 final : public Gfx {
    // Ressources de copie d'une swapchain (panneau et montre, ou fenêtre capturée).
    struct CopySlot {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> list;
        Com<ID3D12Fence> fence;
        Com<ID3D12Resource> upload;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT64 fenceValue = 0;
        std::vector<ID3D12Resource*> textures;
    };
    CopySlot m_copy[Gfx::kSwapchains];

  public:
    GfxD3D12(ID3D12Device* device, ID3D12CommandQueue* queue) {
        m_device.share(device);
        m_queue.share(queue);
    }

    ~GfxD3D12() override {
        waitIdle();
        if (m_event) CloseHandle(m_event);
    }

    const char* name() const override { return "Direct3D 12"; }

    bool enumerateImages(const NextFunctions& next, XrSwapchain swapchain, int slot) override {
        if (slot < 0 || slot >= Gfx::kSwapchains) return false;
        uint32_t count = 0;
        if (XR_FAILED(next.EnumerateSwapchainImages(swapchain, 0, &count, nullptr)) || count == 0) return false;
        std::vector<XrSwapchainImageD3D12KHR> images(count);
        for (auto& image : images) {
            image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
            image.next = nullptr;
            image.texture = nullptr;
        }
        if (XR_FAILED(next.EnumerateSwapchainImages(swapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))
            return false;
        CopySlot& target = m_copy[slot];
        target.textures.clear();
        for (uint32_t i = 0; i < count && i < images.size(); ++i) {
            if (!images[i].texture) return false;
            target.textures.push_back(images[i].texture);
        }
        return !target.textures.empty() && createResources(target, target.textures[0]);
    }

    UploadResult upload(int slot, uint32_t imageIndex, const uint8_t* pixels, int width, int height) override {
        if (slot < 0 || slot >= Gfx::kSwapchains) return UploadResult::Error;
        CopySlot& target = m_copy[slot];
        if (imageIndex >= target.textures.size() || !target.list || width <= 0 || height <= 0) return UploadResult::Error;
        // L'allocateur et le tampon d'envoi ne peuvent être réutilisés qu'une fois la copie précédente exécutée.
        if (target.fence->GetCompletedValue() < target.fenceValue) return UploadResult::Busy;
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = target.footprint;
        if (static_cast<UINT>(width) > footprint.Footprint.Width || static_cast<UINT>(height) > footprint.Footprint.Height) return UploadResult::Error;

        void* mapped = nullptr;
        const D3D12_RANGE noRead = {0, 0};
        if (FAILED(target.upload->Map(0, &noRead, &mapped)) || !mapped) return UploadResult::Error;
        uint8_t* dst = static_cast<uint8_t*>(mapped) + footprint.Offset;
        for (int y = 0; y < height; ++y)
            std::memcpy(dst + static_cast<size_t>(y) * footprint.Footprint.RowPitch, pixels + static_cast<size_t>(y) * width * 4,
                        static_cast<size_t>(width) * 4);
        target.upload->Unmap(0, nullptr);

        if (FAILED(target.allocator->Reset()) || FAILED(target.list->Reset(target.allocator.get(), nullptr))) return UploadResult::Error;

        ID3D12Resource* texture = target.textures[imageIndex];
        // La spécification OpenXR garantit que l'image acquise est dans l'état RENDER_TARGET
        // et exige qu'elle y soit revenue au moment de la rendre.
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture;
        barrier.Transition.Subresource = 0;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        target.list->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION to = {};
        to.pResource = texture;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION from = {};
        from.pResource = target.upload.get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = footprint;
        // Seulement la partie remplie (coin supérieur gauche) : le reste de la texture n'est pas montré.
        const D3D12_BOX box = {0, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height), 1};
        target.list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        target.list->ResourceBarrier(1, &barrier);

        if (FAILED(target.list->Close())) return UploadResult::Error;
        ID3D12CommandList* lists[] = {target.list.get()};
        m_queue->ExecuteCommandLists(1, lists);
        if (FAILED(m_queue->Signal(target.fence.get(), ++target.fenceValue))) return UploadResult::Error;
        return UploadResult::Ok;
    }

    bool busy(int slot) override {
        if (slot < 0 || slot >= Gfx::kSwapchains) return false;
        const CopySlot& target = m_copy[slot];
        return target.fence && target.fence->GetCompletedValue() < target.fenceValue;
    }

    void waitIdle() override {
        for (CopySlot& slot : m_copy) wait(slot.fence.get(), slot.fenceValue);
        if (m_timingFence) {
            // Un repère de début peut être parti sans repère de fin : un signal de plus couvre tout.
            if (SUCCEEDED(m_queue->Signal(m_timingFence.get(), m_timingFenceValue + 1))) ++m_timingFenceValue;
            wait(m_timingFence.get(), m_timingFenceValue);
        }
    }

    // --- Mesure du temps GPU -----------------------------------------------------------------------
    // Deux petites listes de commandes par image, exécutées sur la file du jeu : l'une ne contient
    // qu'un horodatage (début), l'autre un horodatage et sa copie vers un tampon lisible (fin).
    // Les files Direct3D 12 acceptent des soumissions depuis n'importe quel fil d'exécution.

    bool enableTiming() override {
        if (m_timing) return true;
        const D3D12_COMMAND_LIST_TYPE type = m_queue->GetDesc().Type;
        // Sur une file de copie, les horodatages ne sont pas garantis : pas de mesure.
        if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE) return false;
        if (FAILED(m_queue->GetTimestampFrequency(&m_tickFrequency)) || m_tickFrequency == 0) return false;

        D3D12_QUERY_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = 2 * kSlots;
        if (FAILED(m_device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(m_queryHeap.put())))) return false;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer = {};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = 2 * kSlots * sizeof(UINT64);
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.Format = DXGI_FORMAT_UNKNOWN;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(m_readback.put()))))
            return false;
        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_timingFence.put())))) return false;

        for (TimingSlot& slot : m_slots) {
            if (FAILED(m_device->CreateCommandAllocator(type, IID_PPV_ARGS(slot.beginAllocator.put()))) ||
                FAILED(m_device->CreateCommandAllocator(type, IID_PPV_ARGS(slot.endAllocator.put()))) ||
                FAILED(m_device->CreateCommandList(0, type, slot.beginAllocator.get(), nullptr, IID_PPV_ARGS(slot.beginList.put()))) ||
                FAILED(m_device->CreateCommandList(0, type, slot.endAllocator.get(), nullptr, IID_PPV_ARGS(slot.endList.put()))))
                return false;
            slot.beginList->Close();  // une liste est créée ouverte
            slot.endList->Close();
        }
        m_timing = true;
        return true;
    }

    void timingBegin() override {
        if (!m_timing) return;
        const int index = m_writeSlot;
        TimingSlot& slot = m_slots[index];
        if (slot.begun) {
            // Deuxième xrBeginFrame sans xrEndFrame (image abandonnée) : la mesure couvrirait deux images.
            slot.tainted = true;
            return;
        }
        if (slot.pending) return;  // résultats pas encore relus : image non mesurée
        // Les listes de cet emplacement ont fini de s'exécuter : ses résultats ont été relus.
        if (FAILED(slot.beginAllocator->Reset()) || FAILED(slot.beginList->Reset(slot.beginAllocator.get(), nullptr))) return;
        slot.beginList->EndQuery(m_queryHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(2 * index));
        if (FAILED(slot.beginList->Close())) return;  // rien n'a été soumis : l'emplacement reste libre
        ID3D12CommandList* lists[] = {slot.beginList.get()};
        m_queue->ExecuteCommandLists(1, lists);
        slot.begun = true;
        slot.tainted = false;
    }

    void timingEnd(double now) override {
        if (!m_timing) return;
        const int index = m_writeSlot;
        TimingSlot& slot = m_slots[index];
        if (!slot.begun) return;
        slot.begun = false;
        // La liste de début est peut-être encore sur le GPU : quoi qu'il arrive ensuite, l'emplacement
        // ne sera réutilisé qu'une fois une barrière passée derrière elle.
        bool valid = !slot.tainted;
        if (FAILED(slot.endAllocator->Reset()) || FAILED(slot.endList->Reset(slot.endAllocator.get(), nullptr))) {
            valid = false;
        } else {
            slot.endList->EndQuery(m_queryHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(2 * index + 1));
            slot.endList->ResolveQueryData(m_queryHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(2 * index), 2, m_readback.get(),
                                           static_cast<UINT64>(2 * index) * sizeof(UINT64));
            if (SUCCEEDED(slot.endList->Close())) {
                ID3D12CommandList* lists[] = {slot.endList.get()};
                m_queue->ExecuteCommandLists(1, lists);
            } else {
                valid = false;
            }
        }
        if (FAILED(m_queue->Signal(m_timingFence.get(), m_timingFenceValue + 1))) {
            // Sans barrière, impossible de savoir quand ces listes auront fini : la mesure s'arrête
            // pour la session plutôt que de risquer de réutiliser une liste en cours d'exécution.
            m_timing = false;
            return;
        }
        slot.fence = ++m_timingFenceValue;
        slot.endTime = now;
        slot.valid = valid;
        slot.pending = true;
        m_writeSlot = (m_writeSlot + 1) % kSlots;
    }

    bool adapterLuid(LUID& luid) override {
        if (!m_device) return false;
        luid = m_device->GetAdapterLuid();
        return true;
    }

    // Plus de listes envoyées. Celles déjà soumises finissent seules ; waitIdle attend leur barrière.
    void stopTiming() override { m_timing = false; }

    int timingCollect(GpuTiming* out, int capacity) override {
        int count = 0;
        while (m_timing && count < capacity) {
            const int index = m_readSlot;
            TimingSlot& slot = m_slots[index];
            if (!slot.pending || m_timingFence->GetCompletedValue() < slot.fence) break;
            slot.pending = false;
            m_readSlot = (m_readSlot + 1) % kSlots;
            if (!slot.valid) continue;  // image abandonnée au milieu, ou liste de fin non soumise
            const D3D12_RANGE read = {static_cast<SIZE_T>(2 * index) * sizeof(UINT64), static_cast<SIZE_T>(2 * index + 2) * sizeof(UINT64)};
            void* mapped = nullptr;
            if (FAILED(m_readback->Map(0, &read, &mapped)) || !mapped) continue;
            UINT64 stamps[2];
            std::memcpy(stamps, static_cast<const uint8_t*>(mapped) + read.Begin, sizeof(stamps));
            const D3D12_RANGE noWrite = {0, 0};
            m_readback->Unmap(0, &noWrite);
            if (stamps[1] <= stamps[0]) continue;
            const double ms = static_cast<double>(stamps[1] - stamps[0]) * 1000.0 / static_cast<double>(m_tickFrequency);
            if (ms < 1000.0) out[count++] = {slot.endTime, static_cast<float>(ms)};
        }
        return count;
    }

  private:
    // Attend (500 ms au plus) que la barrière atteigne `value`.
    void wait(ID3D12Fence* fence, UINT64 value) {
        if (!fence || fence->GetCompletedValue() >= value) return;
        if (!m_event) m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (m_event && SUCCEEDED(fence->SetEventOnCompletion(value, m_event))) WaitForSingleObject(m_event, 500);
    }

    bool createResources(CopySlot& slot, ID3D12Resource* sample) {
        // Swapchain recréée : les ressources précédentes sont remplacées, une fois leur copie terminée.
        wait(slot.fence.get(), slot.fenceValue);
        slot.upload.reset();
        slot.list.reset();
        slot.allocator.reset();
        slot.fence.reset();
        slot.fenceValue = 0;
        const D3D12_COMMAND_LIST_TYPE type = m_queue->GetDesc().Type;
        if (FAILED(m_device->CreateCommandAllocator(type, IID_PPV_ARGS(slot.allocator.put())))) return false;
        if (FAILED(m_device->CreateCommandList(0, type, slot.allocator.get(), nullptr, IID_PPV_ARGS(slot.list.put())))) {
            slot.allocator.reset();
            return false;
        }
        slot.list->Close();  // une liste est créée ouverte
        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(slot.fence.put())))) {
            slot.list.reset();
            return false;
        }

        // Disposition mémoire attendue pour copier un tampon vers le niveau 0 de la texture.
        const D3D12_RESOURCE_DESC textureDesc = sample->GetDesc();
        UINT64 totalBytes = 0;
        m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &slot.footprint, nullptr, nullptr, &totalBytes);
        if (totalBytes == 0 || totalBytes == UINT64_MAX) {
            slot.list.reset();
            return false;
        }

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer = {};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = totalBytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.Format = DXGI_FORMAT_UNKNOWN;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(slot.upload.put())))) {
            slot.list.reset();
            return false;
        }
        return true;
    }

    Com<ID3D12Device> m_device;
    Com<ID3D12CommandQueue> m_queue;
    HANDLE m_event = nullptr;

    // Mesure du temps GPU.
    static constexpr int kSlots = 8;  // images mesurées en attente de résultat au plus
    struct TimingSlot {
        Com<ID3D12CommandAllocator> beginAllocator, endAllocator;
        Com<ID3D12GraphicsCommandList> beginList, endList;
        UINT64 fence = 0;
        double endTime = 0.0;
        bool begun = false;    // repère de début soumis, pas encore celui de fin
        bool pending = false;  // barrière posée, résultat pas encore relu
        bool tainted = false;  // la mesure couvre une image abandonnée : à ignorer
        bool valid = false;    // le résultat relu aura un sens
    };
    bool m_timing = false;
    UINT64 m_tickFrequency = 0;
    Com<ID3D12QueryHeap> m_queryHeap;
    Com<ID3D12Resource> m_readback;
    Com<ID3D12Fence> m_timingFence;
    UINT64 m_timingFenceValue = 0;
    TimingSlot m_slots[kSlots];
    int m_writeSlot = 0, m_readSlot = 0;
};

}  // namespace

Gfx* CreateGfxD3D12(ID3D12Device* device, ID3D12CommandQueue* queue) {
    return (device && queue) ? new GfxD3D12(device, queue) : nullptr;
}

}  // namespace xrpo
