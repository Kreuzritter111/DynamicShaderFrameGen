// Native D3D11 DLAA before Skyrim post-processing (and before ENB/UI).
// Hook locations originate from the project's pre-v0.50 implementation.
#include "FrameGen.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

namespace FrameGen
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        bool hooksReady = false;
        bool resolveHealthy = false;
        ComPtr<ID3DDeviceContextState> isolated;
        ID3D11Device* stateDevice = nullptr;
        ComPtr<ID3D11Texture2D> output;
        D3D11_TEXTURE2D_DESC outputDesc{};

        struct UpdateJitter
        {
            static void thunk(RE::BSGraphics::State* state)
            {
                original(state);
                Get().UpdateNativeDlaa(state);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };
        struct PostProcessing
        {
            static void thunk(RE::ImageSpaceManager* manager, std::uint32_t arg,
                RE::RENDER_TARGET target, void* data, bool flag)
            {
                Get().DrawNativeDlaa();
                original(manager, arg, target, data, flag);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };
        template<class Hook>
        void PatchCall(std::uintptr_t address)
        {
            auto& trampoline = SKSE::GetTrampoline();
            Hook::original = trampoline.write_call<5>(address, Hook::thunk);
        }

        float Halton(std::uint32_t index, std::uint32_t base)
        {
            float value = 0.0f, fraction = 1.0f;
            while (index) {
                fraction /= static_cast<float>(base);
                value += fraction * static_cast<float>(index % base);
                index /= base;
            }
            return value - 0.5f;
        }
    }

    void FrameGen::InstallNativeDlaaHooks()
    {
        if (hooksReady || !settings.enableFrameGen || !settings.enableUpscale || settings.frameGeneration)
            return;
        const auto jitterCall = REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, 0xE2);
        const auto postCall = REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7);
        // Validate both before changing either: a half-installed temporal pipeline
        // would jitter the image without resolving it. Never patch a detoured site.
        if (*reinterpret_cast<const std::uint8_t*>(jitterCall) != 0xE8 ||
            *reinterpret_cast<const std::uint8_t*>(postCall) != 0xE8) {
            SKSE::log::error("[DLAA] hook sites changed; no DLAA hooks installed (jitter={:x}, post={:x})", jitterCall, postCall);
            return;
        }
        SKSE::AllocTrampoline(128);
        PatchCall<UpdateJitter>(jitterCall);
        PatchCall<PostProcessing>(postCall);
        hooksReady = true;
        SKSE::log::info("[DLAA] native hooks installed; input/output must match, FG/MFG disabled");
    }

    void FrameGen::UpdateNativeDlaa(RE::BSGraphics::State* state)
    {
        if (!hooksReady || !state || !streamline.featureDLSS || !settings.enableUpscale || !GetFrameBuffer())
            return;
        auto& rt = state->GetRuntimeData();
        rt.dynamicResolutionPreviousWidthRatio = rt.dynamicResolutionWidthRatio;
        rt.dynamicResolutionPreviousHeightRatio = rt.dynamicResolutionHeightRatio;
        rt.dynamicResolutionWidthRatio = rt.dynamicResolutionHeightRatio = 1.0f;
        rt.dynamicResolutionLock = 1;
        engineFrameCount = state->GetFrameCount();
        streamline.frameSerial = engineFrameCount;
        jitterX = resolveHealthy ? Halton(engineFrameCount % 8 + 1, 2) : 0.0f;
        jitterY = resolveHealthy ? Halton(engineFrameCount % 8 + 1, 3) : 0.0f;
        state->projectionPosScaleX = -2.0f * jitterX / std::max(1.0f, screenWidth);
        state->projectionPosScaleY =  2.0f * jitterY / std::max(1.0f, screenHeight);
    }

    void FrameGen::DrawNativeDlaa()
    {
        if (!hooksReady || !settings.enableUpscale || !streamline.featureDLSS || !GetFrameBuffer())
            return;
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer || !d3d11Device || !d3d11Context) return;
        auto& color = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
        auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
        auto& motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
        if (!color.texture || !depth.texture || !motion.texture) return;
        const bool wasHealthy = resolveHealthy;
        resolveHealthy = false;
        D3D11_TEXTURE2D_DESC desc{}, depthDesc{}, motionDesc{};
        static_cast<ID3D11Texture2D*>(color.texture)->GetDesc(&desc);
        static_cast<ID3D11Texture2D*>(depth.texture)->GetDesc(&depthDesc);
        static_cast<ID3D11Texture2D*>(motion.texture)->GetDesc(&motionDesc);
        const auto width = static_cast<std::uint32_t>(screenWidth);
        const auto height = static_cast<std::uint32_t>(screenHeight);
        if (desc.Width != width || desc.Height != height || desc.SampleDesc.Count != 1 ||
            depthDesc.Width != width || depthDesc.Height != height ||
            motionDesc.Width != width || motionDesc.Height != height) {
            static bool reported = false;
            if (!reported) {
                SKSE::log::error("[DLAA] refusing mismatched native inputs: color={}x{} depth={}x{} motion={}x{} output={}x{}",
                    desc.Width, desc.Height, depthDesc.Width, depthDesc.Height, motionDesc.Width, motionDesc.Height, width, height);
                reported = true;
            }
            return;
        }
        if (!output || outputDesc.Width != width || outputDesc.Height != height || outputDesc.Format != desc.Format) {
            output.Reset();
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = desc.MiscFlags = 0;
            desc.MipLevels = desc.ArraySize = 1;
            if (FAILED(d3d11Device->CreateTexture2D(&desc, nullptr, &output))) return;
            outputDesc = desc;
        }
        // NGX changes pipeline state. Swap into an isolated state and restore the
        // entire engine context, including bindings, before the original pass.
        ComPtr<ID3D11Device1> device;
        ComPtr<ID3D11DeviceContext1> context;
        ComPtr<ID3DDeviceContextState> saved;
        if (FAILED(d3d11Device->QueryInterface(IID_PPV_ARGS(&device))) ||
            FAILED(d3d11Context->QueryInterface(IID_PPV_ARGS(&context)))) return;
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL selected{};
        if (!isolated || stateDevice != d3d11Device) {
            isolated.Reset();
            if (FAILED(device->CreateDeviceContextState(0, levels, 2, D3D11_SDK_VERSION,
                    __uuidof(ID3D11Device), &selected, &isolated))) return;
            stateDevice = d3d11Device;
        }
        context->SwapDeviceContextState(isolated.Get(), &saved);
        const bool ok = streamline.EvaluateNativeDlaa(context.Get(), color.texture, output.Get(),
            depth.texture, motion.texture, width, height, *GetFrameBuffer(), settings);
        context->SwapDeviceContextState(saved.Get(), nullptr);
        resolveHealthy = ok;
        if (wasHealthy && !ok) SKSE::log::error("[DLAA] resolve failed; custom jitter disabled until recovery");
        if (ok) d3d11Context->CopyResource(color.texture, output.Get());
        static std::uint64_t frames = 0;
        if (++frames % 180 == 1)
            SKSE::log::info("[DLAA] evaluated={} native={}x{} frame={} FG=0", ok, width, height, engineFrameCount);
    }
}
