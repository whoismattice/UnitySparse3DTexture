#include "pch.h"

#include "RenderingPlugin.h"

#include <comdef.h>
#include "IUnityLog.h"
#include "IUnityGraphicsD3D12.h"
#include "IUnityInterface.h"
#include <string>


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

ID3D12Resource* UNITY_INTERFACE_API CreateVolumetricResource(int width, int height, int depth)
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
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8_UNORM,
	desc.SampleDesc.Count = 1,
	desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ID3D12Resource* reservedResource = nullptr;

	HRESULT hr = s_Device->CreateReservedResource(
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&reservedResource)
	);

	if (SUCCEEDED(hr))
	{
		UNITY_LOG(s_Log, "Successfully created a volumetric resource");
		
		GetResourceTiling(reservedResource);
		return reservedResource;
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

void GetResourceTiling(ID3D12Resource* resource)
{
	// Ensures D3D12 device and resource are both valid
	if (!s_Device || !resource)
	{
		UNITY_LOG_ERROR(s_Log, "GetResourceTiling: Device or resource is null");
	}

	D3D12_RESOURCE_DESC resourceDescription = resource->GetDesc();

	UINT numTilesForEntireResource;
	D3D12_PACKED_MIP_INFO packedMipInfo;
	D3D12_TILE_SHAPE resourceTileShape;
	UINT numSubresourceTilings = 1;
	D3D12_SUBRESOURCE_TILING subresourceTiling;

	s_Device->GetResourceTiling(
		resource,
		&numTilesForEntireResource,
		&packedMipInfo,
		&resourceTileShape,
		&numSubresourceTilings,
		0,
		&subresourceTiling
		);

	char buffer[256];
	
	sprintf_s(buffer, "Total tiles for resource: %u", numTilesForEntireResource);
	UNITY_LOG(s_Log, buffer);

	sprintf_s(buffer, "Total tiles for resource: %u", numTilesForEntireResource);

	sprintf_s(buffer, "Standard tile shape (WxHxD in texels): %u x %u x %u",
		resourceTileShape.WidthInTexels,
		resourceTileShape.HeightInTexels,
		resourceTileShape.DepthInTexels);
	UNITY_LOG(s_Log, buffer);

	// The subresourceTiling provides more detail for this specific subresource (mip level 0)
	sprintf_s(buffer, "Subresource tile count (WxHxD in tiles): %u x %u x %u",
		subresourceTiling.WidthInTiles,
		subresourceTiling.HeightInTiles,
		subresourceTiling.DepthInTiles);
	UNITY_LOG(s_Log, buffer);

	sprintf_s(buffer, "Subresource tile offset: %u", subresourceTiling.StartTileIndexInOverallResource);
	UNITY_LOG(s_Log, buffer);
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