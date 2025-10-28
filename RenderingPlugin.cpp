#include "pch.h"

#include "RenderingPlugin.h"

#include <comdef.h>
#include "IUnityLog.h"
#include "IUnityGraphicsD3D12.h"
#include "IUnityInterface.h"
#include "FixedHeap.h"
#include <string>
#include <memory>


// Unity interfaces
static IUnityInterfaces* s_UnityInterfaces = nullptr;
static IUnityGraphics* s_Graphics = nullptr;
static IUnityGraphicsD3D12v6* s_D3D12 = nullptr;
static IUnityLog* s_Log = nullptr;

// D3D12 device provided by Unity
static ID3D12Device* s_Device = nullptr;

// Forward declaration of internal static functions
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);
static void UNITY_INTERFACE_API OnRenderEvent(int eventId, void* data);

static std::unique_ptr<IHeap> g_tileHeap = nullptr;

static ID3D12DescriptorHeap* g_SrvDescriptorHeap = nullptr;
static UINT g_DescriptorSize = 0;

// This function is called when the plugin is loaded
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Log = s_UnityInterfaces->Get<IUnityLog>();
	
	

	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

// This function is called when the plugin is unloaded
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
	s_Log = nullptr;
	s_UnityInterfaces = nullptr;
	s_Graphics = nullptr;
}

// Implementation of GFX device callback
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	switch (eventType)
	{
		// Gets D3D12 device when initialised
		case kUnityGfxDeviceEventInitialize:
		{
			UNITY_LOG(s_Log, "Native Plugin: Acquire GFX Device");
			s_D3D12 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v6>();
			s_Device = s_D3D12->GetDevice();
			if (s_D3D12 == nullptr)
			{
				UNITY_LOG_ERROR(s_Log, "Couldn't find appropriate D3D12 device");
				break;
			}
			else {
				UNITY_LOG(s_Log, "Found appropriate D3D12 device");
				InitializeDescriptorHeap();
			}
			break;
		}

		// Sets D3D12 device to nullptr to ensure it is not used after shutdown
		case kUnityGfxDeviceEventShutdown:
		{
			s_Device = nullptr;
			s_D3D12 = nullptr;
			break;
		}
	}
	if (s_Log)
	{
		//s_Log = nullptr;
	}
}

UnityRenderingEventAndData UNITY_INTERFACE_API GetRenderEventAndDataCallback()
{
	return OnRenderEvent;
}

ID3D12Resource* UNITY_INTERFACE_API CreateVolumetricResource(int width, int height, int depth, int mipmapCount, DXGI_FORMAT format)
{
	// If graphics device is not created, we can't create a resource
	if (!s_Device)
	{
		UNITY_LOG_ERROR(s_Log, "CreateVolumetricResource called but not D3D12 device is null");
		return nullptr;
	}
	
	D3D12_RESOURCE_DESC desc = {};

	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	desc.Alignment = 0;
	desc.Width = static_cast<UINT64>(width);
	desc.Height = static_cast<UINT>(height);
	desc.DepthOrArraySize = static_cast<UINT16>(depth);
	desc.MipLevels = mipmapCount;
	desc.Format = static_cast<DXGI_FORMAT>(format),
	desc.SampleDesc.Count = 1,
	desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ID3D12Resource* reservedResource = nullptr;
	UNITY_LOG(s_Log, "successfully created resource flags");
	

	HRESULT hr = s_Device->CreateReservedResource(
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&reservedResource)
	);


	if (SUCCEEDED(hr))
	{
		UNITY_LOG(s_Log, "Successfully created a volumetric resource");
		return reservedResource;
	}
	if (FAILED(hr))
	{
		UNITY_LOG_ERROR(s_Log, "Could not create a volumetric resource");
	}
	UNITY_LOG_ERROR(s_Log, "Could not create a volumetric resource");
	return nullptr;
}

void UNITY_INTERFACE_API DestroyVolumetricResource(ID3D12Resource* resource)
{
	if (resource == nullptr)
	{
		return;
	}

	ID3D12Resource* d3d12resource = static_cast<ID3D12Resource*>(resource);

	d3d12resource->Release();

	UNITY_LOG(s_Log, "Released volumetric resource");

	// TODO :: implement resource destruction
	return;
}

UNITY_INTERFACE_EXPORT UINT64 CreateSRVForResource(
	ID3D12Resource* resource,
	UINT descriptorIndex
) {
	if (!resource || !g_SrvDescriptorHeap) return 0;

	D3D12_RESOURCE_DESC desc = resource->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture3D.MipLevels = desc.MipLevels;
	srvDesc.Texture3D.MostDetailedMip = 0;


	D3D12_CPU_DESCRIPTOR_HANDLE gpuHandle = g_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	gpuHandle.ptr += descriptorIndex * g_DescriptorSize;
	return gpuHandle.ptr;

}

UNITY_INTERFACE_EXPORT void GetResourceTilingInfo(ID3D12Resource* resource, ResourceTilingInfo* outInfo) {
	if (!resource || !outInfo)
	{
		return;
	}

	D3D12_RESOURCE_DESC resourceDescription = resource->GetDesc();
	UINT numTilesForEntireResource = 0;
	D3D12_PACKED_MIP_INFO packedMipInfo;
	D3D12_TILE_SHAPE resourceTileShape;
	UINT numSubresources = resourceDescription.MipLevels;

	char resultBuffer[256];
	auto subresourceTilings = new D3D12_SUBRESOURCE_TILING[numSubresources];

	s_Device->GetResourceTiling(
		resource,
		&numTilesForEntireResource,
		&packedMipInfo,
		&resourceTileShape,
		&numSubresources,
		0,
		subresourceTilings
	);

	sprintf_s(resultBuffer, "[C++] GetResourceTilingInfo: Call complete. Value of numSubresources is now: %u", numSubresources);
	UNITY_LOG(s_Log, resultBuffer);

	outInfo->TileWidthInTexels = resourceTileShape.WidthInTexels;
	outInfo->TileHeightInTexels = resourceTileShape.HeightInTexels;
	outInfo->TileDepthInTexels = resourceTileShape.DepthInTexels;
	outInfo->SubresourceCount = numSubresources;
	outInfo->NumPackedMips = packedMipInfo.NumPackedMips;

	delete[] subresourceTilings;
}

UNITY_INTERFACE_EXPORT void GetAllSubresourceTilings(
	ID3D12Resource* resource,
	SubresourceTilingInfo* outSubresourceTilingArray,
	int arraySize
)	{
		// Checks for null pointers
		if (!s_Device || !resource || !outSubresourceTilingArray)
		{
			return;
		}

		auto subresourceTilings = new D3D12_SUBRESOURCE_TILING[arraySize];

		UINT numSubresources = arraySize;

		s_Device->GetResourceTiling(resource, nullptr, nullptr, nullptr, &numSubresources, 0, subresourceTilings);

		for (size_t i = 0; i < arraySize; i++)
		{
			outSubresourceTilingArray[i].WidthInTiles = subresourceTilings[i].WidthInTiles;
			outSubresourceTilingArray[i].HeightInTiles = subresourceTilings[i].HeightInTiles;
			outSubresourceTilingArray[i].DepthInTiles = subresourceTilings[i].DepthInTiles;
			outSubresourceTilingArray[i].StartTileIndex = subresourceTilings[i].StartTileIndexInOverallResource;
		}

		delete[] subresourceTilings;
	}



static void UNITY_INTERFACE_API OnRenderEvent(int eventId, void* data)
{
	// Return if GFX device not initialized
	if (s_D3D12 == nullptr) {
		return;
	}

	UnityGraphicsD3D12RecordingState recordingState;
	if (!s_D3D12->CommandRecordingState(&recordingState) || recordingState.commandList == nullptr)
	{
		return;
	}

	ID3D12GraphicsCommandList* commandList = recordingState.commandList;

	UNITY_LOG(s_Log, "Native Plugin: OnRenderEvent Callback Triggered");
}


UNITY_INTERFACE_EXPORT bool MapTilesToHeap(
	ID3D12Resource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	UINT numTiles,
	UINT heapOffsetInTiles,
	ID3D12Heap* heap
) {
	if (!s_D3D12 || !resource || !heap) return false;

	ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

	D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
	startCoord.X = tileX;
	startCoord.Y = tileY;
	startCoord.Z = tileZ;
	startCoord.Subresource = subResource;

	D3D12_TILE_REGION_SIZE regionSize = {};
	regionSize.NumTiles = numTiles;
	regionSize.UseBox = FALSE;

	D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NONE;
	UINT rangeTileCount = numTiles;

	queue->UpdateTileMappings(
		resource,
		1,
		&startCoord,
		&regionSize,
		heap,
		1,
		&rangeFlags,
		&heapOffsetInTiles,
		&rangeTileCount,
		D3D12_TILE_MAPPING_FLAG_NONE
	);

	return true;
}

UNITY_INTERFACE_EXPORT bool UnmapTiles(
	ID3D12Resource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	UINT numTiles
) {
	if (!s_D3D12 || !resource) return false;

	ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

	D3D12_TILED_RESOURCE_COORDINATE startCoord = {};
	startCoord.X = tileX;
	startCoord.Y = tileY;
	startCoord.Z = tileZ;
	startCoord.Subresource = subResource;

	D3D12_TILE_REGION_SIZE regionSize = {};
	regionSize.NumTiles = numTiles;
	regionSize.UseBox = FALSE;

	D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NULL;

	queue->UpdateTileMappings(
		resource,
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

UNITY_INTERFACE_EXPORT ID3D12Heap* GetTileHeap() {
	return g_tileHeap ? g_tileHeap->GetD3D12Heap() : nullptr;
}

UNITY_INTERFACE_EXPORT bool AllocateTilesFromHeap(
	UINT numTiles, UINT* outHeapOffset
) {
	if (!g_tileHeap || !outHeapOffset) return false;

	TileAllocation alloc = g_tileHeap->AllocateTiles(numTiles);
	if (alloc.success) {
		*outHeapOffset = alloc.heapOffsetInTiles;
	}

	return alloc.success;
}


void InitializeDescriptorHeap() {
	if (!s_Device) {
		UNITY_LOG_ERROR(s_Log, "Trying to create descriptor heap but graphics device has not been initialised");
		return;
	}
	

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 256;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	s_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_SrvDescriptorHeap));
	g_DescriptorSize = s_Device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
	);

	UNITY_LOG(s_Log, "Created SRV Descriptor Heap");
}


UNITY_INTERFACE_EXPORT bool UploadDataToTile(
	ID3D12Resource* tiledResource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	void* sourceData,
	UINT dataSize) {
	if (!s_D3D12 || !tiledResource || !sourceData) return false;

	D3D12_RESOURCE_DESC desc = tiledResource->GetDesc();
	ResourceTilingInfo tilingInfo;
	GetResourceTilingInfo(tiledResource, &tilingInfo);

	UINT bytesPerPixel = GetBytesPerPixel(desc.Format);
	if (bytesPerPixel == 0)
	{
		UNITY_LOG(s_Log, "Unsupported texture format");
		return false;
	}
	UINT unalignedRowSize = tilingInfo.TileWidthInTexels * bytesPerPixel;
	const UINT alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	UINT alignedRowPitch = (unalignedRowSize + (alignment - 1)) & ~(alignment - 1);

	UINT64 totalAlignedDataSize = (UINT64)alignedRowPitch * tilingInfo.TileDepthInTexels * tilingInfo.TileHeightInTexels;

	UINT64 unalignedTotalSize = (UINT64)unalignedRowSize
		* tilingInfo.TileHeightInTexels
		* tilingInfo.TileDepthInTexels;
	if (dataSize != unalignedTotalSize) {
		return false;
	}

	D3D12_HEAP_PROPERTIES uploadHeapProps = {};
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC uploadBufferDesc = {};
	uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadBufferDesc.Width = totalAlignedDataSize;
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

	if (FAILED(hr)) return false;

	void* mappedData = nullptr;
	uploadBuffer->Map(0, nullptr, &mappedData);

	BYTE* pDst = (BYTE*)mappedData;
	BYTE* pSrc = (BYTE*)sourceData;

	for (UINT z = 0; z < tilingInfo.TileDepthInTexels; ++z)
	{
		// Get pointer to the start of the current 3D slice in destination
		BYTE* pDstSlice = pDst + (z * alignedRowPitch * tilingInfo.TileHeightInTexels);

		// Get pointer to the start of the current 3D slice in source
		BYTE* pSrcSlice = pSrc + (z * unalignedRowSize * tilingInfo.TileHeightInTexels);

		for (UINT y = 0; y < tilingInfo.TileHeightInTexels; ++y)
		{
			// Copy one row
			memcpy(pDstSlice + (y * alignedRowPitch),   // Dest: offset by ALIGNED pitch
				pSrcSlice + (y * unalignedRowSize),    // Src:  offset by UNALIGNED pitch
				unalignedRowSize);                 // Amount: one UNALIGNED row's worth
		}
	}
	uploadBuffer->Unmap(0, nullptr);

	ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();
	ID3D12CommandAllocator* allocator = nullptr;
	s_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));

	ID3D12GraphicsCommandList* cmdList = nullptr;
	s_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&cmdList));

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = tiledResource;
	barrier.Transition.Subresource = subResource;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	cmdList->ResourceBarrier(1, &barrier);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = tiledResource;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = subResource;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = uploadBuffer;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = 0;
	src.PlacedFootprint.Footprint.Format = desc.Format;
	src.PlacedFootprint.Footprint.Width = tilingInfo.TileWidthInTexels;
	src.PlacedFootprint.Footprint.Height = tilingInfo.TileHeightInTexels;
	src.PlacedFootprint.Footprint.Depth = tilingInfo.TileDepthInTexels;
	src.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

	D3D12_BOX srcBox = {};
	srcBox.right = tilingInfo.TileWidthInTexels;
	srcBox.bottom = tilingInfo.TileHeightInTexels;
	srcBox.back = tilingInfo.TileDepthInTexels;

	cmdList->CopyTextureRegion(&dst,
		tileX * tilingInfo.TileWidthInTexels,
		tileY * tilingInfo.TileHeightInTexels,
		tileZ * tilingInfo.TileDepthInTexels,
		&src, &srcBox);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	cmdList->ResourceBarrier(1, &barrier);

	cmdList->Close();

	ID3D12CommandList* cmdLists[] = { cmdList };
	queue->ExecuteCommandLists(1, cmdLists);

	ID3D12Fence* fence = nullptr;
	s_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	queue->Signal(fence, 1);
	if (fence->GetCompletedValue() < 1) {
		HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		fence->SetEventOnCompletion(1, event);
		WaitForSingleObject(event, 1000);
		CloseHandle(event);
	}
	fence->Release();
	cmdList->Release();
	allocator->Release();
	uploadBuffer->Release();
	
	return true;
}



UNITY_INTERFACE_EXPORT bool TestHeapBasicAllocation()
{
	if (!s_Device) return false;

	auto testHeap = std::make_unique<FixedHeap>(s_Device, 10 * 64 * 1024); // 10 tiles

	if (testHeap)
	{
		UNITY_LOG(s_Log, "Successfully created heap object");
	}
	else {
		UNITY_LOG_ERROR(s_Log, "Couldn't create heap object");
	}

	
	// Test 1: Simple allocation
	auto alloc1 = testHeap->AllocateTiles(3);
	if (!alloc1.success || alloc1.heapOffsetInTiles != 0) {
		UNITY_LOG_ERROR(s_Log, "Test failed: First allocation");
		return false;
	}
	UNITY_LOG(s_Log, "Successfully passed test 1");
	// Test 2: Second allocation should start after first
	auto alloc2 = testHeap->AllocateTiles(2);
	if (!alloc2.success || alloc2.heapOffsetInTiles != 3) {
		UNITY_LOG_ERROR(s_Log, "Test failed: Second allocation offset");
		return false;
	}
	UNITY_LOG(s_Log, "Successfully passed test 2");
	// Test 3: Capacity tracking
	if (testHeap->GetUsedTiles() != 5 || testHeap->GetFreeTiles() != 5) {
		UNITY_LOG_ERROR(s_Log, "Test failed: Capacity tracking");
		return false;
	}
	UNITY_LOG(s_Log, "Successfully passed test 3"); 
	// Test 4: Free and reallocate (tests coalescing)
	testHeap->FreeTiles(0, 3);
	testHeap->FreeTiles(3, 2);
	auto alloc3 = testHeap->AllocateTiles(5); // Should fit in coalesced space
	if (!alloc3.success || alloc3.heapOffsetInTiles != 0) {
		UNITY_LOG_ERROR(s_Log, "Test failed: Free/coalesce/reallocate");
		return false;
	}
	UNITY_LOG(s_Log, "Successfully passed test 4");
	
	// Test 5: Overflow
	auto allocFail = testHeap->AllocateTiles(10); // Only 5 tiles free
	if (allocFail.success) {
		UNITY_LOG_ERROR(s_Log, "Test failed: Should have failed overflow");
		return false;
	}
	
	UNITY_LOG(s_Log, "All heap tests passed!");
	return true;
}


UNITY_INTERFACE_EXPORT bool TestHeapFragmentation()
{
	auto testHeap = std::make_unique<FixedHeap>(s_Device, 10 * 64 * 1024);

	// Allocate pattern: [A][B][C][D]
	auto a = testHeap->AllocateTiles(2);
	auto b = testHeap->AllocateTiles(2);
	auto c = testHeap->AllocateTiles(2);
	auto d = testHeap->AllocateTiles(2);

	// Free B and D, creating fragmentation: [A][FREE][C][FREE]
	testHeap->FreeTiles(b.heapOffsetInTiles, 2);
	testHeap->FreeTiles(d.heapOffsetInTiles, 2);

	// Should have 4 free tiles but fragmented
	if (testHeap->GetFreeTiles() != 6) {

		char resultBuffer[256];
		sprintf_s(resultBuffer, "[C++] TestHeapFragmentation: Call complete. Value of numSubresources is now: %u", testHeap->GetFreeTiles());
		UNITY_LOG(s_Log, resultBuffer);



		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Wrong free count");
		return false;
	}

	auto bigAlloc = testHeap->AllocateTiles(5);
	if (bigAlloc.success) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Should fail - no 5-tile contiguous block");
		return false;
	}

	// But 4 tiles SHOULD succeed (fits in the [6-9] block)
	auto mediumAlloc = testHeap->AllocateTiles(4);
	if (!mediumAlloc.success) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: 4-tile alloc should succeed in [6-9] block");
		return false;
	}

	// Now only 2 tiles remain at [2-3]
	if (testHeap->GetFreeTiles() != 2) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Should have 2 tiles left");
		return false;
	}

	// This 2-tile allocation should also succeed
	auto smallAlloc = testHeap->AllocateTiles(2);
	if (!smallAlloc.success) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Final 2-tile alloc should succeed");
		return false;
	}

	// Now completely full
	if (testHeap->GetFreeTiles() != 0) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Should be completely full now");
		return false;
	}

	UNITY_LOG(s_Log, "Fragmentation test passed!");
	return true;
}