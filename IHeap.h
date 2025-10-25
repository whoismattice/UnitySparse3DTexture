#pragma once
#include <d3d12.h>
#include <cstdint>

// Represents a tile allocation result
struct TileAllocation {
    UINT heapOffsetInTiles;
    ID3D12Heap* heap;
    bool success;
};

// Abstract interface for heap management
class IHeap {
public:
    virtual ~IHeap() = default;

    // Allocate space for N tiles, returns offset in tiles
    virtual TileAllocation AllocateTiles(UINT numTiles) = 0;

    // Free tiles at the given offset
    virtual void FreeTiles(UINT offsetInTiles, UINT numTiles) = 0;

    // Get the underlying D3D12 heap
    virtual ID3D12Heap* GetD3D12Heap() const = 0;

    // Query capacity and usage
    virtual UINT GetTotalCapacityInTiles() const = 0;
    virtual UINT GetUsedTiles() const = 0;
    virtual UINT GetFreeTiles() const = 0;

    // Check if allocation would succeed without actually allocating
    virtual bool CanAllocate(UINT numTiles) const = 0;
};