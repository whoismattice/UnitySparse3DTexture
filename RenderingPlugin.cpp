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

		if (!InitializeUploadBuffers()) {
			LogError("Failed to initialize upload buffers");
			initialized = false;
			return;
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

		TileMetrics tileMetrics = CalculateTileMetrics(desc, tilingInfo, subResource);
		
		ID3D12Resource* uploadBuffer = FillUploadBuffer(
			desc, 
			subResource,
			sourceData,
			tilingInfo,
			tileMetrics
		);


		TileMapping mapping; 
		bool tileAlreadyMapped = resource->IsTileMapped(subResource, tileX, tileY, tileZ);
		if (tileAlreadyMapped) {
			UINT existingHeapOffset;
			if (!resource->GetMappedTileOffset(subResource, tileX, tileY, tileZ, &existingHeapOffset)) {
				LogError("Tile reported as mapped but couldn't get heap offset");
				return false;
			}

			mapping.success = true;
			mapping.heapOffset = existingHeapOffset;
		}
		else {
			mapping = AllocateAndMapTileToHeap(resource, subResource, tileX, tileY, tileZ);

			if (!mapping.success) {
				LogError("Couldn't find space for tile on heap");
				return false;
			}

			// Register the new tile mapping
			resource->RegisterMappedTile(subResource, tileX, tileY, tileZ, mapping.heapOffset);
		}

		if (!mapping.success)
		{
			LogError("Couldn't find space for tile on heap");
			return false;
		}

		resource->RegisterMappedTile(subResource, tileX, tileY, tileZ, mapping.heapOffset);

		bool success = ExecuteTileCopy(
			uploadBuffer, 
			resource, 
			subResource, 
			tileX, tileY, tileZ, 
			tilingInfo
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
	const D3D12_RESOURCE_DESC& resourceDesc,
	UINT subResource,
	const std::span<std::byte>& sourceData,
	const ResourceTilingInfo& tilingInfo,
	const TileMetrics& metrics
) {


	ID3D12Resource* uploadBuffer = GetCurrentUploadBuffer();
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
	memcpy(mapped, sourceData.data(), UPLOAD_TILE_SIZE);
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
	const ResourceTilingInfo& tilingInfo
) {
	ID3D12CommandAllocator* allocator = GetAvailableAllocator();
	if (!allocator || !EnsureCommandListExists(allocator)) {
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

	m_allocatorFenceValues[m_currentAllocatorIndex] = nextFenceValue;

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

		if (m_allocatorFenceValues[index] == 0) {
			m_currentAllocatorIndex = (index + 1) % ALLOCATOR_POOL_SIZE;
			return m_uploadAllocators[index].Get();
		}

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

	Log("Upload buffer ring initialized");
	return true;
}

ID3D12Resource* RenderingPlugin::GetCurrentUploadBuffer() {
	// Returns the buffer corresponding to the current allocator index
	return m_uploadBuffers[m_currentAllocatorIndex].Get();
}