#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>

namespace winmix::app::render {

// Owns the DXGI swap chain (flip model) and the D2D device context targeting
// it, per the port plan's choice of ID2D1DeviceContext over the older
// ID2D1HwndRenderTarget for better DWM composition and to avoid the classic
// resize white-flash.
class DeviceResources
{
public:
    explicit DeviceResources(HWND hwnd);

    void Resize(UINT width, UINT height);

    ID2D1DeviceContext* Context() const { return d2dContext_.Get(); }
    IDWriteFactory* DWriteFactory() const { return dwriteFactory_.Get(); }

    void BeginDraw();

    // Returns false if the device was lost and resources were recreated --
    // the caller should treat brushes/text formats as invalidated and skip
    // presenting this frame.
    bool EndDraw();

private:
    void CreateDeviceResources();
    void CreateWindowSizeDependentResources();

    HWND hwnd_;
    UINT width_ = 0;
    UINT height_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
};

} // namespace winmix::app::render
