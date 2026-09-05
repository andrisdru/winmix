#include "render/DeviceResources.h"

#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace winmix::app::render {

namespace {

void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(what);
    }
}

} // namespace

DeviceResources::DeviceResources(HWND hwnd) : hwnd_(hwnd)
{
    CreateDeviceResources();

    RECT rc;
    GetClientRect(hwnd_, &rc);
    width_ = static_cast<UINT>(rc.right - rc.left);
    height_ = static_cast<UINT>(rc.bottom - rc.top);

    CreateWindowSizeDependentResources();
}

void DeviceResources::CreateDeviceResources()
{
    const UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevel{};
    ThrowIfFailed(
        D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
            nullptr, 0, D3D11_SDK_VERSION,
            &d3dDevice_, &featureLevel, &d3dContext_),
        "D3D11CreateDevice");

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(d3dDevice_.As(&dxgiDevice), "QI(IDXGIDevice)");

    ThrowIfFailed(
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_)),
        "D2D1CreateFactory");

    ThrowIfFailed(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_), "CreateDevice(D2D)");
    ThrowIfFailed(
        d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_),
        "CreateDeviceContext");

    ThrowIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())),
        "DWriteCreateFactory");

    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> dxgiFactory;
    ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)), "GetParent(IDXGIFactory2)");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ThrowIfFailed(
        dxgiFactory->CreateSwapChainForHwnd(d3dDevice_.Get(), hwnd_, &desc, nullptr, nullptr, &swapChain_),
        "CreateSwapChainForHwnd");

    // This app is never fullscreened; disable DXGI's own Alt+Enter handling.
    dxgiFactory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
}

void DeviceResources::CreateWindowSizeDependentResources()
{
    d2dContext_->SetTarget(nullptr);
    targetBitmap_.Reset();

    ThrowIfFailed(swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");

    ComPtr<IDXGISurface> surface;
    ThrowIfFailed(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)), "GetBuffer");

    const float dpi = static_cast<float>(GetDpiForWindow(hwnd_));

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpi, dpi);

    ThrowIfFailed(
        d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &targetBitmap_),
        "CreateBitmapFromDxgiSurface");

    d2dContext_->SetTarget(targetBitmap_.Get());
    d2dContext_->SetDpi(dpi, dpi);
}

void DeviceResources::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0 || (width == width_ && height == height_))
    {
        return;
    }

    width_ = width;
    height_ = height;
    CreateWindowSizeDependentResources();
}

void DeviceResources::BeginDraw()
{
    d2dContext_->BeginDraw();
}

bool DeviceResources::EndDraw()
{
    const HRESULT hr = d2dContext_->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET)
    {
        targetBitmap_.Reset();
        d2dContext_.Reset();
        d2dDevice_.Reset();
        d2dFactory_.Reset();
        swapChain_.Reset();
        d3dContext_.Reset();
        d3dDevice_.Reset();

        CreateDeviceResources();
        CreateWindowSizeDependentResources();
        return false;
    }

    ThrowIfFailed(hr, "EndDraw");

    DXGI_PRESENT_PARAMETERS params{};
    ThrowIfFailed(swapChain_->Present1(1, 0, &params), "Present1");
    return true;
}

} // namespace winmix::app::render
