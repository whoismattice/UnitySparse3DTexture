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


    UNITY_INTERFACE_EXPORT bool TestHeapFragmentation();

    UNITY_INTERFACE_EXPORT bool TestHeapBasicAllocation();

    void InitializeDescriptorHeap();
}