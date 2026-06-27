#include "pch.h"
#include "Diagnostics.h"
#include "IUnityGraphicsD3D12.h"
#include <format>

namespace Diagnostics {

static constexpr UINT64 TILE_SIZE_BYTES = 65536;
static constexpr UINT SMOKE_TEST_WIDTH = 64;
static constexpr UINT SMOKE_TEST_HEIGHT = 64;
static constexpr UINT SMOKE_TEST_DEPTH = 64;

DiagnosticResult CheckFeatureSupport(ID3D12Device* device, IUnityLog* log)
{
    if (!device)
    {
        return { "Feature Support", false, "Device pointer is null" };
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 supportLevel;
    HRESULT hr = device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5,
        &supportLevel,
        sizeof(supportLevel));

    if (FAILED(hr))
    {
        return { "Feature Support", false,
            std::format("CheckFeatureSupport query failed: 0x{:08x}", hr) };
    }

    if (!supportLevel.SRVOnlyTiledResourceTier3)
    {
        return { "Feature Support", false,
            "Tiled Resources Tier 3 not supported" };
    }

    return { "Feature Support", true,
        "Tiled Resources Tier 3 supported" };
}

DiagnosticResult CheckHeapIntegrity(IHeap* heap, IUnityLog* log)
{
    if (!heap)
    {
        return { "Heap Integrity", false, "Heap pointer is null" };
    }

    UINT capacity = heap->GetTotalCapacityInTiles();
    if (capacity == 0)
    {
        return { "Heap Integrity", false, "Heap has zero capacity" };
    }

    UINT freeTiles = heap->GetFreeTiles();
    UINT usedTiles = heap->GetUsedTiles();

    if (!heap->CanAllocate(1))
    {
        return { "Heap Integrity", false, "CanAllocate(1) returned false" };
    }

    TileAllocation alloc = heap->AllocateTiles(1);
    if (!alloc.success)
    {
        return { "Heap Integrity", false,
            "Failed to allocate a single tile" };
    }

    heap->FreeTiles(alloc.heapOffsetInTiles, 1);

    return { "Heap Integrity", true,
        std::format("{} total, {} used, {} free tiles — alloc/free OK",
            capacity, usedTiles, freeTiles) };
}

DiagnosticResult CheckUploadBuffers(
    ID3D12Resource* const* uploadBuffers,
    UINT count,
    IUnityLog* log)
{
    if (!uploadBuffers)
    {
        return { "Upload Buffers", false, "Upload buffer array is null" };
    }

    for (UINT i = 0; i < count; ++i)
    {
        if (!uploadBuffers[i])
        {
            return { "Upload Buffers", false,
                std::format("Upload buffer {} is null", i) };
        }

        D3D12_RESOURCE_DESC desc = uploadBuffers[i]->GetDesc();
        if (desc.Width < TILE_SIZE_BYTES)
        {
            return { "Upload Buffers", false,
                std::format("Upload buffer {} has size {} (expected >= {})",
                    i, desc.Width, TILE_SIZE_BYTES) };
        }
    }

    return { "Upload Buffers", true,
        std::format("All {} upload buffers valid", count) };
}

DiagnosticResult CheckDeviceAndQueue(
    ID3D12Device* device,
    IUnityGraphicsD3D12v6* graphicsD3D12,
    IUnityLog* log)
{
    if (!device)
    {
        return { "Device & Queue", false, "D3D12 device pointer is null" };
    }

    if (!graphicsD3D12)
    {
        return { "Device & Queue", false,
            "IUnityGraphicsD3D12v6 pointer is null" };
    }

    ID3D12CommandQueue* queue = graphicsD3D12->GetCommandQueue();
    if (!queue)
    {
        return { "Device & Queue", false, "Command queue is null" };
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = queue->GetDesc();
    const char* typeName = "Unknown";
    switch (queueDesc.Type)
    {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:  typeName = "Direct";  break;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE: typeName = "Compute"; break;
    case D3D12_COMMAND_LIST_TYPE_COPY:    typeName = "Copy";    break;
    }

    return { "Device & Queue", true,
        std::format("Device valid, command queue valid (type: {})", typeName) };
}

std::vector<DiagnosticResult> RunStartupChecks(
    ID3D12Device* device,
    IHeap* heap,
    ID3D12Resource* const* uploadBuffers,
    UINT uploadBufferCount,
    IUnityGraphicsD3D12v6* graphicsD3D12,
    IUnityLog* log)
{
    std::vector<DiagnosticResult> results;
    results.reserve(4);

    results.push_back(CheckDeviceAndQueue(device, graphicsD3D12, log));
    results.push_back(CheckFeatureSupport(device, log));
    results.push_back(CheckHeapIntegrity(heap, log));
    results.push_back(CheckUploadBuffers(uploadBuffers, uploadBufferCount, log));

    return results;
}

std::vector<DiagnosticResult> RunSmokeTest(
    const SmokeTestOps& ops,
    IHeap* heap,
    IUnityLog* log)
{
    std::vector<DiagnosticResult> results;
    results.reserve(8);

    // Create a minimal reserved resource
    ReservedResource* testResource = ops.createResource(
        SMOKE_TEST_WIDTH, SMOKE_TEST_HEIGHT, SMOKE_TEST_DEPTH,
        false, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM);

    if (!testResource)
    {
        results.push_back({ "Resource Creation", false,
            "Failed to create test reserved resource" });
        return results;
    }

    // Validate tiling info
    const ResourceTilingInfo& tiling = testResource->GetTilingInfo();
    if (tiling.TileWidthInTexels == 0 ||
        tiling.TileHeightInTexels == 0 ||
        tiling.TileDepthInTexels == 0)
    {
        results.push_back({ "Resource Creation", false,
            "Tiling info has zero dimensions after creation" });
        ops.destroyResource(testResource);
        return results;
    }

    results.push_back({ "Resource Creation", true,
        std::format("Created {}x{}x{} resource, tile shape {}x{}x{} texels",
            SMOKE_TEST_WIDTH, SMOKE_TEST_HEIGHT, SMOKE_TEST_DEPTH,
            tiling.TileWidthInTexels, tiling.TileHeightInTexels,
            tiling.TileDepthInTexels) });

    // Prepare 64 KiB of patterned test data
    std::vector<std::byte> testData(TILE_SIZE_BYTES);
    for (size_t i = 0; i < testData.size(); ++i)
    {
        testData[i] = static_cast<std::byte>(i & 0xFF);
    }

    // Upload to tile (0,0,0) of subresource 0
    bool uploadOk = ops.uploadData(
        testResource,
        0,          // subResource
        0, 0, 0,   // tileX, tileY, tileZ
        testData.data(),
        static_cast<UINT>(testData.size()));

    if (!uploadOk)
    {
        results.push_back({ "Tile Upload", false,
            "UploadDataToTile failed for tile (0,0,0)" });
        ops.destroyResource(testResource);
        return results;
    }

    results.push_back({ "Tile Upload", true,
        "Uploaded 64 KiB test pattern to tile (0,0,0)" });

    // Unmap the tile
    bool unmapOk = ops.unmapTile(
        testResource,
        0,          // subResource
        0, 0, 0);  // tileX, tileY, tileZ

    if (!unmapOk)
    {
        results.push_back({ "Tile Unmap", false,
            "UnmapTile failed for tile (0,0,0)" });
        ops.destroyResource(testResource);
        return results;
    }

    results.push_back({ "Tile Unmap", true,
        "Successfully unmapped tile (0,0,0)" });

    // Batch upload test: 2x2x2 box of tiles
    {
        UINT boxW = 2, boxH = 2, boxD = 2;
        UINT batchTileCount = boxW * boxH * boxD;
        UINT64 batchDataSize = batchTileCount * TILE_SIZE_BYTES;
        std::vector<std::byte> batchData(batchDataSize);
        for (size_t i = 0; i < batchData.size(); ++i)
            batchData[i] = static_cast<std::byte>((i + 1) & 0xFF);

        bool batchOk = ops.uploadTileBox(
            testResource,
            0,                              // subResource
            1, 0, 0,                       // start at tile (1,0,0)
            boxW, boxH, boxD,
            batchData.data(),
            static_cast<UINT>(batchData.size()));

        if (!batchOk)
        {
            results.push_back({ "Batch Upload", false,
                std::format("UploadDataToTileBox failed for {}x{}x{} box",
                    boxW, boxH, boxD) });
            ops.destroyResource(testResource);
            return results;
        }

        results.push_back({ "Batch Upload", true,
            std::format("Uploaded {} tiles ({}x{}x{} box) in one submission",
                batchTileCount, boxW, boxH, boxD) });

        // Unmap the batch tiles
        for (UINT z = 0; z < boxD; ++z)
            for (UINT y = 0; y < boxH; ++y)
                for (UINT x = 0; x < boxW; ++x)
                    ops.unmapTile(testResource, 0,
                        1 + x, 0 + y, 0 + z);
    }

    results.push_back({ "Batch Unmap", true,
        "Unmapped all batch tiles" });

    // Clean up
    bool destroyOk = ops.destroyResource(testResource);
    if (!destroyOk)
    {
        results.push_back({ "Resource Cleanup", false,
            "Failed to destroy test resource" });
        return results;
    }

    results.push_back({ "Resource Cleanup", true,
        "Destroyed test resource successfully" });

    return results;
}

} // namespace Diagnostics
