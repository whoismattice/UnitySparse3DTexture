#pragma once
#include <d3d12.h>
#include <dxgi.h>
#include "IUnityLog.h"
#include <memory>
#include "TilingInfo.h"
#include <span>


class ReservedResource {
public:
	// Texture properties
	const UINT width;
	const UINT height;
	const UINT depth;
	const bool useMipMaps;
	const DXGI_FORMAT textureFormat;
	std::unique_ptr<ID3D12Resource> resource;


	ReservedResource(UINT width, UINT height, UINT depth, bool useMipMaps, UINT mipmapCount, DXGI_FORMAT format, ID3D12Device* device, IUnityLog* logger);

	std::unique_ptr<std::vector<ResourceTilingInfo>> GetTilingInfo();

	bool UploadDataToTile(
		UINT subresource,
		UINT tileX, UINT tileY, UINT tileZ,
		UINT HeapOffsetInTiles,
		std::span<const std::byte> data
	);

private:
	ID3D12Device* device;
	IUnityLog* logger;
	
};
