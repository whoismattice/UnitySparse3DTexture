#pragma once
#include "IHeap.h"
#include <vector>
#include <queue>

class FixedHeap : public IHeap {
public:
	FixedHeap(ID3D12Device* device, UINT64 sizeInBytes);
	~FixedHeap() override;

	TileAllocation AllocateTiles(UINT numTiles) override;
	void FreeTiles(UINT offsetInTiles, UINT numTiles) override;

	ID3D12Heap* GetD3D12Heap() const override { return m_heap; }
	UINT GetTotalCapacityInTiles() const override { return m_totalTiles; }
	UINT GetUsedTiles() const override { return m_usedTiles; }
	UINT GetFreeTiles() const override { return m_totalTiles - m_usedTiles; }
	bool CanAllocate(UINT numTiles) const override;

private:
	ID3D12Heap* m_heap;
	UINT m_totalTiles;
	UINT m_usedTiles;

	struct FreeBlock {
		UINT offset;
		UINT count;

		bool operator<(const FreeBlock& other) const {
			return offset < other.offset;
		}

	};

	std::vector<FreeBlock> m_freeBlocks;

	void CoalesceFreeBlocks();
};
