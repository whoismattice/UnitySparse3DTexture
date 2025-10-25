#include "pch.h"

#include "FixedHeap.h"
#include <algorithm>

FixedHeap::FixedHeap(ID3D12Device* device, UINT64 sizeInBytes)
	: m_heap(nullptr), m_usedTiles(0)
{
	// Align memory size to 64kb pages
	sizeInBytes = (sizeInBytes + 65535) & ~65535;
	m_totalTiles = static_cast<UINT>(sizeInBytes / 65536);

	D3D12_HEAP_DESC heapDesc = {};
	heapDesc.SizeInBytes = sizeInBytes;
	heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;

	HRESULT hr = device->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_heap));

	if (SUCCEEDED(hr)) {
		m_freeBlocks.push_back({ 0, m_totalTiles });
	}
}


FixedHeap::~FixedHeap()
{
	if (m_heap) {
		m_heap->Release();
		m_heap = nullptr;
	}
}

TileAllocation FixedHeap::AllocateTiles(UINT numTiles)
{
	TileAllocation result = { 0, nullptr, false };

	for (auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it)
	{
		if (it->count >= numTiles) {
			result.heapOffsetInTiles = it->offset;
			result.heap = m_heap;
			result.success = true;

			m_usedTiles += numTiles;

			if (it->count == numTiles) {
				m_freeBlocks.erase(it);
			}
			else {
				it->offset += numTiles;
				it->count -= numTiles;
			}

			return result;
		}
	}
	return result;
}

void FixedHeap::FreeTiles(UINT offsetInTiles, UINT numTiles)
{
	m_usedTiles -= numTiles;

	m_freeBlocks.push_back({ offsetInTiles, numTiles });

	CoalesceFreeBlocks();
}

void FixedHeap::CoalesceFreeBlocks()
{
	if (m_freeBlocks.size() <= 1)
		return;

	std::sort(m_freeBlocks.begin(), m_freeBlocks.end());

	std::vector<FreeBlock> merged;
	merged.push_back(m_freeBlocks[0]);

	for (size_t i = 1; i < m_freeBlocks.size(); i++)
	{
		FreeBlock& last = merged.back();
		FreeBlock current = m_freeBlocks[i];
		if (last.offset + last.count == current.offset)
		{
			last.count += current.count;
		}
		else {
			merged.push_back(current);
		}
	}
	m_freeBlocks = std::move(merged);
}

bool FixedHeap::CanAllocate(UINT numTiles) const {
	for (const auto& block : m_freeBlocks) {
		if (block.count >= numTiles)
			return true;
	}
	return false;
}
