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

// Forward declaration of internal static functions
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static std::unique_ptr<RenderingPlugin> g_RenderPlugin;

// This function is called when the plugin is loaded
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	
	g_RenderPlugin = std::make_unique<RenderingPlugin>(unityInterfaces);
	s_Log = unityInterfaces->Get<IUnityLog>();
	s_UnityInterfaces = unityInterfaces;
	IUnityGraphics* graphics = s_UnityInterfaces->Get<IUnityGraphics>();
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

ReservedResource* UNITY_INTERFACE_API CreateVolumetricResource(UINT width, UINT height, UINT depth, bool useMipmaps, UINT mipmapCount, DXGI_FORMAT format)
{
	try {
		ReservedResource* newResource = g_RenderPlugin->CreateVolumetricResource(
			width, height, depth,
			useMipmaps, mipmapCount,
			format
		);
		return newResource;
	}
	catch (const std::exception& ex) {
		UNITY_LOG(s_Log, ex.what());
		return nullptr;
	}
	return nullptr;
}

bool UNITY_INTERFACE_API DestroyVolumetricResource(ReservedResource* resource)
{
	try {
		return g_RenderPlugin->DestroyVolumetricResource(resource);
	}
	catch (const std::exception& ex) {
		UNITY_LOG(s_Log, ex.what());
		return true;
	}
	return false;
}

ID3D12Resource* UNITY_INTERFACE_API GetPointerToD3D12Resource(ReservedResource* resource)
{
	try {
		if (resource == nullptr)
		{
			UNITY_LOG_ERROR(s_Log, "Supplied ReservedResource object is null");
			return nullptr;
		}

		return resource->D3D12Resource.Get();
	}
	catch (const std::exception& ex) {
		UNITY_LOG_ERROR(s_Log, ex.what());
		return nullptr;
	}
}

UNITY_INTERFACE_EXPORT void GetResourceTilingInfo(ReservedResource* resource, C_ResourceTilingInfo* outInfo) {
	try {
		if (resource == nullptr)
		{
			return;
		}
	 	ResourceTilingInfo tilingInfo = resource->GetTilingInfo();
		outInfo->TileWidthInTexels = tilingInfo.TileWidthInTexels;
		outInfo->TileHeightInTexels= tilingInfo.TileHeightInTexels;
		outInfo->TileDepthInTexels= tilingInfo.TileDepthInTexels;
		outInfo->NumPackedMips = tilingInfo.NumPackedMips;
		outInfo->SubresourceCount = tilingInfo.SubresourceCount;
		outInfo->pSubresourceTilingInfo = tilingInfo.subresourceTilingInfo.data();
	}
	catch (const std::exception& ex)
	{
		UNITY_LOG_ERROR(s_Log, ex.what());
		return;
	}
}


UNITY_INTERFACE_EXPORT bool UploadDataToTile(
	ReservedResource* tiledResource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ,
	void* sourceData,
	UINT dataSize
) {
	try {
		if (tiledResource == nullptr)
		{
			UNITY_LOG_ERROR(s_Log, "Reserved resource is not assigned");
			return false;
		}

		std::span<std::byte> dataSpan(
			static_cast<std::byte*>(sourceData), dataSize
		);

		return g_RenderPlugin->UploadDataToTile(
			tiledResource,
			subResource,
			tileX, tileY, tileZ,
			dataSpan
		);
	}
	catch (const std::exception& ex)
	{
		UNITY_LOG_ERROR(s_Log, ex.what());
		return false;
	}
	

}

UNITY_INTERFACE_EXPORT bool UnmapTile(
	ReservedResource* resource,
	UINT subResource,
	UINT tileX, UINT tileY, UINT tileZ
)
{
	UNITY_LOG_ERROR(s_Log, "Unmapping tiles has not been implemented yet");
	return false;
}