#pragma once
#include <d3d12.h>
#include <dxgi.h>
#include "IUnityLog.h"
#include <memory>
#include "TilingInfo.h"
#include <wrl/client.h>
#include <span>
#include <unordered_map>

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

	const ResourceTilingInfo& GetTilingInfo() const;
	
	void RegisterMappedTile(
		UINT subresource, 
		UINT x, UINT y, UINT z, 
		UINT heapOffset
	);

	bool GetMappedTileOffset(
		UINT subresource, 
		UINT x, UINT y, UINT z, 
		UINT* outOffset
	) const;

	void UnregisterMappedTile(
		UINT subresource, 
		UINT x, UINT y, UINT z
	);

	bool IsTileMapped(
		UINT subresource, 
		UINT x, UINT y, UINT z
	) const;

private:
	struct MappedTile {
		UINT heapOffset;
		UINT subResource;
		UINT tileX, tileY, tileZ;
	};
	std::unordered_map<UINT64, MappedTile> mappedTiles;

	UINT64 GetTileKey(UINT subresource, UINT x, UINT y, UINT z) const {
		return ((UINT64)subresource << 48) | ((UINT64)x << 32) | ((UINT64)y << 16) | z;
	}

	ID3D12Device* device;
	IUnityLog* logger;
	ResourceTilingInfo tilingInfo;
};
