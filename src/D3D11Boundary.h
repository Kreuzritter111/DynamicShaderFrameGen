#pragma once

#include <REX/W32/D3D11.h>
#include <d3d11.h>

namespace FrameGen
{
    // CommonLib's REX declarations describe the same Windows COM interfaces,
    // but are distinct C++ types. Convert only at the native SDK boundary;
    // static_cast cannot cross these parallel interface hierarchies.
    inline ID3D11Texture2D* NativeTexture(REX::W32::ID3D11Texture2D* texture) noexcept
    {
        static_assert(sizeof(REX::W32::ID3D11Texture2D) == sizeof(ID3D11Texture2D));
        return reinterpret_cast<ID3D11Texture2D*>(texture);
    }

    inline ID3D11ShaderResourceView* NativeView(REX::W32::ID3D11ShaderResourceView* view) noexcept
    {
        static_assert(sizeof(REX::W32::ID3D11ShaderResourceView) == sizeof(ID3D11ShaderResourceView));
        return reinterpret_cast<ID3D11ShaderResourceView*>(view);
    }
}
