// The non-FG presentation transaction. DLAA/NR rendering is upstream and unchanged.
#include "FrameGen.h"
#include "PlainPresent.h"
#include <d3d12sdklayers.h>
#include <dxgidebug.h>
#include <memory>
#include <vector>

namespace FrameGen
{
    void DX12SwapChain::LogPlainFailure(HRESULT result, UINT sync, UINT flags, UINT usedIndex)
    {
        ++plainFailures;
        if (plainFailures > 3 && plainFailures % 120 != 0) return;
        DXGI_SWAP_CHAIN_DESC1 actual{};
        BOOL fullscreen = FALSE;
        swapChain->GetDesc1(&actual);
        swapChain->GetFullscreenState(&fullscreen, nullptr);
        const auto src = colorOutWrapped->resource->GetDesc();
        SKSE::log::error("[PRESENT0] failure #{} hr=0x{:08X} stage={} sync={} flags=0x{:x} "
            "used/current={}/{} fullscreen={} size={}x{} format={} buffers={} effect={} chainFlags=0x{:x} "
            "source={}x{} format={} submitted/completed={}/{} removed=0x{:08X}. copy contract=COMMON/PRESENT (actual GPU validation messages follow if available)",
            plainFailures, static_cast<unsigned>(result), m_stage, sync, flags, usedIndex,
            swapChain->GetCurrentBackBufferIndex(), fullscreen != FALSE,
            actual.Width, actual.Height, static_cast<unsigned>(actual.Format), actual.BufferCount,
            static_cast<unsigned>(actual.SwapEffect), actual.Flags, src.Width, src.Height,
            static_cast<unsigned>(src.Format), plainSerial, plainCompletion->GetCompletedValue(),
            static_cast<unsigned>(d3d12Device->GetDeviceRemovedReason()));
        // Read only. Do not globally enable a debug layer after other plugins may
        // already have created devices, or clear another component's DXGI log.
        winrt::com_ptr<ID3D12InfoQueue> info;
        if (SUCCEEDED(d3d12Device->QueryInterface(IID_PPV_ARGS(info.put())))) {
            const auto count = info->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = count > 24 ? count - 24 : 0; i < count; ++i) {
                SIZE_T size = 0;
                if (FAILED(info->GetMessage(i, nullptr, &size))) continue;
                std::vector<char> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(info->GetMessage(i, message, &size)))
                    SKSE::log::error("[PRESENT0][D3D12] id={} severity={} {}", static_cast<unsigned>(message->ID),
                        static_cast<unsigned>(message->Severity), message->pDescription);
            }
        } else {
            SKSE::log::warn("[PRESENT0] D3D12 validation queue unavailable (debug layer was not enabled before device creation)");
        }
        using GetDebug = HRESULT(WINAPI*)(UINT, REFIID, void**);
        const auto debug = reinterpret_cast<GetDebug>(GetProcAddress(GetModuleHandleW(L"dxgi.dll"), "DXGIGetDebugInterface1"));
        winrt::com_ptr<IDXGIInfoQueue> dxgi;
        if (debug && SUCCEEDED(debug(0, IID_PPV_ARGS(dxgi.put())))) {
            const auto count = dxgi->GetNumStoredMessages(DXGI_DEBUG_ALL);
            for (UINT64 i = count > 24 ? count - 24 : 0; i < count; ++i) {
                SIZE_T size = 0;
                if (FAILED(dxgi->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &size))) continue;
                std::vector<char> storage(size);
                auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(storage.data());
                if (SUCCEEDED(dxgi->GetMessage(DXGI_DEBUG_ALL, i, message, &size)))
                    SKSE::log::error("[PRESENT0][DXGI] id={} {}", message->ID, message->pDescription);
            }
        }
    }

    HRESULT DX12SwapChain::PresentPlain(UINT sync, UINT flags)
    {
        // TEST does not consume a render frame, allocator, or fence value.
        if (flags & DXGI_PRESENT_TEST) return swapChain->Present(sync, flags);
        frameIndex = swapChain->GetCurrentBackBufferIndex();
        const UINT usedIndex = frameIndex;
        auto fail = [&](HRESULT hr) {
            LogPlainFailure(hr, sync, flags, usedIndex);
            return hr; // Preserve the actual DXGI/HRESULT; do not report E_FAIL/S_OK.
        };
        if (frameIndex >= swapChainDesc.BufferCount || frameIndex >= 3) return fail(E_UNEXPECTED);
        m_stage = "plain allocator completion";
        HRESULT hr = PlainPresent::Wait(plainCompletion.get(), plainSubmitted[frameIndex], kFenceWaitTimeoutMs);
        if (FAILED(hr)) return fail(hr); // Never reset an allocator after a timed-out wait.
        m_stage = "plain allocator Reset";
        if (FAILED(hr = commandAllocators[frameIndex]->Reset())) return fail(hr);
        if (FAILED(hr = commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr))) return fail(hr);
        m_stage = "plain copy to current backbuffer";
        hr = PlainPresent::RecordCopy(commandLists[frameIndex].get(), colorOutWrapped->resource.get(),
            swapChainBuffers[frameIndex].get());
        if (FAILED(hr)) {
            commandLists[frameIndex]->Close();
            return fail(hr);
        }
        // This signal must actually be submitted: we replace D3D11 Present,
        // which normally provides the implicit flush of the immediate context.
        const UINT64 ready = fenceValue++;
        const UINT64 complete = fenceValue++;
        m_stage = "plain D3D11 release";
        if (FAILED(hr = d3d11Context->Signal(d3d11Fence.get(), ready))) return fail(hr);
        d3d11Context->Flush();
        if (FAILED(hr = commandQueue->Wait(d3d12Fence.get(), ready))) return fail(hr);
        const UINT64 submitted = ++plainSerial;
        plainSubmitted[frameIndex] = submitted;
        m_stage = "plain Submit";
        hr = PlainPresent::Submit(commandQueue.get(), commandLists[frameIndex].get(), plainCompletion.get(), submitted, [&]() {
            // Finish interop ordering BEFORE calling DXGI. A rejected Present
            // must not skip the completion signal/wait and poison later frames.
            HRESULT order = commandQueue->Signal(d3d12Fence.get(), complete);
            if (FAILED(order)) return order;
            order = d3d11Context->Wait(d3d11Fence.get(), complete);
            if (FAILED(order)) return order;
            d3d11Context->Flush();
            m_stage = "plain d3d12 Present";
            return swapChain->Present(sync, flags);
        });
        frameIndex = swapChain->GetCurrentBackBufferIndex();
        if (FAILED(hr)) return fail(hr);
        if (plainSerial <= 3 || plainSerial % 600 == 0)
            SKSE::log::info("[PRESENT0] hr=0x{:08X} frame={} used/next={}/{} completed={} sync={} flags=0x{:x}",
                static_cast<unsigned>(hr), plainSerial, usedIndex, frameIndex,
                plainCompletion->GetCompletedValue(), sync, flags);
        plainFailures = 0;
        return hr; // Includes DXGI_STATUS_OCCLUDED, not fabricated success.
    }

    void DX12SwapChain::RefreshPlainBuffers()
    {
        DX::ThrowIfFailed(swapChain->GetDesc1(&swapChainDesc));
        if (swapChainDesc.BufferCount < 2 || swapChainDesc.BufferCount > 3)
            DX::ThrowIfFailed(E_UNEXPECTED);
        for (UINT i = 0; i < swapChainDesc.BufferCount; ++i)
            DX::ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffers[i].put())));
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    HRESULT DX12SwapChain::ResizePlain(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
    {
        std::unique_lock<std::mutex> lock(plainMutex, std::try_to_lock);
        if (!lock.owns_lock()) return DXGI_ERROR_WAS_STILL_DRAWING;
        if (count > 3) return DXGI_ERROR_INVALID_CALL;
        // The D3D11-facing proxy exposes a single logical buffer. A legacy
        // request for one buffer still needs two physical flip-model buffers.
        count = PlainPresent::ResizeCount(count, swapChainDesc.BufferCount);
        flags = PlainPresent::ResizeFlags(flags, swapChainDesc.Flags);
        HRESULT hr = PlainPresent::Wait(plainCompletion.get(), plainSerial, kFenceWaitTimeoutMs);
        if (FAILED(hr)) return hr;
        for (UINT i = 0; i < 3; ++i) {
            if (FAILED(hr = commandAllocators[i]->Reset())) return hr;
            if (FAILED(hr = commandLists[i]->Reset(commandAllocators[i].get(), nullptr))) return hr;
            if (FAILED(hr = commandLists[i]->Close())) return hr;
            swapChainBuffers[i] = nullptr;
        }
        hr = swapChain->ResizeBuffers(count, width, height, format, flags);
        try {
            RefreshPlainBuffers(); // Reacquire old buffers too when resize was rejected.
            if (FAILED(hr)) {
                SKSE::log::error("[PRESENT0] ResizeBuffers rejected hr=0x{:08X}", static_cast<unsigned>(hr));
                return hr;
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = swapChainDesc.Width;
            desc.Height = swapChainDesc.Height;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = swapChainDesc.Format;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            auto back = std::make_unique<WrappedResource>(desc, d3d11Device.get(), d3d12Device.get());
            desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            auto color = std::make_unique<WrappedResource>(desc, d3d11Device.get(), d3d12Device.get());
            auto nr = std::make_unique<WrappedResource>(desc, d3d11Device.get(), d3d12Device.get());
            delete swapChainBufferWrapped;
            delete colorOutWrapped;
            delete nrOutWrapped;
            swapChainBufferWrapped = back.release();
            colorOutWrapped = color.release();
            nrOutWrapped = nr.release();
            delete depthWrapped; depthWrapped = nullptr;
            delete mvecWrapped; mvecWrapped = nullptr;
            Get().screenWidth = static_cast<float>(swapChainDesc.Width);
            Get().screenHeight = static_cast<float>(swapChainDesc.Height);
            SKSE::log::info("[PRESENT0] resized: {}x{} buffers={} index={}",
                swapChainDesc.Width, swapChainDesc.Height, swapChainDesc.BufferCount, frameIndex);
            return S_OK;
        } catch (const DX::com_exception& e) {
            SKSE::log::error("[PRESENT0] ResizeBuffers resource rebuild failed: {}", e.what());
            return E_FAIL;
        }
    }
}
