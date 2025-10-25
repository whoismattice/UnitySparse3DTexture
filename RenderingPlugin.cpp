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
			}
			else {
				UNITY_LOG(s_Log, "Found appropriate D3D12 device");
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
	if (testHeap->GetFreeTiles() != 4) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Wrong free count");
		return false;
	}

	// Try to allocate 4 contiguous - should fail due to fragmentation
	auto bigAlloc = testHeap->AllocateTiles(4);
	if (bigAlloc.success) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Should fail on fragmented alloc");
		return false;
	}

	// But 2 tiles should succeed
	auto smallAlloc = testHeap->AllocateTiles(2);
	if (!smallAlloc.success) {
		UNITY_LOG_ERROR(s_Log, "Fragmentation test: Small alloc should succeed");
		return false;
	}

	UNITY_LOG(s_Log, "Fragmentation test passed!");
	return true;
}