// Windows: whether every video card of the computer has Direct3D 12 at feature level 12_0 (the renderer's Automatic
// tries Direct3D 12 first then: it uses about a third of Vulkan's video memory). The cards whose Direct3D 12 stopped
// the whole computer had feature level 11_0 (0.8.5): they keep Vulkan first. The DLLs load here, so the program
// starts on a Windows without them.
// To ask a card opens its driver (about 100 ms of the start for a fast card). `cards` gets the cards and their driver
// versions when they all have it; with the same text as `known` (an earlier start's `cards`) no card is asked again.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi.h>

namespace stardraft::render {

bool allCardsHaveD3d12FeatureLevel12(const std::string& known, std::string& cards) {
    cards.clear();
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    bool all = false;
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    using CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    const auto createFactory = dxgi ? reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
    const auto createDevice = d3d12 ? reinterpret_cast<CreateDevice>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice"))) : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (createFactory && createDevice && SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        std::vector<IDXGIAdapter1*> list;  // (not the software devices)
        std::string now;                   // each card and its driver's version ("": a card did not say)
        bool said = true;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                adapter->Release();
                continue;
            }
            list.push_back(adapter);
            LARGE_INTEGER version{};
            said = said && SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version));
            char text[96];
            std::snprintf(text, sizeof text, "%04x:%04x:%08x:%02x:%016llx;", desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision,
                          static_cast<unsigned long long>(version.QuadPart));
            now += text;
        }
        if (!said) now.clear();
        all = !list.empty();
        if (now.empty() || now != known)
            for (IDXGIAdapter1* a : list)  // (no device is made: a null pointer asks only whether one could be)
                all = all && SUCCEEDED(createDevice(a, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr));
        for (IDXGIAdapter1* a : list) a->Release();
        factory->Release();
        if (all) cards = now;
    }
    if (d3d12) FreeLibrary(d3d12);
    if (dxgi) FreeLibrary(dxgi);
    return all;
}

}  // namespace stardraft::render
