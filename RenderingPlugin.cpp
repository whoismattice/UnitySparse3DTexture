#include "pch.h"
#include <stdexcept>
#include "FixedHeap.h"
#include <format>
#include <thread>
#include <chrono>
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
		// Retry device acquisition — the D3D12 environment may not be ready
		// when the plugin first loads.
		constexpr int MAX_RETRIES = 5;
		constexpr auto RETRY_DELAY = std::chrono::milliseconds(200);

		for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
			try {
				s_D3D12 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v6>();
				if (s_D3D12 != nullptr) {
					s_Device = s_D3D12->GetDevice();
					if (s_Device != nullptr) {
						break;
					}
				}
			}
			catch (const std::runtime_error&) {
				// Interface not available yet — retry
			}

			if (attempt < MAX_RETRIES) {
				std::this_thread::sleep_for(RETRY_DELAY);
			}
		}

		if (s_D3D12 == nullptr || s_Device == nullptr)
		{
			return;
		}

		// Check tiled resource support before allocating resources
		if (!GetTiledResourceSupportStatus()) {
			initialized = false;
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

		if (!InitializeUploadBuffers()) {
			LogError("Failed to initialize upload buffers");
			initialized = false;
			return;
		}

		if (!InitializeBatchUploadBuffers()) {
			LogError("Failed to initialize batch upload buffers");
			initialized = false;
			return;
		}

		Log("Found appropriate D3D12 device");
		initialized = true;

		RunDiagnostics(false);

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

bool RenderingPlugin::UnmapDataFromTile(
	ReservedResource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ
) {
	try {
		if (!resource) {
			LogError("UnmapDataFromTile: null resource");
			return false;
		}

		// Check if tile is actually mapped
		if (!resource->IsTileMapped(subResource, tileX, tileY, tileZ)) {
			LogError("UnmapDataFromTile: tile is not mapped");
			return false;
		}

		// Get the heap offset
		UINT heapOffset;
		if (!resource->GetMappedTileOffset(subResource, tileX, tileY, tileZ, &heapOffset)) {
			LogError("UnmapDataFromTile: failed to get heap offset");
			return false;
		}

		// Unmap from GPU
		if (!UnmapTileFromHeap(subResource, tileX, tileY, tileZ, heapOffset, resource)) {
			LogError("UnmapDataFromTile: failed to unmap tile from heap");
			return false;
		}

		// Free heap memory
		g_tileHeap->FreeTiles(heapOffset, 1);

		// Unregister from tracking
		resource->UnregisterMappedTile(subResource, tileX, tileY, tileZ);

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

		// Acquire a ring slot (allocator + upload buffer) up front
		UINT allocatorIndex;
		ID3D12CommandAllocator* allocator = GetAvailableAllocator(allocatorIndex);
		if (!allocator) {
			LogError("UploadDataToTile: no available allocator");
			return false;
		}

		ID3D12Resource* uploadBuffer = FillUploadBuffer(
			m_uploadBuffers[allocatorIndex].Get(),
			sourceData
		);

		if (!uploadBuffer) {
			LogError("UploadDataToTile: FillUploadBuffer returned null");
			m_allocatorFenceValues[allocatorIndex] = 0;
			return false;
		}


		TileMapping mapping;
		bool tileAlreadyMapped = resource->IsTileMapped(subResource, tileX, tileY, tileZ);
		if (tileAlreadyMapped) {
			UINT existingHeapOffset;
			if (!resource->GetMappedTileOffset(subResource, tileX, tileY, tileZ, &existingHeapOffset)) {
				LogError("Tile reported as mapped but couldn't get heap offset");
				m_allocatorFenceValues[allocatorIndex] = 0;
				return false;
			}

			mapping.success = true;
			mapping.heapOffset = existingHeapOffset;
		}
		else {
			mapping = AllocateAndMapTileToHeap(resource, subResource, tileX, tileY, tileZ);

			if (!mapping.success) {
				LogError("Couldn't find space for tile on heap");
				m_allocatorFenceValues[allocatorIndex] = 0;
				return false;
			}

		}

		if (!mapping.success)
		{
			LogError("Couldn't find space for tile on heap");
			m_allocatorFenceValues[allocatorIndex] = 0;
			return false;
		}

		bool success = ExecuteTileCopy(
			uploadBuffer,
			resource,
			subResource,
			tileX, tileY, tileZ,
			tilingInfo,
			allocatorIndex
		);
		if (!success)
		{
			if (!tileAlreadyMapped) {
				resource->UnregisterMappedTile(subResource, tileX, tileY, tileZ);
				UnmapTileFromHeap(subResource, tileX, tileY, tileZ, mapping.heapOffset, resource);
				g_tileHeap->FreeTiles(mapping.heapOffset, 1);
			}
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
	return true;
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

ID3D12Resource* RenderingPlugin::FillUploadBuffer(
	ID3D12Resource* uploadBuffer,
	const std::span<std::byte>& sourceData
) {
	if (!uploadBuffer) {
		LogError("Upload buffer is null");
		return nullptr;
	}

	// Copy data to D3D12 resource
	void* mapped = nullptr;
	HRESULT hr = uploadBuffer->Map(0, nullptr, &mapped);
	if (FAILED(hr) || !mapped) {
		LogError(std::format("UploadDataToTile: Map failed 0x{:08x}", hr));
		throw std::exception("UploadDataToTile: Map failed ");
	}
	memcpy(mapped, sourceData.data(), sourceData.size_bytes());
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

	resource->RegisterMappedTile(subResource, tileX, tileY, tileZ, mapping.heapOffset);

	return mapping;
}

bool RenderingPlugin::ExecuteTileCopy(
	ID3D12Resource* uploadBuffer,
	ReservedResource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	const ResourceTilingInfo& tilingInfo,
	UINT allocatorIndex
) {
	ID3D12CommandAllocator* allocator = m_uploadAllocators[allocatorIndex].Get();
	if (!EnsureCommandListExists(allocator)) {
		return false;
	}

	// Set up tile region to copy
	D3D12_TILED_RESOURCE_COORDINATE tileCoord = {};
	tileCoord.X = tileX;
	tileCoord.Y = tileY;
	tileCoord.Z = tileZ;
	tileCoord.Subresource = subResource;


	D3D12_TILE_REGION_SIZE regionSize = {};
	regionSize.NumTiles = 1;
	regionSize.UseBox = TRUE;
	regionSize.Width = 1;   // 1 tile wide
	regionSize.Height = 1;  // 1 tile tall
	regionSize.Depth = 1;

	m_uploadCommandList->CopyTiles(
		resource->D3D12Resource.Get(),
		&tileCoord,
		&regionSize,
		uploadBuffer,
		0,  // offset in upload buffer
		D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
	);

	HRESULT hr = m_uploadCommandList->Close();
	if (FAILED(hr)) {
		LogError("ExecuteTileCopy: cmdList->Close failed");
		return false;
	}


	ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();
	ID3D12CommandList* lists[] = { m_uploadCommandList.Get() };
	queue->ExecuteCommandLists(1, lists);

	const UINT64 nextFenceValue = ++m_fenceValue;
	hr = queue->Signal(m_uploadFence.Get(), nextFenceValue);
	if (FAILED(hr))
	{
		LogError("queue->Signal failed in UploadDataToTile");
		return false;
	}

	m_allocatorFenceValues[allocatorIndex] = nextFenceValue;

	return true;
}

ID3D12CommandAllocator* RenderingPlugin::GetAvailableAllocator(UINT& outAllocatorIndex) {
	// Start searching from the slot after the last-used one
	UINT startIndex = (m_currentAllocatorIndex + 1) % ALLOCATOR_POOL_SIZE;
	for (UINT i = 0; i < ALLOCATOR_POOL_SIZE; i++) {
		UINT index = (startIndex + i) % ALLOCATOR_POOL_SIZE;

		if (m_allocatorFenceValues[index] == 0) {
			m_currentAllocatorIndex = index;
			outAllocatorIndex = index;
			return m_uploadAllocators[index].Get();
		}

		if (m_uploadFence->GetCompletedValue() >= m_allocatorFenceValues[index]) {
			m_uploadAllocators[index]->Reset();
			m_currentAllocatorIndex = index;
			outAllocatorIndex = index;
			return m_uploadAllocators[index].Get();
		}
	}

	// All slots in flight — wait for the oldest
	UINT oldestIndex = startIndex;
	UINT64 oldestFenceValue = m_allocatorFenceValues[oldestIndex];
	if (m_uploadFence->GetCompletedValue() < oldestFenceValue) {
		m_uploadFence->SetEventOnCompletion(oldestFenceValue, m_fenceEvent.get());
		WaitForSingleObject(m_fenceEvent.get(), INFINITE);
	}

	m_uploadAllocators[oldestIndex]->Reset();
	m_currentAllocatorIndex = oldestIndex;
	outAllocatorIndex = oldestIndex;
	return m_uploadAllocators[oldestIndex].Get();
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

bool RenderingPlugin::InitializeUploadBuffers() {
	D3D12_HEAP_PROPERTIES uploadHeapProps = {};
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = UPLOAD_TILE_SIZE;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	for (UINT i = 0; i < ALLOCATOR_POOL_SIZE; ++i) {
		HRESULT hr = s_Device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_uploadBuffers[i])
		);

		if (FAILED(hr)) {
			LogError(std::format("Failed to create upload buffer {}: 0x{:08x}", i, hr));
			return false;
		}
	}

	return true;
}

bool RenderingPlugin::InitializeBatchUploadBuffers() {
	D3D12_HEAP_PROPERTIES uploadHeapProps = {};
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = BATCH_UPLOAD_BYTE_SIZE;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	for (UINT i = 0; i < ALLOCATOR_POOL_SIZE; ++i) {
		HRESULT hr = s_Device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_batchUploadBuffers[i])
		);

		if (FAILED(hr)) {
			LogError(std::format("Failed to create batch upload buffer {}: 0x{:08x}", i, hr));
			return false;
		}
	}

	return true;
}

bool RenderingPlugin::ValidateTileBoxParams(
	const ReservedResource* resource,
	const TileBox& box,
	const std::span<std::byte>& sourceData,
	D3D12_RESOURCE_DESC* outResourceDesc,
	ResourceTilingInfo* outResourceTilingInfo
) {
	if (!initialized)
	{
		LogError("UploadDataToTileBox: plugin not initialized");
		return false;
	}

	if (!resource)
	{
		LogError("UploadDataToTileBox: null resource");
		return false;
	}

	if (box.width == 0 || box.height == 0 || box.depth == 0)
	{
		LogError("UploadDataToTileBox: zero-dimension box");
		return false;
	}

	UINT tileCount = box.TileCount();
	UINT64 expectedSize = static_cast<UINT64>(tileCount) * UPLOAD_TILE_SIZE;
	if (sourceData.size_bytes() != expectedSize)
	{
		LogError(std::format(
			"UploadDataToTileBox: expected {} bytes for {} tiles, got {}",
			expectedSize, tileCount, sourceData.size_bytes()));
		return false;
	}

	// Validate box is within subresource tile grid
	*outResourceDesc = resource->D3D12Resource->GetDesc();
	*outResourceTilingInfo = resource->GetTilingInfo();

	if (box.subResource >= outResourceTilingInfo->SubresourceCount)
	{
		LogError(std::format(
			"UploadDataToTileBox: subResource {} out of range (max {})",
			box.subResource, outResourceTilingInfo->SubresourceCount - 1));
		return false;
	}

	const SubresourceTilingInfo& subInfo =
		outResourceTilingInfo->subresourceTilingInfo[box.subResource];

	if (box.startX + box.width > subInfo.WidthInTiles ||
		box.startY + box.height > subInfo.HeightInTiles ||
		box.startZ + box.depth > subInfo.DepthInTiles)
	{
		LogError(std::format(
			"UploadDataToTileBox: box exceeds subresource bounds "
			"(box end: {},{},{}; subresource dims: {},{},{})",
			box.startX + box.width,
			box.startY + box.height,
			box.startZ + box.depth,
			subInfo.WidthInTiles,
			subInfo.HeightInTiles,
			subInfo.DepthInTiles));
		return false;
	}

	UINT bytesPerPixel = GetBytesPerPixel(outResourceDesc->Format);
	if (bytesPerPixel == 0)
	{
		LogError("UploadDataToTileBox: unsupported texture format");
		return false;
	}

	return true;
}

void RenderingPlugin::RollbackTileBoxMapping(
	ReservedResource* resource,
	const TileBox& box,
	UINT heapOffsetInTiles,
	UINT tileCount
) {
	ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

	D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
	startCoord.X = box.startX;
	startCoord.Y = box.startY;
	startCoord.Z = box.startZ;
	startCoord.Subresource = box.subResource;

	D3D12_TILE_REGION_SIZE regionSize = {};
	regionSize.NumTiles = tileCount;
	regionSize.UseBox = TRUE;
	regionSize.Width = box.width;
	regionSize.Height = box.height;
	regionSize.Depth = box.depth;

	D3D12_TILE_RANGE_FLAGS nullFlags = D3D12_TILE_RANGE_FLAG_NULL;

	queue->UpdateTileMappings(
		resource->D3D12Resource.Get(),
		1, &startCoord, &regionSize,
		nullptr,
		1, &nullFlags,
		nullptr, nullptr,
		D3D12_TILE_MAPPING_FLAG_NONE
	);

	// Unregister all tiles in the box
	for (UINT z = box.startZ; z < box.startZ + box.depth; ++z)
		for (UINT y = box.startY; y < box.startY + box.height; ++y)
			for (UINT x = box.startX; x < box.startX + box.width; ++x)
				resource->UnregisterMappedTile(box.subResource, x, y, z);

	// Return heap space
	g_tileHeap->FreeTiles(heapOffsetInTiles, tileCount);
}

bool RenderingPlugin::UploadDataToTileBox(
	ReservedResource* resource,
	const TileBox& box,
	const std::span<std::byte>& sourceData
) {
	try {
		// Validation
		D3D12_RESOURCE_DESC desc;
		ResourceTilingInfo tilingInfo;
		if (!ValidateTileBoxParams(resource, box, sourceData, &desc, &tilingInfo))
			return false;

		UINT tileCount = box.TileCount();

		// Pre-check for pre-existing mappings
		for (UINT z = box.startZ; z < box.startZ + box.depth; ++z)
			for (UINT y = box.startY; y < box.startY + box.height; ++y)
				for (UINT x = box.startX; x < box.startX + box.width; ++x)
					if (resource->IsTileMapped(box.subResource, x, y, z))
					{
						LogError(std::format(
							"UploadDataToTileBox: tile ({},{},{}) already mapped",
							x, y, z));
						return false;
					}

		// Pre-check capacity
		if (!g_tileHeap->CanAllocate(tileCount))
		{
			LogError(std::format(
				"UploadDataToTileBox: heap cannot allocate {} tiles "
				"(free: {}, used: {})",
				tileCount, g_tileHeap->GetFreeTiles(), g_tileHeap->GetUsedTiles()));
			return false;
		}

		// Allocate contiguous heap space for the entire box
		TileAllocation alloc = g_tileHeap->AllocateTiles(tileCount);
		if (!alloc.success)
		{
			LogError("UploadDataToTileBox: AllocateTiles failed");
			return false;
		}

		// Map the box region with one UpdateTileMappings call
		{
			ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

			D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
			startCoord.X = box.startX;
			startCoord.Y = box.startY;
			startCoord.Z = box.startZ;
			startCoord.Subresource = box.subResource;

			D3D12_TILE_REGION_SIZE regionSize = {};
			regionSize.NumTiles = tileCount;
			regionSize.UseBox = TRUE;
			regionSize.Width = box.width;
			regionSize.Height = box.height;
			regionSize.Depth = box.depth;

			D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NONE;

			queue->UpdateTileMappings(
				resource->D3D12Resource.Get(),
				1, &startCoord, &regionSize,
				g_tileHeap->GetD3D12Heap(),
				1, &rangeFlags,
				&alloc.heapOffsetInTiles,
				&tileCount,
				D3D12_TILE_MAPPING_FLAG_NONE
			);
		}

		// Register all tiles with sequential heap offsets
		{
			UINT i = 0;
			for (UINT z = box.startZ; z < box.startZ + box.depth; ++z)
				for (UINT y = box.startY; y < box.startY + box.height; ++y)
					for (UINT x = box.startX; x < box.startX + box.width; ++x)
						resource->RegisterMappedTile(
							box.subResource, x, y, z,
							alloc.heapOffsetInTiles + i++);
		}

		// Acquire ring slot for the copy
		UINT allocatorIndex;
		ID3D12CommandAllocator* cmdAllocator =
			GetAvailableAllocator(allocatorIndex);
		if (!cmdAllocator || !EnsureCommandListExists(cmdAllocator))
		{
			RollbackTileBoxMapping(
				resource, box, alloc.heapOffsetInTiles, tileCount);
			return false;
		}

		// Fill batch upload buffer
		ID3D12Resource* batchBuffer =
			m_batchUploadBuffers[allocatorIndex].Get();
		if (!batchBuffer)
		{
			RollbackTileBoxMapping(
				resource, box, alloc.heapOffsetInTiles, tileCount);
			return false;
		}

		{
			void* mapped = nullptr;
			HRESULT hr = batchBuffer->Map(0, nullptr, &mapped);
			if (FAILED(hr) || !mapped)
			{
				LogError(std::format(
					"UploadDataToTileBox: Map failed 0x{:08x}", hr));
				RollbackTileBoxMapping(
					resource, box, alloc.heapOffsetInTiles, tileCount);
				return false;
			}
			memcpy(mapped, sourceData.data(), sourceData.size_bytes());
			batchBuffer->Unmap(0, nullptr);
		}

		// Copy entire box with one CopyTiles call
		{
			D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
			startCoord.X = box.startX;
			startCoord.Y = box.startY;
			startCoord.Z = box.startZ;
			startCoord.Subresource = box.subResource;

			D3D12_TILE_REGION_SIZE regionSize = {};
			regionSize.NumTiles = tileCount;
			regionSize.UseBox = TRUE;
			regionSize.Width = box.width;
			regionSize.Height = box.height;
			regionSize.Depth = box.depth;

			m_uploadCommandList->CopyTiles(
				resource->D3D12Resource.Get(),
				&startCoord,
				&regionSize,
				batchBuffer,
				0,
				D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
			);
		}

		HRESULT hr = m_uploadCommandList->Close();
		if (FAILED(hr))
		{
			LogError("UploadDataToTileBox: cmdList->Close failed");
			RollbackTileBoxMapping(
				resource, box, alloc.heapOffsetInTiles, tileCount);
			return false;
		}

		{
			ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();
			ID3D12CommandList* lists[] = { m_uploadCommandList.Get() };
			queue->ExecuteCommandLists(1, lists);

			const UINT64 nextFenceValue = ++m_fenceValue;
			hr = queue->Signal(m_uploadFence.Get(), nextFenceValue);
			if (FAILED(hr))
			{
				LogError("UploadDataToTileBox: queue->Signal failed");
				RollbackTileBoxMapping(
					resource, box, alloc.heapOffsetInTiles, tileCount);
				return false;
			}

			m_allocatorFenceValues[allocatorIndex] = nextFenceValue;
		}

		return true;
	}
	catch (const std::exception& ex)
	{
		LogError(ex.what());
		return false;
	}
}

std::vector<DiagnosticResult> RenderingPlugin::RunDiagnostics(bool includeSmokeTest)
{
	Log("Running diagnostics...");

	ID3D12Resource* rawBuffers[ALLOCATOR_POOL_SIZE];
	for (UINT i = 0; i < ALLOCATOR_POOL_SIZE; ++i)
	{
		rawBuffers[i] = m_uploadBuffers[i].Get();
	}

	auto results = Diagnostics::RunStartupChecks(
		s_Device,
		g_tileHeap.get(),
		rawBuffers,
		ALLOCATOR_POOL_SIZE,
		s_D3D12,
		s_Log);

	LogDiagnosticResults(results);

	if (includeSmokeTest && Diagnostics::AllPassed(results))
	{
		Log("Tier-1 checks passed — running smoke test");

		Diagnostics::SmokeTestOps ops;
		ops.createResource = [this](
			UINT w, UINT h, UINT d,
			bool mips, UINT mipCount,
			DXGI_FORMAT fmt) -> ReservedResource*
		{
			return this->CreateVolumetricResource(w, h, d, mips, mipCount, fmt);
		};
		ops.destroyResource = [this](ReservedResource* r) -> bool
		{
			return this->DestroyVolumetricResource(r);
		};
		ops.uploadData = [this](
			ReservedResource* r,
			UINT subRes,
			UINT tx, UINT ty, UINT tz,
			void* data, UINT size) -> bool
		{
			std::span<std::byte> dataSpan(
				static_cast<std::byte*>(data), size);
			return this->UploadDataToTile(r, subRes, tx, ty, tz, dataSpan);
		};
		ops.unmapTile = [this](
			ReservedResource* r,
			UINT subRes,
			UINT tx, UINT ty, UINT tz) -> bool
		{
			return this->UnmapDataFromTile(r, subRes, tx, ty, tz);
		};
		ops.uploadTileBox = [this](
			ReservedResource* r,
			UINT subRes,
			UINT sx, UINT sy, UINT sz,
			UINT w, UINT h, UINT d,
			void* data, UINT size) -> bool
		{
			TileBox box;
			box.subResource = subRes;
			box.startX = sx; box.startY = sy; box.startZ = sz;
			box.width = w; box.height = h; box.depth = d;
			std::span<std::byte> dataSpan(
				static_cast<std::byte*>(data), size);
			return this->UploadDataToTileBox(r, box, dataSpan);
		};

		auto smokeResults = Diagnostics::RunSmokeTest(
			ops, g_tileHeap.get(), s_Log);
		LogDiagnosticResults(smokeResults);

		results.insert(results.end(),
			std::make_move_iterator(smokeResults.begin()),
			std::make_move_iterator(smokeResults.end()));
	}
	else if (includeSmokeTest)
	{
		LogError("Skipping smoke test — tier-1 checks did not all pass");
	}

	return results;
}

void RenderingPlugin::LogDiagnosticResults(
	const std::vector<DiagnosticResult>& results)
{
	UINT passed = 0;
	UINT failed = 0;

	for (const auto& r : results)
	{
		if (r.passed)
		{
			passed++;
			Log(std::format("[PASS] {} — {}", r.name, r.message));
		}
		else
		{
			failed++;
			LogError(std::format("[FAIL] {} — {}", r.name, r.message));
		}
	}

	UINT total = passed + failed;
	if (failed == 0)
	{
		Log(std::format("Diagnostics: {}/{} passed", passed, total));
	}
	else
	{
		LogError(std::format(
			"Diagnostics: {}/{} passed, {} FAILED",
			passed, total, failed));
	}
}

bool RenderingPlugin::GetTiledResourceSupportStatus() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 supportLevel;
	HRESULT hr = s_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &supportLevel, sizeof(supportLevel));
	if (SUCCEEDED(hr))
	{
		if (supportLevel.SRVOnlyTiledResourceTier3)
		{
			Log("D3D12 Feature Tiled Resources Tier 3 Supported");
			return true;
		}
		LogError("D3D12 Feature Tiled Resources Tier 3 Not Supported");
		return false;
	}
	LogError("D3D12 Feature Support Query Failed");
	return false;
}
