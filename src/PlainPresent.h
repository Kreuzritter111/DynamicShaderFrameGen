#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace FrameGen::PlainPresent
{
    inline UINT ResizeCount(UINT requested, UINT current) { return requested == 0 ? current : (requested == 1 ? 2 : requested); }
    inline UINT ResizeFlags(UINT requested, UINT created)
    {
        return (requested & ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) | (created & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    }

    inline bool CopyCompatible(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b)
    {
        return a.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && a.Dimension == b.Dimension &&
            a.Width == b.Width && a.Height == b.Height && a.Format == b.Format &&
            a.DepthOrArraySize == b.DepthOrArraySize && a.MipLevels == b.MipLevels &&
            a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality;
    }

    // Both shared source and DXGI buffer enter/leave this list in COMMON/PRESENT
    // (the two names have the same value). Never leave a backbuffer in COPY_DEST.
    inline HRESULT RecordCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* source, ID3D12Resource* buffer)
    {
        if (!list || !source || !buffer || !CopyCompatible(source->GetDesc(), buffer->GetDesc()))
            return E_INVALIDARG;
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto& barrier : barriers) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        }
        barriers[0].Transition.pResource = source;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource = buffer;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(2, barriers);
        list->CopyResource(buffer, source);
        for (auto& barrier : barriers) {
            barrier.Transition.StateBefore = barrier.Transition.StateAfter;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        }
        list->ResourceBarrier(2, barriers);
        return S_OK;
    }

    inline HRESULT Wait(ID3D12Fence* fence, UINT64 value, DWORD timeoutMs)
    {
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) return DXGI_ERROR_DEVICE_REMOVED;
        if (completed >= value) return S_OK;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return HRESULT_FROM_WIN32(GetLastError());
        HRESULT hr = fence->SetEventOnCompletion(value, event);
        if (SUCCEEDED(hr)) {
            const auto result = WaitForSingleObject(event, timeoutMs);
            if (result != WAIT_OBJECT_0)
                hr = result == WAIT_TIMEOUT ? DXGI_ERROR_WAS_STILL_DRAWING : HRESULT_FROM_WIN32(GetLastError());
        }
        CloseHandle(event);
        return hr;
    }

    template<class Present>
    HRESULT Submit(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list,
        ID3D12Fence* completion, UINT64 value, Present&& present)
    {
        HRESULT hr = list->Close();
        if (FAILED(hr)) return hr;
        ID3D12CommandList* lists[]{list};
        queue->ExecuteCommandLists(1, lists);
        hr = queue->Signal(completion, value);
        if (FAILED(hr)) return hr;
        // Completion is queued even when Present rejects the frame. The caller
        // must retain this value for allocator reuse on both success and failure.
        return present();
    }
}
