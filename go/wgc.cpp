#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include "wgc.h"
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>
#include <future>
#include <thread>

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace winrt {
  struct
#if WINRT_IMPL_HAS_DECLSPEC_UUID
    __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
#endif
    IDirect3DDxgiInterfaceAccess: ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID id, void **object) = 0;
  };
}

#if !WINRT_IMPL_HAS_DECLSPEC_UUID
static constexpr GUID GUID__IDirect3DDxgiInterfaceAccess = {
  0xA9B3D012, 0x3DF2, 0x4EE3, { 0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1 }
};
template <>
constexpr auto
__mingw_uuidof<winrt::IDirect3DDxgiInterfaceAccess>() -> GUID const & {
  return GUID__IDirect3DDxgiInterfaceAccess;
}
#endif

typedef HRESULT (WINAPI *pCreateDirect3D11DeviceFromDXGIDevice)(IDXGIDevice*, IInspectable**);

static frame_callback_t g_callback = nullptr;
static void* g_user_data = nullptr;
static GraphicsCaptureSession g_session{ nullptr };
static Direct3D11CaptureFramePool g_frame_pool{ nullptr };
static Direct3D11CaptureFramePool::FrameArrived_revoker g_frame_arrived_revoker;

extern "C" int start_capture(frame_callback_t callback, void* user_data) {
    try {
        winrt::init_apartment();

        if (!GraphicsCaptureSession::IsSupported()) {
            return -1;
        }

        g_callback = callback;
        g_user_data = user_data;

        // 1. Create D3D11 Device
        winrt::com_ptr<ID3D11Device> d3d_device;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, d3d_device.put(), nullptr, nullptr);
        if (FAILED(hr)) return -2;

        // 2. Create WinRT Device
        winrt::com_ptr<IDXGIDevice> dxgi_device = d3d_device.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> inspectable;
        
        HMODULE d3d11_dll = GetModuleHandleW(L"d3d11.dll");
        if (!d3d11_dll) d3d11_dll = LoadLibraryW(L"d3d11.dll");
        
        if (d3d11_dll) {
            auto func = (pCreateDirect3D11DeviceFromDXGIDevice)GetProcAddress(d3d11_dll, "CreateDirect3D11DeviceFromDXGIDevice");
            if (func) {
                hr = func(dxgi_device.get(), inspectable.put());
            } else { return -3; }
        } else { return -3; }
        
        if (FAILED(hr)) return -4;
        IDirect3DDevice uwp_device = inspectable.as<IDirect3DDevice>();

        // 3. Select Primary Monitor
        HMONITOR hMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        
        // 4. Create Capture Item
        auto monitor_factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{ nullptr };
        hr = monitor_factory->CreateForMonitor(hMonitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(hr)) return -5;

        auto size = item.Size();

        // 5. Create Frame Pool
        g_frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(uwp_device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        
        // 6. Setup Event Handler
        g_frame_arrived_revoker = g_frame_pool.FrameArrived(winrt::auto_revoke, [](Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
            auto frame = sender.TryGetNextFrame();
            if (frame && g_callback) {
                auto size = frame.ContentSize();
                g_callback(size.Width, size.Height, winrt::get_abi(frame), g_user_data);
            }
        });

        // 7. Start Session
        g_session = g_frame_pool.CreateCaptureSession(item);
        
        if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
            g_session.IsBorderRequired(false);
        }

        g_session.StartCapture();
        return 0;

    } catch (...) {
        return -99;
    }
}


extern "C" void stop_capture() {
    if (g_session) {
        g_session.Close();
        g_session = nullptr;
    }
    if (g_frame_pool) {
        g_frame_arrived_revoker.revoke();
        g_frame_pool.Close();
        g_frame_pool = nullptr;
    }
}

