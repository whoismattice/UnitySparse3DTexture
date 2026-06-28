#include "pch.h"
#include "SparseTextureInterface.h"
#include "ReservedResource.h"
#include "PluginFacade.h"

// C export wrapping ReservedResource::IsTileMapped.
// Returns false on null resource.
extern "C" {

UNITY_INTERFACE_EXPORT bool IsTileMapped(
    ReservedResource* resource,
    UINT subresource, UINT tileX, UINT tileY, UINT tileZ)
{
    if (!resource) return false;
    return resource->IsTileMapped(subresource, tileX, tileY, tileZ);
}

} // extern "C"

// Bridge: converts ResourceTilingInfo (complex, from TilingInfo.h) into
// SparseTexture_TileInfo (simple, from SparseTextureInterface.h).
// The function table's GetResourceTilingInfo uses this; the existing
// P/Invoke export GetResourceTilingInfo fills the complex struct instead.
static void GetSimpleResourceTilingInfo(
    ReservedResource* resource,
    SparseTexture_TileInfo* outInfo)
{
    if (!resource || !outInfo) return;

    const auto& tilingInfo = resource->GetTilingInfo();
    outInfo->TileShapeX = tilingInfo.TileWidthInTexels;
    outInfo->TileShapeY = tilingInfo.TileHeightInTexels;
    outInfo->TileShapeZ = tilingInfo.TileDepthInTexels;
    outInfo->TileSizeInBytes = 65536;
}

extern "C" {

UNITY_INTERFACE_EXPORT void GetFunctionTable(
    ReservedResource* resource,
    SparseTextureFunctionTable* outTable)
{
    if (!outTable) return;

    outTable->UploadDataToTile      = &UploadDataToTile;
    outTable->UnmapTile             = &UnmapTile;
    outTable->IsTileMapped          = &IsTileMapped;
    outTable->GetResourceTilingInfo = &GetSimpleResourceTilingInfo;
    outTable->resource              = resource;
}

} // extern "C"
