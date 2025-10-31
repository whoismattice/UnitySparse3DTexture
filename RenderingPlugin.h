#include <d3d12.h>
#include <dxgi.h>
#include "IUnityInterface.h"
#include "IUnityGraphics.h"
#include "IUnityGraphicsD3D12.h"
#include "IUnityLog.h"
#include <wrl/client.h>
#include <memory>
#include <vector>
#include "IHeap.h"
#include "ReservedResource.h"
#include <string>

class RenderingPlugin {
public:
	RenderingPlugin(IUnityInterfaces* unityInterface);

	void InitializeGraphicsDevice();
	UINT GetSrvForResource();

	ReservedResource* CreateVolumetricResource(
		UINT width, UINT height, UINT depth, 
		bool useMipmaps,
		UINT mipmapCount,
		DXGI_FORMAT format
	);	

	bool MapTileToHeap(
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		UINT tileOffsetInHeap,
		const ReservedResource& resource);

	bool UnmapTileFromHeap(
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		UINT tileOffsetInHeap,
		const ReservedResource& resource);

	bool AllocateTileToHeap(UINT* outHeapOffset);

	bool UploadDataToTile(
		const ReservedResource& resource,
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		std::span<std::byte> sourceData
	);

private:
	
	void Log(const std::string& message);

	void LogError(const std::string& message);

	void InitialiseSrvDescriptorHeap();

	IUnityInterfaces* s_UnityInterfaces;
	IUnityGraphics* s_Graphics;
	IUnityGraphicsD3D12v6* s_D3D12;
	IUnityLog* s_Log;
	ID3D12Device* s_Device;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_SrvDescriptorHeap;

	std::unique_ptr<IHeap> g_tileHeap;

	UINT64 g_CurrentSrvHandle;
	UINT g_DescriptorSize;

	bool initialized;

	std::vector<std::unique_ptr<ReservedResource>> g_resources;

	UINT GetBytesPerPixel(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return 16;

		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R32G32_FLOAT:
			return 8;

		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
			return 4;

		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R8G8_UNORM:
			return 2;

		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SINT:
			return 1;

		default:
			// Format not handled or is block-compressed
			return 0;
		}
	}
};