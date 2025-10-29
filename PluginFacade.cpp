#include "pch.h"

#include "PluginFacade.h"

#include <comdef.h>
#include "IUnityLog.h"
#include "IUnityGraphicsD3D12.h"
#include "IUnityInterface.h"
#include "FixedHeap.h"
#include <string>
#include <memory>
#include "RenderingPlugin.h"

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

static UINT64 g_CurrentSrvHandle = 0;

static ID3D12DescriptorHeap* g_SrvDescriptorHeap = nullptr;
static UINT g_DescriptorSize = 0;

static std::unique_ptr<RenderingPlugin> g_RenderPlugin;

// This function is called when the plugin is loaded
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	g_RenderPlugin = std::make_unique<RenderingPlugin>(unityInterfaces);
	s_Log = unityInterfaces->Get<IUnityLog>();

	auto* graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

// This function is called when the plugin is unloaded
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
	g_RenderPlugin.reset();
}

// Implementation of GFX device callback
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	switch (eventType)
	{
		// Gets D3D12 device when initialised
		case kUnityGfxDeviceEventInitialize:
		{

			g_RenderPlugin->InitializeGraphicsDevice();
			break;
		}

		// Sets D3D12 device to nullptr to ensure it is not used after shutdown
		case kUnityGfxDeviceEventShutdown:
		{
			g_RenderPlugin.reset();
			break;
		}
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
) {
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
	if (!outHeapOffset) {
		UNITY_LOG(s_Log, "AllocateTilesFromHeap: invalid parameter outHeapOffset == nullptr");
		return false;
	}
	if (!g_tileHeap) {
		UNITY_LOG(s_Log, "AllocateTilesFromHeap: tile heap (g_tileHeap) is null");
		return false;
	}


	TileAllocation alloc = g_tileHeap->AllocateTiles(numTiles);

	if (alloc.success) {
		*outHeapOffset = alloc.heapOffsetInTiles;
		return true;
	}
	char msg[128];
	snprintf(msg, sizeof(msg),
		"AllocateTilesFromHeap: failed to allocate %u tiles",
		numTiles);
	UNITY_LOG(s_Log, msg);

	return false;
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




	UINT64 unalignedTotalSize = (UINT64)unalignedRowSize
		* tilingInfo.TileHeightInTexels
		* tilingInfo.TileDepthInTexels;


	if (dataSize != (UINT)unalignedTotalSize) {
		char msg[256];
		sprintf_s(msg, sizeof(msg),
			"UploadDataToTile: dataSize mismatch. expected %llu got %u",
			(unsigned long long)unalignedTotalSize, dataSize);
		UNITY_LOG(s_Log, msg);
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
		UNITY_LOG(s_Log, "UploadDataToTile: CreateCommittedResource failed 0x%08x", hr);
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
	BYTE* srcBytes = reinterpret_cast<BYTE*>(sourceData);

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
	if (!AllocateTilesFromHeap(1, &heapOffsetInTiles)) {
		UNITY_LOG(s_Log, "UploadDataToTile: AllocateTilesFromHeap failed");
		uploadBuffer->Release();
		return false;
	}
	ID3D12Heap* tileHeap = GetTileHeap();
	if (!tileHeap) {
		UNITY_LOG(s_Log, "UploadDataToTile: GetTileHeap returned null");
		uploadBuffer->Release();
		return false;
	}

	// Map the single tile of the resource to the heap offset we allocated.
	if (!MapTilesToHeap(tiledResource, subResource, tileX, tileY, tileZ, 1, heapOffsetInTiles, tileHeap)) {
		UNITY_LOG(s_Log, "UploadDataToTile: MapTilesToHeap failed");
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
	dstLoc.pResource = tiledResource;
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
		UNITY_LOG(s_Log, "UploadDataToTile: cmdList->Close failed 0x%08x", hr);
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
		UNITY_LOG(s_Log, "UploadDataToTile: CreateFence failed 0x%08x", hr);
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