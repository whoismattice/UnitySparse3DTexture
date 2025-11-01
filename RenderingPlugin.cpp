#include "pch.h"
#include <stdexcept>
#include "FixedHeap.h"
#include <format>
#include "RenderingPlugin.h"



RenderingPlugin::RenderingPlugin(IUnityInterfaces* unityInterface) : s_UnityInterfaces(unityInterface) {



	try {
		s_Graphics = unityInterface->Get<IUnityGraphics>();
		s_Log = unityInterface->Get<IUnityLog>();

		
	}
	catch (const std::runtime_error& ex)
	{
		Log(ex.what());
	}
}

void RenderingPlugin::InitializeGraphicsDevice()
{
	try {
		// Get graphics device from Unity interface
		s_D3D12 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v6>();
		s_Device = s_D3D12->GetDevice();
		if (s_D3D12 == nullptr || s_Device == nullptr)
		{
			return;
		}

		// Create tile heap
		g_tileHeap = std::make_unique<FixedHeap>(s_Device, 512 * 1024 * 1024);


		// Create fence object
		HRESULT hr = s_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_uploadFence));
		if (FAILED(hr) || !m_uploadFence) {
			LogError("UploadDataToTile: CreateFence failed");
		}

		// Create allocator pool
		for (UINT i = 0; i < ALLOCATOR_POOL_SIZE; ++i) {
			s_Device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(m_uploadAllocators[i].GetAddressOf())
			);
			m_allocatorFenceValues[i] = 0;
		}


		Log("Found appropriate D3D12 device");
		Log("TODO: CHECK FEATURE LEVEL");
		initialized = true;



	}
	catch (const std::runtime_error& ex)
	{
		LogError(ex.what());
	}
}

void RenderingPlugin::Log(const std::string& message)
{
	UNITY_LOG(s_Log, message.c_str());
}

void RenderingPlugin::LogError(const std::string& message) 
{
	UNITY_LOG_ERROR(s_Log, message.c_str());
}

ReservedResource* RenderingPlugin::CreateVolumetricResource(
	UINT width, UINT height, UINT depth, 
	bool useMipmaps,
	UINT mipmapCount,
	DXGI_FORMAT format
) {
	if (!initialized) {
		LogError("CreateVolumetricResource called before plugin initialised with D3D12 device");
		return nullptr;
	}
	try {
		
		g_resources.push_back(std::make_unique<ReservedResource>(
			width, height, depth,
			useMipmaps,
			static_cast<UINT>(useMipmaps ? mipmapCount : 1),
			format, s_Device, s_Log
		));

		return g_resources.back().get();
	}
	catch (const std::runtime_error& ex)
	{
		LogError(ex.what());
		return nullptr;
	}
}

bool RenderingPlugin::DestroyVolumetricResource(ReservedResource* resource)
{
	try {
		auto it = std::find_if(g_resources.begin(), g_resources.end(),
			[resource](const std::unique_ptr<ReservedResource>& p) {
				return p.get() == resource;
			});
		if (it != g_resources.end())
		{
			g_resources.erase(it);
			return true;
		}
		else {
			LogError("Reserved resource not found");
			return false;
		}
	}
	catch (const std::exception& ex) {
		LogError(ex.what());
		return false;
	}
}
 
bool RenderingPlugin::MapTileToHeap(
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	UINT tileOffsetInHeap,
	ReservedResource* resource) {
	try {
		if (g_tileHeap == nullptr) return false;

		ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

		D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
		startCoord.X = tileX;
		startCoord.Y = tileY;
		startCoord.Z = tileZ;
		startCoord.Subresource = subResource;

		D3D12_TILE_REGION_SIZE regionSize = {};
		regionSize.NumTiles = 1;
		regionSize.UseBox = FALSE;

		D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NONE;
		UINT rangeTileCount = 1;

		

		queue->UpdateTileMappings(
			resource->D3D12Resource.Get(),
			1,
			&startCoord,
			&regionSize,
			g_tileHeap->GetD3D12Heap(),
			1,
			&rangeFlags,
			&tileOffsetInHeap,
			&rangeTileCount,
			D3D12_TILE_MAPPING_FLAG_NONE
		);

		return true;
	}
	catch (const std::exception& ex) {
		LogError(ex.what());
		return false;
	}
}

bool RenderingPlugin::UnmapTileFromHeap(
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	UINT tileOffsetInHeap,
	ReservedResource* resource) {
	try {
		if (!s_D3D12) return false;

		ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

		D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
		startCoord.X = tileX;
		startCoord.Y = tileY;
		startCoord.Z = tileZ;
		startCoord.Subresource = subResource;

		D3D12_TILE_REGION_SIZE regionSize = {};
		regionSize.NumTiles = 1;
		regionSize.UseBox = FALSE;

		D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NULL;

		queue->UpdateTileMappings(
			resource->D3D12Resource.Get(),
			1,
			&startCoord,
			&regionSize,
			nullptr,
			1,
			&rangeFlags,
			nullptr,
			nullptr,
			D3D12_TILE_MAPPING_FLAG_NONE
		);

		return true;
	}
	catch (const std::exception& ex) {
		LogError(ex.what());
		return false;
	}
}

bool RenderingPlugin::AllocateTileToHeap(UINT* outHeapOffset) {
	try {
		if (!outHeapOffset) {
			Log("AllocateTilesFromHeap: invalid parameter outHeapOffset == nullptr");
			return false;
		}
		
		TileAllocation alloc = g_tileHeap->AllocateTiles(1);

		if (alloc.success) {
			*outHeapOffset = alloc.heapOffsetInTiles;
			return true;
		}
		
		
		Log("Failed to allocate tile");

		return false;
	}
	catch (const std::exception& ex) {
		Log(ex.what());
		return false;
	}
}

bool RenderingPlugin::UploadDataToTile(
	ReservedResource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	const std::span<std::byte>& sourceData
) {
	try {
		D3D12_RESOURCE_DESC desc;
		ResourceTilingInfo tilingInfo;
		ValidateTileUploadParams(resource, subResource, sourceData, &desc, &tilingInfo);

		TileMetrics tileMetrics = CalculateTileMetrics(desc, tilingInfo, subResource);

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedFootprint = {};
		
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer = CreateAndFillUploadBuffer(
			desc, 
			subResource,
			sourceData,
			tilingInfo,
			tileMetrics,
			&placedFootprint
		);


		TileMapping mapping = AllocateAndMapTileToHeap(
			resource, 
			subResource, 
			tileX, tileY, tileZ
		);
		if (!mapping.success)
		{
			LogError("Couldn't find space for tile on heap");
			return false;
		}


		bool success = ExecuteTileCopy(
			uploadBuffer.Get(), 
			placedFootprint, 
			resource, 
			subResource, 
			tileX, tileY, tileZ, 
			tilingInfo
		);
		if (!success)
		{
			UnmapTileFromHeap(subResource, tileX, tileY, tileZ, mapping.heapOffset, resource);
		}

		return success;

	}
	catch (const std::exception& ex) {

		LogError(ex.what());
		return false;
	}
}

bool RenderingPlugin::ValidateTileUploadParams(
	const ReservedResource* resource,
	UINT subresource,
	const std::span<std::byte>& sourceData,
	D3D12_RESOURCE_DESC* outResourceDesc,
	ResourceTilingInfo* outResourceTilingInfo
) {
	if (sourceData.size_bytes() != 65536)
	{
		LogError(std::format("Tried uploading data of size {} bytes, expected 65536 bytes", sourceData.size_bytes()));
		return false;
	}
	if (!initialized)
	{
		LogError("Plugin not initialized");
		return false;
	}

	// Get tile size info
	*outResourceDesc = resource->D3D12Resource->GetDesc();
	*outResourceTilingInfo = resource->GetTilingInfo();
	UINT bytesPerPixel = GetBytesPerPixel(outResourceDesc->Format);
	if (bytesPerPixel == 0)
	{
		LogError("Unsupported texture format");
		return false;
	}
	UINT unalignedRowSize = outResourceTilingInfo->TileWidthInTexels * bytesPerPixel;

	UINT64 unalignedTotalSize = (UINT64)unalignedRowSize * outResourceTilingInfo->TileHeightInTexels * outResourceTilingInfo->TileDepthInTexels;
	if (sourceData.size_bytes() != (UINT)unalignedTotalSize)
	{
		LogError(std::format("Expected {} bytes, got {}", unalignedTotalSize, sourceData.size_bytes()));
		return false;
	}
}

TileMetrics RenderingPlugin::CalculateTileMetrics(
	const D3D12_RESOURCE_DESC& desc,
	const ResourceTilingInfo& tilingInfo,
	UINT subResource
) {
	TileMetrics out;
	out.bytesPerPixel = GetBytesPerPixel(desc.Format);

	out.unalignedRowSize = tilingInfo.TileWidthInTexels * out.bytesPerPixel;
	const UINT alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

	UINT64 unalignedTotalSize = (UINT64)out.unalignedRowSize * tilingInfo.TileHeightInTexels * tilingInfo.TileDepthInTexels;
	return out;
}

Microsoft::WRL::ComPtr<ID3D12Resource> RenderingPlugin::CreateAndFillUploadBuffer(
	const D3D12_RESOURCE_DESC& resourceDesc,
	UINT subResource,
	const std::span<std::byte>& sourceData,
	const ResourceTilingInfo& tilingInfo,
	const TileMetrics& metrics,
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT* outFootprint
) {

	UINT numRows = 0;
	UINT64 requiredSizeForCopy = 0;
	s_Device->GetCopyableFootprints(&resourceDesc, subResource, 1, 0, outFootprint, nullptr, nullptr, &requiredSizeForCopy);


	// Create upload heap
	D3D12_HEAP_PROPERTIES uploadHeapProps = {};
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC uploadBufferDesc = {};
	uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadBufferDesc.Width = requiredSizeForCopy;
	uploadBufferDesc.Height = 1;
	uploadBufferDesc.DepthOrArraySize = 1;
	uploadBufferDesc.MipLevels = 1;
	uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadBufferDesc.SampleDesc.Count = 1;
	uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr <ID3D12Resource> uploadBuffer = nullptr;
	HRESULT hr = s_Device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	);

	if (FAILED(hr) || !uploadBuffer) {
		LogError("UploadDataToTile: CreateCommittedResource failed");
		throw std::exception("UploadDataToTile: CreateCommittedResource failed");
	}

	// Copy data to D3D12 resource
	void* mapped = nullptr;
	hr = uploadBuffer->Map(0, nullptr, &mapped);
	if (FAILED(hr) || !mapped) {
		LogError(std::format("UploadDataToTile: Map failed 0x{:08x}", hr));
		throw std::exception("UploadDataToTile: Map failed ");
	}

	BYTE* dstBytes = reinterpret_cast<BYTE*>(mapped) + outFootprint->Offset;
	const BYTE* srcBytes = reinterpret_cast<const BYTE*>(sourceData.data());

	UINT alignedRowPitch = outFootprint->Footprint.RowPitch;

	// Copy each depth slice
	for (UINT z = 0; z < tilingInfo.TileDepthInTexels; ++z) {
		// Destination uses aligned pitch and footprint height
		BYTE* dstSlice = dstBytes +
			(UINT64)z * alignedRowPitch * outFootprint->Footprint.Height;

		// Source uses unaligned pitch and actual tile height
		const BYTE* srcSlice = srcBytes +
			(UINT64)z * metrics.unalignedRowSize * tilingInfo.TileHeightInTexels;

		// Copy each row in this slice
		for (UINT y = 0; y < tilingInfo.TileHeightInTexels; ++y) {
			memcpy(
				dstSlice + (UINT64)y * alignedRowPitch,
				srcSlice + (UINT64)y * metrics.unalignedRowSize,
				metrics.unalignedRowSize
			);
		}
	}

	uploadBuffer->Unmap(0, nullptr);
	return uploadBuffer;
}

TileMapping RenderingPlugin::AllocateAndMapTileToHeap(
	ReservedResource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ
) {
	// Allocate heap memory for tile
	UINT heapOffsetInTiles = 0;
	if (!AllocateTileToHeap(&heapOffsetInTiles)) {
		LogError("UploadDataToTile: AllocateTilesFromHeap failed");
		throw std::exception("UploadDataToTile: AllocateTilesFromHeap failed");
	}


	ID3D12Heap* tileHeap = g_tileHeap->GetD3D12Heap();
	if (!tileHeap) {
		LogError("UploadDataToTile: GetTileHeap returned null");
		throw std::exception("UploadDataToTile: GetTileHeap returned null");
	}

	// Map the single tile of the resource to the heap offset we allocated.

	if (!MapTileToHeap(subResource, tileX, tileY, tileZ, heapOffsetInTiles, resource)) {
		LogError("UploadDataToTile: MapTilesToHeap failed");
		throw std::exception("UploadDataToTile: MapTilesToHeap failed");
	}

	TileMapping mapping;
	mapping.success = true;
	mapping.heapOffset = heapOffsetInTiles;
	return mapping;
}

bool RenderingPlugin::ExecuteTileCopy(
	ID3D12Resource* uploadBuffer,
	const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
	ReservedResource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	const ResourceTilingInfo& tilingInfo
) {
	ID3D12CommandAllocator* allocator = GetAvailableAllocator();
	if (!allocator || !EnsureCommandListExists(allocator)) {
		return false;
	}

	HRESULT hr = s_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
	if (FAILED(hr) || !allocator) {
		UNITY_LOG(s_Log, "UploadDataToTile: CreateCommandAllocator failed 0x%08x", hr);
		return false;
	}

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList = nullptr;
	hr = s_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&cmdList));
	if (FAILED(hr) || !cmdList) {
		UNITY_LOG(s_Log, "UploadDataToTile: CreateCommandList failed 0x%08x", hr);
		return false;
	}

	D3D12_RESOURCE_BARRIER barriers[2] = {};

	// Transition to COPY_DEST
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barriers[0].Transition.pResource = resource->D3D12Resource.Get();
	barriers[0].Transition.Subresource = subResource;
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

	cmdList->ResourceBarrier(1, &barriers[0]);

	// Copy texture
	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = uploadBuffer;
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = footprint;

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = resource->D3D12Resource.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = subResource;

	D3D12_BOX srcBox = {};
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = tilingInfo.TileWidthInTexels;
	srcBox.bottom = tilingInfo.TileHeightInTexels;
	srcBox.back = tilingInfo.TileDepthInTexels;

	UINT64 dstX = tileX * tilingInfo.TileWidthInTexels;
	UINT64 dstY = tileY * tilingInfo.TileHeightInTexels;
	UINT64 dstZ = tileZ * tilingInfo.TileDepthInTexels;

	cmdList->CopyTextureRegion(&dstLoc,
		static_cast<UINT>(dstX),
		static_cast<UINT>(dstY),
		static_cast<UINT>(dstZ),
		&srcLoc,
		&srcBox);

	// Transition texture back to D3D12_RESOURCE_STATE_COMMON
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barriers[1].Transition.pResource = resource->D3D12Resource.Get();
	barriers[1].Transition.Subresource = subResource;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

	cmdList->ResourceBarrier(1, &barriers[1]);

	hr = cmdList->Close();
	if (FAILED(hr)) {
		LogError("UploadDataToTile: cmdList->Close failed");
		return false;
	}

	ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();
	ID3D12CommandList* lists[] = { cmdList.Get() };
	queue->ExecuteCommandLists(1, lists);

	const UINT64 nextFenceValue = ++m_fenceValue;
	hr = queue->Signal(m_uploadFence.Get(), nextFenceValue);
	if (FAILED(hr))
	{
		LogError("queue->Signal failed in UploadDataToTile");
		return false;
	}

	if (m_uploadFence->GetCompletedValue() < nextFenceValue) {
		hr = m_uploadFence->SetEventOnCompletion(nextFenceValue, m_fenceEvent.get());
		if (FAILED(hr))
		{
			LogError("UploadDataToTile: SetEventOnCompletion failed");
		}
		WaitForSingleObject(m_fenceEvent.get(), INFINITE);
	}


	return true;
}

ID3D12CommandAllocator* RenderingPlugin::GetAvailableAllocator() {
	UINT startIndex = m_currentAllocatorIndex;
	for (UINT i = 0; i < ALLOCATOR_POOL_SIZE; i++) {
		UINT index = (startIndex + i) % ALLOCATOR_POOL_SIZE;

		if (m_uploadFence->GetCompletedValue() >= m_allocatorFenceValues[index]) {
			m_uploadAllocators[index]->Reset();
			m_currentAllocatorIndex = (index + 1) % ALLOCATOR_POOL_SIZE;

			return m_uploadAllocators[index].Get();
		}
	}

	UINT64 oldestFenceValue = m_allocatorFenceValues[startIndex];
	if (m_uploadFence->GetCompletedValue() < oldestFenceValue) {
		m_uploadFence->SetEventOnCompletion(oldestFenceValue, m_fenceEvent.get());
		WaitForSingleObject(m_fenceEvent.get(), INFINITE);
	}

	m_uploadAllocators[startIndex]->Reset();
	m_currentAllocatorIndex = (startIndex + 1) % ALLOCATOR_POOL_SIZE;
	return m_uploadAllocators[startIndex].Get();
}

bool RenderingPlugin::EnsureCommandListExists(ID3D12CommandAllocator* allocator) {
	if (m_uploadCommandList) {
		HRESULT hr = m_uploadCommandList->Reset(allocator, nullptr);
		if (FAILED(hr)) {
			LogError(std::format("Failed to reset command list: 0x{:08x}", hr));
			return false;
		}
		return true;
	}

	// First time - create it
	HRESULT hr = s_Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		allocator,
		nullptr,
		IID_PPV_ARGS(&m_uploadCommandList)
	);

	if (FAILED(hr)) {
		LogError(std::format("Failed to create command list: 0x{:08x}", hr));
		return false;
	}

	return true;
}