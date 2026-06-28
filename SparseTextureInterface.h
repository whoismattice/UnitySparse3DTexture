#pragma once
#include <cstdint>

// Opaque handle. Forward declaration only — neither plugin needs the full
// definition of ReservedResource to store or pass the pointer.
class ReservedResource;

// Hardware-dependent tile dimensions for a reserved resource's format.
// Query once at initialization; tile coordinates are derived from these values.
struct SparseTexture_TileInfo {
    uint32_t TileShapeX;
    uint32_t TileShapeY;
    uint32_t TileShapeZ;
    uint32_t TileSizeInBytes;   // Always 65536 (64KB) for standard sparse textures
};

// Function pointer table for cross-plugin calls.
// Populated by UnitySparse3DTexture's GetFunctionTable export.
// All pointers are non-owning; the table is immutable after initialization.
// Callers must null-check table->resource before invoking any function.
struct SparseTextureFunctionTable {
    // Upload data to a tile. Maps the tile if not already mapped.
    // Returns true on successful submission to the D3D12 command queue.
    // The upload is GPU-async — sourceData must remain valid until the GPU copy completes.
    bool (*UploadDataToTile)(
        ReservedResource* resource,
        uint32_t subResource,
        uint32_t tileX,
        uint32_t tileY,
        uint32_t tileZ,
        void* sourceData,
        uint32_t dataSize);

    // Unmap a tile, releasing its physical heap allocation for reuse.
    // Returns true on success.
    bool (*UnmapTile)(
        ReservedResource* resource,
        uint32_t subresource,
        uint32_t tileX,
        uint32_t tileY,
        uint32_t tileZ);

    // Query whether a tile is currently mapped in physical GPU memory.
    bool (*IsTileMapped)(
        ReservedResource* resource,
        uint32_t subresource,
        uint32_t tileX,
        uint32_t tileY,
        uint32_t tileZ);

    // Retrieve hardware-dependent tile dimensions for this resource.
    // Use these to convert voxel/chunk coordinates to tile coordinates.
    void (*GetResourceTilingInfo)(
        ReservedResource* resource,
        SparseTexture_TileInfo* outInfo);

    // The resource handle to pass to all other functions in this table.
    ReservedResource* resource;
};
