#pragma once
#include <d3d12.h>
#include <dxgi.h>
#include "IUnityLog.h"
#include "IHeap.h"
#include "ReservedResource.h"
#include <string>
#include <vector>
#include <functional>

struct IUnityGraphicsD3D12v6;

struct DiagnosticResult {
    const char* name;
    bool passed;
    std::string message;
};

namespace Diagnostics {

// Tier-1 checks (fast, non-destructive, safe to run at startup)

DiagnosticResult CheckFeatureSupport(ID3D12Device* device, IUnityLog* log);

DiagnosticResult CheckHeapIntegrity(IHeap* heap, IUnityLog* log);

DiagnosticResult CheckUploadBuffers(
    ID3D12Resource* const* uploadBuffers,
    UINT count,
    IUnityLog* log);

DiagnosticResult CheckDeviceAndQueue(
    ID3D12Device* device,
    IUnityGraphicsD3D12v6* graphicsD3D12,
    IUnityLog* log);

// Runs all tier-1 checks
std::vector<DiagnosticResult> RunStartupChecks(
    ID3D12Device* device,
    IHeap* heap,
    ID3D12Resource* const* uploadBuffers,
    UINT uploadBufferCount,
    IUnityGraphicsD3D12v6* graphicsD3D12,
    IUnityLog* log);

// Tier-2 smoke test callbacks (exercises full resource lifecycle)
struct SmokeTestOps {
    std::function<ReservedResource*(UINT, UINT, UINT, bool, UINT, DXGI_FORMAT)> createResource;
    std::function<bool(ReservedResource*)> destroyResource;
    std::function<bool(ReservedResource*, UINT, UINT, UINT, UINT, void*, UINT)> uploadData;
    std::function<bool(ReservedResource*, UINT, UINT, UINT, UINT)> unmapTile;
    std::function<bool(ReservedResource*, UINT, UINT, UINT, UINT, UINT, UINT, UINT, void*, UINT)> uploadTileBox;
};

// Runs the full-pipeline smoke test using the provided callbacks
std::vector<DiagnosticResult> RunSmokeTest(
    const SmokeTestOps& ops,
    IHeap* heap,
    IUnityLog* log);

// Returns true if all results in the vector passed
inline bool AllPassed(const std::vector<DiagnosticResult>& results)
{
    for (const auto& r : results)
    {
        if (!r.passed) return false;
    }
    return true;
}

} // namespace Diagnostics
