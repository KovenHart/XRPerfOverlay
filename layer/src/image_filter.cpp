#include "image_filter.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstring>
#include <string>
#include <vector>

#include "image_filter_source.h"
#include "log.h"

namespace xrpo {
namespace {

using Microsoft::WRL::ComPtr;

// --- Formats ---------------------------------------------------------------------------------------
// Copie de travail : format sans type de la même famille (la copie et les vues l'exigent).
DXGI_FORMAT TypelessOf(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8X8_TYPELESS;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return DXGI_FORMAT_R10G10B10A2_TYPELESS;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

// Vues (lecture de la copie, écriture dans l'image) : le format choisi par le jeu ; un format sans type
// (rare) est lu comme UNORM. Dans les deux cas le shader reçoit des valeurs linéaires (OpenXR : un format
// UNORM porte des valeurs linéaires, une vue sRGB les décode) et en rend.
DXGI_FORMAT ViewOf(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        default: return f;
    }
}

// --- Compilation du shader (d3dcompiler_47, chargé à la demande : la layer n'en dépend pas) ---------
using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

bool CompileFilter(ComPtr<ID3DBlob>& vs, ComPtr<ID3DBlob>& ps) {
    static CompileFn compile = nullptr;
    if (!compile) {
        HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
        if (module) compile = reinterpret_cast<CompileFn>(reinterpret_cast<void*>(GetProcAddress(module, "D3DCompile")));
    }
    if (!compile) {
        Log("Filtres d'image : d3dcompiler_47.dll introuvable.");
        return false;
    }
    const std::string source = std::string(kImageFilterCore) + "\n" + kImageFilterEntry;
    for (int pass = 0; pass < 2; ++pass) {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = compile(source.data(), source.size(), "image_filter", nullptr, nullptr, pass == 0 ? "VsFilter" : "PsFilter",
                                   pass == 0 ? "vs_5_0" : "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, pass == 0 ? vs.GetAddressOf() : ps.GetAddressOf(),
                                   errors.GetAddressOf());
        if (FAILED(hr)) {
            if (errors)
                Log("Filtres d'image : shader refuse : %.*s", static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
            else
                Log("Filtres d'image : shader refuse (erreur 0x%08lx).", static_cast<unsigned long>(hr));
            return false;
        }
    }
    return true;
}

// --- Direct3D 11 -------------------------------------------------------------------------------------
class ImageFilterD3D11 final : public ImageFilter {
  public:
    bool init(ID3D11Device* device) {
        m_device = device;
        device->GetImmediateContext(m_context.GetAddressOf());
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(m_device1.GetAddressOf()))) || FAILED(m_context.As(&m_context1))) {
            Log("Filtres d'image : Direct3D 11.1 indisponible.");
            return false;
        }
        ComPtr<ID3DBlob> vs, ps;
        if (!CompileFilter(vs, ps)) return false;
        if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_vs.GetAddressOf())) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ps.GetAddressOf()))) {
            Log("Filtres d'image : shaders Direct3D 11 impossibles.");
            return false;
        }
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(ImageFilterParams);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&bd, nullptr, m_constants.GetAddressOf()))) return false;
        // État de rendu à nous : celui du jeu est mis de côté pendant le rendu, puis remis tel quel.
        const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
        if (FAILED(m_device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, m_state.GetAddressOf()))) {
            Log("Filtres d'image : etat de rendu Direct3D 11 impossible.");
            return false;
        }
        return true;
    }

    bool apply(const ImageFilterTarget* targets, int count, int64_t format, uint32_t mipCount, const ImageFilterParams& params) override {
        if (m_failed || count <= 0 || !targets[0].texture) return false;
        auto* texture = static_cast<ID3D11Texture2D*>(targets[0].texture);
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        const DXGI_FORMAT viewFormat = ViewOf(static_cast<DXGI_FORMAT>(format));
        const DXGI_FORMAT typeless = TypelessOf(desc.Format);
        if (desc.SampleDesc.Count > 1 || typeless == DXGI_FORMAT_UNKNOWN || TypelessOf(viewFormat) != typeless) return false;
        (void)mipCount;
        // Copies de travail d'abord (hors de notre état de rendu) : chaque zone telle que le jeu l'a rendue.
        ID3D11ShaderResourceView* views[4] = {};
        count = count > 4 ? 4 : count;
        for (int t = 0; t < count; ++t) {
            const ImageFilterTarget& target = targets[t];
            Work* work = workFor(t, target.width, target.height, typeless, viewFormat);
            if (!work) return fail();
            const D3D11_BOX box = {target.x, target.y, 0, target.x + target.width, target.y + target.height, 1};
            m_context->CopySubresourceRegion(work->texture.Get(), 0, 0, 0, 0, texture, D3D11CalcSubresource(0, target.slice, desc.MipLevels), &box);
            views[t] = work->view.Get();
        }
        ID3DDeviceContextState* previous = nullptr;
        m_context1->SwapDeviceContextState(m_state.Get(), &previous);
        bool ok = true;
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);
        ID3D11Buffer* buffers[] = {m_constants.Get()};
        m_context->PSSetConstantBuffers(0, 1, buffers);
        for (int t = 0; t < count && ok; ++t) {
            const ImageFilterTarget& target = targets[t];
            D3D11_RENDER_TARGET_VIEW_DESC rd = {};
            rd.Format = viewFormat;
            if (desc.ArraySize > 1) {
                rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rd.Texture2DArray.FirstArraySlice = target.slice;
                rd.Texture2DArray.ArraySize = 1;
            } else {
                rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            }
            ComPtr<ID3D11RenderTargetView> rtv;
            ImageFilterParams p = params;
            p.originX = static_cast<float>(target.x);
            p.originY = static_cast<float>(target.y);
            p.width = static_cast<float>(target.width);
            p.height = static_cast<float>(target.height);
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (FAILED(m_device->CreateRenderTargetView(texture, &rd, rtv.GetAddressOf())) ||
                FAILED(m_context->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                ok = false;
                break;
            }
            std::memcpy(mapped.pData, &p, sizeof(p));
            m_context->Unmap(m_constants.Get(), 0);
            ID3D11RenderTargetView* targetsRtv[] = {rtv.Get()};
            m_context->OMSetRenderTargets(1, targetsRtv, nullptr);
            const D3D11_VIEWPORT viewport = {static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.width),
                                             static_cast<float>(target.height), 0.f, 1.f};
            m_context->RSSetViewports(1, &viewport);
            ID3D11ShaderResourceView* srv[] = {views[t]};
            m_context->PSSetShaderResources(0, 1, srv);
            m_context->Draw(3, 0);
        }
        // Rien des nôtres ne reste attaché (l'image du jeu ne doit pas rester tenue par notre état).
        ID3D11ShaderResourceView* noView[] = {nullptr};
        m_context->PSSetShaderResources(0, 1, noView);
        ID3D11RenderTargetView* noTarget[] = {nullptr};
        m_context->OMSetRenderTargets(1, noTarget, nullptr);
        m_context1->SwapDeviceContextState(previous, nullptr);
        if (previous) previous->Release();
        if (!ok) return fail();
        return true;
    }

  private:
    // Copie de travail d'une zone (une par œil), agrandie seulement si besoin.
    struct Work {
        UINT width = 0, height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, viewFormat = DXGI_FORMAT_UNKNOWN;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
    };

    Work* workFor(int index, UINT width, UINT height, DXGI_FORMAT typeless, DXGI_FORMAT viewFormat) {
        Work& w = m_work[index];
        if (w.texture && w.width >= width && w.height >= height && w.format == typeless && w.viewFormat == viewFormat) return &w;
        w = Work();
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = typeless;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
        vd.Format = viewFormat;
        vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateTexture2D(&td, nullptr, w.texture.GetAddressOf())) ||
            FAILED(m_device->CreateShaderResourceView(w.texture.Get(), &vd, w.view.GetAddressOf()))) {
            w = Work();
            return nullptr;
        }
        w.width = width;
        w.height = height;
        w.format = typeless;
        w.viewFormat = viewFormat;
        return &w;
    }

    bool fail() {
        Log("Filtres d'image : rendu Direct3D 11 impossible, filtres arretes pour cette session.");
        m_failed = true;
        return false;
    }

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11Device1> m_device1;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11DeviceContext1> m_context1;
    ComPtr<ID3DDeviceContextState> m_state;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11Buffer> m_constants;
    Work m_work[4];
};

// --- Direct3D 12 -------------------------------------------------------------------------------------
class ImageFilterD3D12 final : public ImageFilter {
  public:
    ~ImageFilterD3D12() override {
        // Rien ne doit être libéré pendant que la carte graphique s'en sert encore.
        if (m_fence && m_event && m_fence->GetCompletedValue() < m_value && SUCCEEDED(m_fence->SetEventOnCompletion(m_value, m_event)))
            WaitForSingleObject(m_event, 500);
        if (m_event) CloseHandle(m_event);
    }

    bool init(ID3D12Device* device, ID3D12CommandQueue* queue) {
        m_device = device;
        m_queue = queue;
        if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
            Log("Filtres d'image : file Direct3D 12 du jeu sans rendu (type %d).", static_cast<int>(queue->GetDesc().Type));
            return false;
        }
        ComPtr<ID3DBlob> vs, ps;
        if (!CompileFilter(vs, ps)) return false;
        m_vs = vs;
        m_ps = ps;
        // Signature : 20 constantes (b0) et la copie de travail (t0), lue par Load (pas d'échantillonneur).
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[2] = {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = sizeof(ImageFilterParams) / 4;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs = {};
        rs.NumParameters = 2;
        rs.pParameters = parameters;
        using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
        HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
        const auto serialize = d3d12 ? reinterpret_cast<SerializeFn>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12SerializeRootSignature"))) : nullptr;
        ComPtr<ID3DBlob> blob, errors;
        if (!serialize || FAILED(serialize(&rs, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), errors.GetAddressOf())) ||
            FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(m_root.GetAddressOf())))) {
            Log("Filtres d'image : signature Direct3D 12 impossible.");
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kSlots * kTargets;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_srvHeap.GetAddressOf())))) return false;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_rtvHeap.GetAddressOf())))) return false;
        m_srvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_rtvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (int i = 0; i < kSlots; ++i)
            if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_allocators[i].GetAddressOf())))) return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(m_list.GetAddressOf()))))
            return false;
        m_list->Close();
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf())))) return false;
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return m_event != nullptr;
    }

    bool apply(const ImageFilterTarget* targets, int count, int64_t format, uint32_t mipCount, const ImageFilterParams& params) override {
        if (m_failed || count <= 0 || !targets[0].texture) return false;
        auto* image = static_cast<ID3D12Resource*>(targets[0].texture);
        const D3D12_RESOURCE_DESC desc = image->GetDesc();
        const DXGI_FORMAT viewFormat = ViewOf(static_cast<DXGI_FORMAT>(format));
        const DXGI_FORMAT typeless = TypelessOf(desc.Format);
        if (desc.SampleDesc.Count > 1 || typeless == DXGI_FORMAT_UNKNOWN || TypelessOf(viewFormat) != typeless) return false;
        ID3D12PipelineState* pso = pipelineFor(viewFormat);
        if (!pso) return fail("pipeline");
        count = count > kTargets ? kTargets : count;

        // Emplacement suivant : ses commandes précédentes doivent être terminées (presque toujours le cas).
        const int slot = m_nextSlot;
        m_nextSlot = (m_nextSlot + 1) % kSlots;
        if (m_fence->GetCompletedValue() < m_slotValues[slot]) {
            if (FAILED(m_fence->SetEventOnCompletion(m_slotValues[slot], m_event)) || WaitForSingleObject(m_event, 100) != WAIT_OBJECT_0) return false;
        }
        if (FAILED(m_allocators[slot]->Reset()) || FAILED(m_list->Reset(m_allocators[slot].Get(), pso))) return fail("liste de commandes");
        ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
        m_list->SetDescriptorHeaps(1, heaps);
        m_list->SetGraphicsRootSignature(m_root.Get());
        m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const uint32_t mips = mipCount ? mipCount : desc.MipLevels;
        for (int t = 0; t < count; ++t) {
            const ImageFilterTarget& target = targets[t];
            Work* work = workFor(t, target.width, target.height, typeless);
            if (!work) {
                m_list->Close();
                return fail("copie de travail");
            }
            const UINT subresource = target.slice * mips;  // niveau 0 de la tranche (D3D12CalcSubresource)
            // Image : rendu -> source de copie ; copie de la zone ; retour au rendu (état OpenXR à la libération).
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource = image;
            barriers[0].Transition.Subresource = subresource;
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            m_list->ResourceBarrier(1, barriers);
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = work->texture.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = image;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = subresource;
            const D3D12_BOX box = {target.x, target.y, 0, target.x + target.width, target.y + target.height, 1};
            m_list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition.pResource = work->texture.Get();
            barriers[1].Transition.Subresource = 0;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            m_list->ResourceBarrier(2, barriers);

            // Vues de cet emplacement : lecture de la copie, écriture dans la zone de l'image.
            const int index = slot * kTargets + t;
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
            srvCpu.ptr += static_cast<SIZE_T>(index) * m_srvStep;
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
            srvGpu.ptr += static_cast<UINT64>(index) * m_srvStep;
            D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = viewFormat;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(work->texture.Get(), &sd, srvCpu);
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += static_cast<SIZE_T>(index) * m_rtvStep;
            D3D12_RENDER_TARGET_VIEW_DESC rd = {};
            rd.Format = viewFormat;
            if (desc.DepthOrArraySize > 1) {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rd.Texture2DArray.FirstArraySlice = target.slice;
                rd.Texture2DArray.ArraySize = 1;
            } else {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            }
            m_device->CreateRenderTargetView(image, &rd, rtv);

            ImageFilterParams p = params;
            p.originX = static_cast<float>(target.x);
            p.originY = static_cast<float>(target.y);
            p.width = static_cast<float>(target.width);
            p.height = static_cast<float>(target.height);
            m_list->SetGraphicsRoot32BitConstants(0, sizeof(p) / 4, &p, 0);
            m_list->SetGraphicsRootDescriptorTable(1, srvGpu);
            m_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const D3D12_VIEWPORT viewport = {static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.width),
                                             static_cast<float>(target.height), 0.f, 1.f};
            const D3D12_RECT scissor = {static_cast<LONG>(target.x), static_cast<LONG>(target.y), static_cast<LONG>(target.x + target.width),
                                        static_cast<LONG>(target.y + target.height)};
            m_list->RSSetViewports(1, &viewport);
            m_list->RSSetScissorRects(1, &scissor);
            m_list->DrawInstanced(3, 1, 0, 0);

            barriers[0] = barriers[1];
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            m_list->ResourceBarrier(1, barriers);
        }
        if (FAILED(m_list->Close())) return fail("liste de commandes");
        ID3D12CommandList* lists[] = {m_list.Get()};
        m_queue->ExecuteCommandLists(1, lists);
        if (FAILED(m_queue->Signal(m_fence.Get(), ++m_value))) return fail("fence");
        m_slotValues[slot] = m_value;
        return true;
    }

  private:
    static constexpr int kSlots = 8;    // soumissions en vol (une par image et par swapchain)
    static constexpr int kTargets = 4;  // zones par soumission

    struct Work {
        UINT width = 0, height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        ComPtr<ID3D12Resource> texture;  // toujours en COPY_DEST entre deux utilisations
    };

    Work* workFor(int index, UINT width, UINT height, DXGI_FORMAT typeless) {
        Work& w = m_work[index];
        if (w.texture && w.width >= width && w.height >= height && w.format == typeless) return &w;
        // Remplacée : la précédente peut encore servir à une soumission en vol.
        if (w.texture && m_fence->GetCompletedValue() < m_value && SUCCEEDED(m_fence->SetEventOnCompletion(m_value, m_event)))
            WaitForSingleObject(m_event, 500);
        w = Work();
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = width;
        rd.Height = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = typeless;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(w.texture.GetAddressOf())))) {
            w = Work();
            return nullptr;
        }
        w.width = width;
        w.height = height;
        w.format = typeless;
        return &w;
    }

    ID3D12PipelineState* pipelineFor(DXGI_FORMAT format) {
        for (auto& entry : m_pipelines)
            if (entry.first == format) return entry.second.Get();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = m_root.Get();
        pd.VS = {m_vs->GetBufferPointer(), m_vs->GetBufferSize()};
        pd.PS = {m_ps->GetBufferPointer(), m_ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = format;
        pd.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pso;
        if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(pso.GetAddressOf())))) return nullptr;
        m_pipelines.emplace_back(format, pso);
        return pso.Get();
    }

    bool fail(const char* step) {
        Log("Filtres d'image : Direct3D 12, echec (%s), filtres arretes pour cette session.", step);
        m_failed = true;
        return false;
    }

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3DBlob> m_vs, m_ps;
    ComPtr<ID3D12RootSignature> m_root;
    std::vector<std::pair<DXGI_FORMAT, ComPtr<ID3D12PipelineState>>> m_pipelines;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap, m_rtvHeap;
    UINT m_srvStep = 0, m_rtvStep = 0;
    ComPtr<ID3D12CommandAllocator> m_allocators[kSlots];
    ComPtr<ID3D12GraphicsCommandList> m_list;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_event = nullptr;
    UINT64 m_value = 0;
    UINT64 m_slotValues[kSlots] = {};
    int m_nextSlot = 0;
    Work m_work[kTargets];
};

}  // namespace

std::unique_ptr<ImageFilter> ImageFilter::CreateD3D11(ID3D11Device* device) {
    if (!device) return nullptr;
    std::unique_ptr<ImageFilterD3D11> filter(new ImageFilterD3D11());
    if (!filter->init(device)) return nullptr;
    Log("Filtres d'image : prets (Direct3D 11).");
    return filter;
}

std::unique_ptr<ImageFilter> ImageFilter::CreateD3D12(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (!device || !queue) return nullptr;
    std::unique_ptr<ImageFilterD3D12> filter(new ImageFilterD3D12());
    if (!filter->init(device, queue)) return nullptr;
    Log("Filtres d'image : prets (Direct3D 12).");
    return filter;
}

bool ImageFilter::Supported(int64_t format) { return TypelessOf(static_cast<DXGI_FORMAT>(format)) != DXGI_FORMAT_UNKNOWN; }

}  // namespace xrpo
