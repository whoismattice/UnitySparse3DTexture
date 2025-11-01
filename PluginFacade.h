#pragma once

#include "pch.h"

#include <d3d12.h>
#include <dxgi.h>
#include "IUnityInterface.h"
#include "IUnityGraphics.h"
#include "TilingInfo.h"
#include "ReservedResource.h"


// C-style interface for C# to call into.
extern "C"
{
    UNITY_INTERFACE_EXPORT ReservedResource* CreateVolumetricResource(UINT width, UINT height, UINT depth, bool useMipmaps, UINT mipmapCount, DXGI_FORMAT format);

    UNITY_INTERFACE_EXPORT bool TiledResourceSupport();


    // This function will release the native resource.
    UNITY_INTERFACE_EXPORT bool DestroyVolumetricResource(ReservedResource* resource);

    UNITY_INTERFACE_EXPORT void GetResourceTilingInfo(ReservedResource* resource, C_ResourceTilingInfo* outInfo);

    UNITY_INTERFACE_EXPORT ID3D12Resource* GetPointerToD3D12Resource(ReservedResource* resource);

    UNITY_INTERFACE_EXPORT bool UploadDataToTile(
        ReservedResource* reservedResource,
        UINT subResource,
        UINT tileX, UINT tileY, UINT tileZ,
        void* sourceData,
        UINT dataSize);

    UNITY_INTERFACE_EXPORT bool UnmapTile(
        ReservedResource* reservedResource,
        UINT subresource,
        UINT tileX, UINT tileY, UINT tileZ
    );
}