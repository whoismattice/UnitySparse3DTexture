#pragma once
#include <d3d12.h>
#include <dxgi.h>
#include "IUnityLog.h"
#include <memory>
#include "TilingInfo.h"
#include <wrl/client.h>
#include <span>


class ReservedResource {
public:
	// Texture properties
	const UINT width;
	const UINT height;
	const UINT depth;
	const bool useMipMaps;
	const UINT mipMapCount;
	const DXGI_FORMAT textureFormat;
	Microsoft::WRL::ComPtr<ID3D12Resource> D3D12Resource;


	ReservedResource(UINT width, UINT height, UINT depth, bool useMipMaps, UINT mipmapCount, DXGI_FORMAT format, ID3D12Device* device, IUnityLog* logger);

	ResourceTilingInfo GetTilingInfo() const;
	

	bool UploadDataToTile(
		UINT subresource,
		UINT tileX, UINT tileY, UINT tileZ,
		std::span<const std::byte> data
	);

	bool UnloadTile(
		UINT subresource,
		UINT tileX, UINT tileY, UINT tileZ
	);

private:
	ID3D12Device* device;
	IUnityLog* logger;
	ResourceTilingInfo tilingInfo;
};
