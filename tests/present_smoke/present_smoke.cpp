#include "PlainPresent.h"
#include <d3d11_4.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;
namespace PP = FrameGen::PlainPresent;
struct Failure { HRESULT hr; const char* operation; };
void Check(HRESULT hr, const char* operation) { std::cerr << (FAILED(hr) ? "error: " : "stage: ") << operation << " hr=0x" << std::hex << static_cast<unsigned>(hr) << std::dec << "\n"; if (FAILED(hr)) throw Failure{hr, operation}; }
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

class Smoke
{
public:
    HWND window = nullptr;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12InfoQueue> info;
    ComPtr<ID3D12InfoQueue1> callbackInfo;
    DWORD callbackCookie = 0;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12Fence> shared, complete;
    ComPtr<ID3D11Device5> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D11Fence> shared11;
    ComPtr<ID3D11Texture2D> source11;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D12Resource> source, readback;
    std::array<ComPtr<ID3D12CommandAllocator>, 3> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, 3> lists;
    std::array<ComPtr<ID3D12Resource>, 3> buffers;
    std::array<UINT64, 3> submitted{};
    UINT width = 160, height = 90;
    UINT64 serial = 0, timeline = 1;
    unsigned flipped = 0;

    Smoke()
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        else std::cout << "Debug layer unavailable: validation-message coverage not exercised\n";
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        ComPtr<IDXGIAdapter> adapter;
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "WARP D3D12 device");
        device.As(&info);
        if (info) {
            info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        }
        if (SUCCEEDED(device.As(&callbackInfo))) {
            Check(callbackInfo->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
                D3D12_MESSAGE_ID id, LPCSTR description, void*) {
                    if (severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
                        std::fprintf(stderr, "validation: id=%u severity=%u %s\n", static_cast<unsigned>(id),
                            static_cast<unsigned>(severity), description);
                        std::fflush(stderr);
                    }
                }, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, nullptr, &callbackCookie), "validation callback");
        }
        D3D12_COMMAND_QUEUE_DESC q{};
        Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> ctx;
        Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &base, nullptr, &ctx), "WARP D3D11 device");
        Check(base.As(&device11), "D3D11Device5");
        Check(ctx.As(&context11), "D3D11Context4");
        Check(device->CreateFence(1, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&shared)), "shared fence");
        HANDLE handle = nullptr;
        Check(device->CreateSharedHandle(shared.Get(), nullptr, GENERIC_ALL, nullptr, &handle), "fence handle");
        HRESULT hr = device11->OpenSharedFence(handle, IID_PPV_ARGS(&shared11));
        CloseHandle(handle);
        Check(hr, "OpenSharedFence");
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&complete)), "completion fence");
        for (unsigned i = 0; i < 3; ++i) {
            Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])), "allocator");
            Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(), nullptr,
                IID_PPV_ARGS(&lists[i])), "list");
            Check(lists[i]->Close(), "initial Close");
        }
        window = CreateWindowExW(0, L"STATIC", L"FG=0 WARP presentation test", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) throw Failure{HRESULT_FROM_WIN32(GetLastError()), "HWND"};
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width; desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGIFactory5> f5;
        BOOL tearing = FALSE;
        if (SUCCEEDED(factory.As(&f5)) && SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &tearing, sizeof(tearing))) && tearing) desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &chain), "CreateSwapChainForHwnd");
        Check(chain.As(&swap), "swapchain3");
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        Buffers();
        void* identity = nullptr;
        Check(PP::QueryBaseSwapChain(swap.Get(), __uuidof(IUnknown), &identity), "proxy IUnknown policy");
        Require(identity == static_cast<IUnknown*>(swap.Get()), "proxy identity changed");
        static_cast<IUnknown*>(identity)->Release();
        identity = nullptr;
        Require(PP::QueryBaseSwapChain(swap.Get(), __uuidof(IDXGISwapChain1), &identity) == E_NOINTERFACE && !identity,
            "base-only proxy must not expose extended backend interfaces");
        Require(PP::QueryBaseSwapChain(swap.Get(), __uuidof(IUnknown), nullptr) == E_POINTER, "QI null out pointer");
        Resources();
    }
    ~Smoke()
    {
        // Present may append GPU work after the copy-completion fence. Drain
        // before resource destruction, including when a checked call throws.
        if (queue && complete && SUCCEEDED(queue->Signal(complete.Get(), ++serial)))
            PP::Wait(complete.Get(), serial, 5000);
        if (callbackInfo) callbackInfo->UnregisterMessageCallback(callbackCookie);
        if (window) DestroyWindow(window);
    }

    void Buffers()
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        Check(swap->GetDesc1(&desc), "backbuffer descriptor");
        Require(desc.BufferCount <= buffers.size(), "too many DXGI buffers");
        for (UINT i = 0; i < desc.BufferCount; ++i)
            Check(swap->GetBuffer(i, IID_PPV_ARGS(&buffers[i])), "retain backbuffer");
    }

    void Resources()
    {
        source.Reset(); source11.Reset(); rtv.Reset(); readback.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Check(device11->CreateTexture2D(&desc, nullptr, &source11), "shared texture");
        Check(device11->CreateRenderTargetView(source11.Get(), nullptr, &rtv), "RTV");
        ComPtr<IDXGIResource1> dxgi;
        Check(source11.As(&dxgi), "IDXGIResource1");
        HANDLE handle = nullptr;
        Check(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            nullptr, &handle), "texture handle");
        HRESULT hr = device->OpenSharedHandle(handle, IID_PPV_ARGS(&source));
        CloseHandle(handle); Check(hr, "D3D12 OpenSharedHandle");
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = ((width * 4 + 255) & ~255u) * height;
        buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "readback");
    }

    void Frame(bool reject = false)
    {
        MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        const UINT index = swap->GetCurrentBackBufferIndex();
        Require(index < allocators.size(), "DXGI returned an invalid buffer index");
        std::cerr << "frame: " << serial << " index=" << index << " allocator=" << allocators[index].Get()
            << " need=" << submitted[index] << " completed=" << complete->GetCompletedValue() << "\n";
        Check(PP::Wait(complete.Get(), submitted[index], 5000), "allocator completion");
        Check(allocators[index]->Reset(), "allocator Reset");
        Check(lists[index]->Reset(allocators[index].Get(), nullptr), "list Reset");
        const unsigned component = static_cast<unsigned>(serial % 3);
        float color[4]{0, 0, 0, 1}; color[component] = 1;
        context11->ClearRenderTargetView(rtv.Get(), color);
        const UINT64 ready = ++timeline, done = ++timeline;
        Check(context11->Signal(shared11.Get(), ready), "D3D11 Signal");
        context11->Flush();
        Check(queue->Wait(shared.Get(), ready), "D3D12 Wait");
        const auto& back = buffers[index];
        Require(back != nullptr, "missing current backbuffer");
        Check(PP::RecordCopy(lists[index].Get(), source.Get(), back.Get()), "RecordCopy");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = back.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        lists[index]->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        const auto backDesc = back->GetDesc();
        device->GetCopyableFootprints(&backDesc, 0, 1, 0, &dst.PlacedFootprint, nullptr, nullptr, nullptr);
        src.pResource = back.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        lists[index]->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        lists[index]->ResourceBarrier(1, &barrier);
        submitted[index] = ++serial;
        const auto result = PP::Submit(queue.Get(), lists[index].Get(), complete.Get(), serial, [&]() {
            Check(queue->Signal(shared.Get(), done), "D3D12 Signal");
            Check(context11->Wait(shared11.Get(), done), "D3D11 Wait");
            context11->Flush();
            return reject ? DXGI_ERROR_INVALID_CALL : swap->Present(1, 0);
        });
        if (reject) Require(result == DXGI_ERROR_INVALID_CALL, "injected Present error was swallowed");
        else { Check(result, "Present(1,0)"); if (result == S_OK) ++flipped; }
        Check(PP::Wait(complete.Get(), serial, 5000), "completion after Present (including failure)");
        void* data = nullptr;
        D3D12_RANGE range{0, 4}; Check(readback->Map(0, &range, &data), "Map readback");
        const auto* pixel = static_cast<const unsigned char*>(data);
        const bool matches = pixel[0] == (component == 0 ? 255 : 0) && pixel[1] == (component == 1 ? 255 : 0) &&
            pixel[2] == (component == 2 ? 255 : 0) && pixel[3] == 255;
        D3D12_RANGE written{0, 0}; readback->Unmap(0, &written);
        Require(matches, "backbuffer pixel is stale/wrong after cross-API copy");
    }

    void Resize(UINT requested)
    {
        Check(queue->Signal(complete.Get(), ++serial), "drain after Present");
        Check(PP::Wait(complete.Get(), serial, 5000), "idle before resize");
        for (unsigned i = 0; i < 3; ++i) {
            Check(allocators[i]->Reset(), "resize allocator");
            Check(lists[i]->Reset(allocators[i].Get(), nullptr), "resize list");
            Check(lists[i]->Close(), "resize Close");
            buffers[i].Reset();
        }
        width += 32; height += 18;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        Check(swap->GetDesc1(&desc), "resize descriptor");
        const auto count = PP::ResizeCount(requested, desc.BufferCount);
        Check(swap->ResizeBuffers(count, width, height, DXGI_FORMAT_UNKNOWN,
            PP::ResizeFlags(0, desc.Flags)), "ResizeBuffers");
        Buffers();
        Resources();
    }

    void Validation()
    {
        if (!info) return;
        unsigned errors = 0;
        const auto count = info->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(info->GetMessage(i, message, &size), "debug message");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                std::cerr << message->pDescription << '\n'; ++errors;
            }
        }
        Require(!errors, "D3D12 validation errors");
        std::cout << "D3D12 validation: zero errors\n";
    }
};

int main()
{
    AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* exception) -> LONG {
        const auto* record = exception->ExceptionRecord;
        if (record->ExceptionCode != 0x406D1388 && record->ExceptionCode != 0x40010006) {
            std::fprintf(stderr, "native exception: code=0x%08lX address=%p\n", record->ExceptionCode,
                record->ExceptionAddress);
            std::fflush(stderr);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
    std::cerr << "stage: entered WARP main\n";
    try {
        Smoke smoke;
        for (unsigned i = 0; i < 6; ++i) smoke.Frame();
        smoke.Frame(true); // A failed Present must still complete submitted GPU work.
        smoke.Frame();
        smoke.Resize(1);
        for (unsigned i = 0; i < 2; ++i) smoke.Frame();
        smoke.Resize(3);
        for (unsigned i = 0; i < 4; ++i) smoke.Frame();
        smoke.Validation();
        std::cout << "14 frames verified by pixel readback; Present(1,0) successes=" << smoke.flipped
            << "; failure recovery and legacy 1->2 / 3-buffer resize verified\n";
        if (!smoke.flipped) { std::cout << "SKIP: window presentation occluded in this runner session\n"; return 77; }
        return 0;
    } catch (const Failure& failure) {
        std::cerr << "error: " << failure.operation << " hr=0x" << std::hex << static_cast<unsigned>(failure.hr) << '\n';
        if (failure.hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || failure.hr == DXGI_ERROR_UNSUPPORTED) {
            std::cout << "SKIP: WARP/window capability unavailable at " << failure.operation << "\n"; return 77;
        }
        return 1;
    } catch (const std::exception& failure) { std::cerr << "error: " << failure.what() << '\n'; return 1; }
}
