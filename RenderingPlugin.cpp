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
		s_D3D12 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v6>();
		s_Device = s_D3D12->GetDevice();
		if (s_D3D12 == nullptr || s_Device == nullptr)
		{
			return;
		}
		g_tileHeap = std::make_unique<FixedHeap>(s_Device, 512 * 1024 * 1024);
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
	catch (std::exception ex) {
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
	catch (std::exception ex) {
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
	catch (std::exception ex) {
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
	catch (std::exception ex) {
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
		// Check pre-conditions
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

		// 
		D3D12_RESOURCE_DESC desc = resource->D3D12Resource->GetDesc();
		ResourceTilingInfo tilingInfo = resource->GetTilingInfo();
		UINT bytesPerPixel = GetBytesPerPixel(desc.Format);
		if (bytesPerPixel == 0)
		{
			LogError("Unsupported texture format");
			return false;
		}
		UINT unalignedRowSize = tilingInfo.TileWidthInTexels * bytesPerPixel;
		const UINT alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

		UINT64 unalignedTotalSize = (UINT64)unalignedRowSize * tilingInfo.TileHeightInTexels * tilingInfo.TileDepthInTexels;
		if (sourceData.size_bytes() != (UINT) unalignedTotalSize)
		{
			LogError(std::format("Expected {} bytes, got {}", unalignedTotalSize, sourceData.size_bytes()));
			return false;
		}

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedFootprint = {};
		UINT numRows = 0;
		UINT64 totalBytes = 0;
		UINT64 requiredSizeForCopy = 0;
		s_Device->GetCopyableFootprints(&desc, subResource, 1, 0, &placedFootprint, &numRows, nullptr, &requiredSizeForCopy);



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

		ID3D12Resource* uploadBuffer = nullptr;
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
			return false;
		}

		void* mapped = nullptr;
		hr = uploadBuffer->Map(0, nullptr, &mapped);
		if (FAILED(hr) || !mapped) {
			UNITY_LOG(s_Log, "UploadDataToTile: Map failed 0x%08x", hr);
			uploadBuffer->Release();
			return false;
		}

		BYTE* dstBytes = reinterpret_cast<BYTE*>(mapped) + placedFootprint.Offset; // usually 0
		BYTE* srcBytes = reinterpret_cast<BYTE*>(sourceData.data());

		UINT alignedRowPitch = placedFootprint.Footprint.RowPitch;
		for (UINT z = 0; z < placedFootprint.Footprint.Depth; ++z) {
			BYTE* dstSlice = dstBytes + (UINT64)z * alignedRowPitch * placedFootprint.Footprint.Height;
			BYTE* srcSlice = srcBytes + (UINT64)z * unalignedRowSize * placedFootprint.Footprint.Height;
			for (UINT y = 0; y < placedFootprint.Footprint.Height; ++y) {
				memcpy(dstSlice + (UINT64)y * alignedRowPitch,
					srcSlice + (UINT64)y * unalignedRowSize,
					unalignedRowSize);
			}
		}

		uploadBuffer->Unmap(0, nullptr);

		UINT heapOffsetInTiles = 0;
		if (!AllocateTileToHeap(&heapOffsetInTiles)) {
			LogError("UploadDataToTile: AllocateTilesFromHeap failed");
			uploadBuffer->Release();
			return false;
		}

		ID3D12Heap* tileHeap = g_tileHeap->GetD3D12Heap();
		if (!tileHeap) {
			LogError("UploadDataToTile: GetTileHeap returned null");
			uploadBuffer->Release();
			return false;
		}

		// Map the single tile of the resource to the heap offset we allocated.
		
		if (!MapTileToHeap(subResource, tileX, tileY, tileZ, heapOffsetInTiles, resource)) {
			LogError("UploadDataToTile: MapTilesToHeap failed");
			uploadBuffer->Release();
			return false;
		}



		ID3D12CommandAllocator* allocator = nullptr;
		hr = s_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
		if (FAILED(hr) || !allocator) {
			UNITY_LOG(s_Log, "UploadDataToTile: CreateCommandAllocator failed 0x%08x", hr);
			uploadBuffer->Release();
			return false;
		}

		ID3D12GraphicsCommandList* cmdList = nullptr;
		hr = s_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&cmdList));
		if (FAILED(hr) || !cmdList) {
			UNITY_LOG(s_Log, "UploadDataToTile: CreateCommandList failed 0x%08x", hr);
			allocator->Release();
			uploadBuffer->Release();
			return false;
		}

		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = uploadBuffer;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.PlacedFootprint = placedFootprint;

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

		hr = cmdList->Close();
		if (FAILED(hr)) {
			LogError("UploadDataToTile: cmdList->Close failed");
			cmdList->Release();
			allocator->Release();
			uploadBuffer->Release();
			return false;
		}

		ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();
		ID3D12CommandList* lists[] = { cmdList };
		queue->ExecuteCommandLists(1, lists);

		ID3D12Fence* fence = nullptr;
		hr = s_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		if (FAILED(hr) || !fence) {
			LogError("UploadDataToTile: CreateFence failed");
			cmdList->Release();
			allocator->Release();
			uploadBuffer->Release();
			return false;
		}

		UINT64 fenceValue = 1;
		queue->Signal(fence, fenceValue);
		if (fence->GetCompletedValue() < fenceValue) {
			HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			fence->SetEventOnCompletion(fenceValue, event);
			WaitForSingleObject(event, INFINITE);
			CloseHandle(event);
		}

		// Clean up
		fence->Release();
		cmdList->Release();
		allocator->Release();
		uploadBuffer->Release();

		return true;

	}
	catch (std::exception ex) {

		LogError(ex.what());
		return false;
	}
}
