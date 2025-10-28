#pragma once

#include "pch.h"
#include <d3d12.h>
#include <dxgi.h>
#include "IUnityInterface.h"
#include "IUnityGraphics.h"
#include "TilingInfo.h"



// C-style interface for C# to call into.
extern "C"
{
    // This function returns a pointer to the native rendering callback function.
    // C# will call this once to get the function pointer.
    UNITY_INTERFACE_EXPORT UnityRenderingEventAndData GetRenderEventAndDataCallback();

    // We will add stubs for resource creation later. For now, we declare them.
    // This function will eventually create the D3D12 resource and return a handle.
    // We use void* as a generic handle type for now.
    UNITY_INTERFACE_EXPORT ID3D12Resource* CreateVolumetricResource(int width, int height, int depth, int mipmapCount, DXGI_FORMAT format);

    // This function will release the native resource.
    UNITY_INTERFACE_EXPORT void DestroyVolumetricResource(ID3D12Resource* resource);

    UNITY_INTERFACE_EXPORT void GetResourceTilingInfo(ID3D12Resource* resource, ResourceTilingInfo* outInfo);

    UNITY_INTERFACE_EXPORT void GetAllSubresourceTilings(
        ID3D12Resource* resource,
        SubresourceTilingInfo* subresourceTilingArray,
        int arraySize
    );


    UNITY_INTERFACE_EXPORT bool MapTilesToHeap(
        ID3D12Resource* resource, 
        UINT subResource, 
        UINT tileX, UINT tileY, UINT tileZ, 
        UINT numTiles, 
        UINT heapOffsetInTiles, 
        ID3D12Heap* heap
    );

    UNITY_INTERFACE_EXPORT bool UnmapTiles(
        ID3D12Resource* resource, 
        UINT subResource, 
        UINT tileX, UINT tileY, UINT tileZ, 
        UINT numTiles
    );


    UNITY_INTERFACE_EXPORT UINT64 CreateSRVForResource(
        ID3D12Resource* resource,
        UINT descriptorIndex
    );


    UNITY_INTERFACE_EXPORT ID3D12Heap* GetTileHeap();

    UNITY_INTERFACE_EXPORT bool AllocateTilesFromHeap(
        UINT numTiles, UINT* outHeapOffset
    );


    UNITY_INTERFACE_EXPORT bool TestHeapFragmentation();

    UNITY_INTERFACE_EXPORT bool TestHeapBasicAllocation();

    UNITY_INTERFACE_EXPORT bool UploadDataToTile(
        ID3D12Resource* tiledResource,
        UINT subResource,
        UINT tileX, UINT tileY, UINT tileZ,
        void* sourceData,
        UINT dataSize);

    void InitializeDescriptorHeap();

    UINT GetBytesPerPixel(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 16;

        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32_FLOAT:
            return 8;

        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
            return 4;

        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_R8G8_UNORM:
            return 2;

        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SINT:
            return 1;

        default:
            // Format not handled or is block-compressed
            return 0;
        }
    }
}