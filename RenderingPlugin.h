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
#include <wil/resource.h>
#include <string>

struct TileMetrics {
	UINT bytesPerPixel;
	UINT unalignedRowSize;
};

struct TileMapping {
	UINT heapOffset;
	bool success;
};

class RenderingPlugin {
public:
	RenderingPlugin(IUnityInterfaces* unityInterface);

	void InitializeGraphicsDevice();

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
		ReservedResource* resource);

	bool UnmapTileFromHeap(
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		UINT tileOffsetInHeap,
		ReservedResource* resource);

	bool AllocateTileToHeap(UINT* outHeapOffset);

	bool UploadDataToTile(
		ReservedResource* resource,
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		const std::span<std::byte>& sourceData
	);

	bool DestroyVolumetricResource(ReservedResource* resource);

private:
	
	void Log(const std::string& message);

	void LogError(const std::string& message);

	bool ValidateTileUploadParams(
		const ReservedResource* resource,
		UINT subresource,
		const std::span<std::byte>& sourceData,
		D3D12_RESOURCE_DESC* outResourceDesc,
		ResourceTilingInfo* outResourceTilingInfo
	);

	TileMetrics CalculateTileMetrics(
		const D3D12_RESOURCE_DESC& desc,
		const ResourceTilingInfo& tilingInfo,
		UINT subResource
	);

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateAndFillUploadBuffer(
		const D3D12_RESOURCE_DESC& resourceDesc,
		UINT subResource,
		const std::span<std::byte>& sourceData,
		const ResourceTilingInfo& tilingInfo,
		const TileMetrics& metrics,
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT* outFootprint
	);

	TileMapping AllocateAndMapTileToHeap(
		ReservedResource* resource,
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ
	);

	bool ExecuteTileCopy(
		ID3D12Resource* uploadBuffer,
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
		ReservedResource* resource,
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		const ResourceTilingInfo& tilingInfo
	);



	IUnityInterfaces* s_UnityInterfaces;
	IUnityGraphics* s_Graphics;
	IUnityGraphicsD3D12v6* s_D3D12;
	IUnityLog* s_Log;
	ID3D12Device* s_Device;

	std::unique_ptr<IHeap> g_tileHeap;

	bool initialized;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_uploadFence;

	UINT64 m_fenceValue = 0;
	wil::unique_event m_fenceEvent = nullptr;


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


