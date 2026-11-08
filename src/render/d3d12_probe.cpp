// Windows: whether every video card of the computer has Direct3D 12 at feature level 12_0 (the renderer's Automatic
// tries Direct3D 12 first then: it uses about a third of Vulkan's video memory). The cards whose Direct3D 12 stopped
// the whole computer had feature level 11_0 (0.8.5): they keep Vulkan first. The DLLs load here, so the program
// starts on a Windows without them.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d12.h>
#include <dxgi.h>

namespace stardraft::render {

bool allCardsHaveD3d12FeatureLevel12() {
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    bool all = false;
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    using CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    const auto createFactory = dxgi ? reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
    const auto createDevice = d3d12 ? reinterpret_cast<CreateDevice>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice"))) : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (createFactory && createDevice && SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        int cards = 0, good = 0;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                ++cards;
                // (no device is made: a null pointer asks only whether one could be)
                if (SUCCEEDED(createDevice(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) ++good;
            }
            adapter->Release();
        }
        factory->Release();
        all = cards > 0 && good == cards;
    }
    if (d3d12) FreeLibrary(d3d12);
    if (dxgi) FreeLibrary(dxgi);
    return all;
}

}  // namespace stardraft::render
