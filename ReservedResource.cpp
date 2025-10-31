#include "pch.h"
#include "ReservedResource.h"


ReservedResource::ReservedResource(UINT width, UINT height, UINT depth, bool useMipMaps, UINT mipmapCount, DXGI_FORMAT format, ID3D12Device* device, IUnityLog* logger) :
	width(width), height(height), depth(depth), useMipMaps(useMipMaps), mipMapCount(mipmapCount), textureFormat(format), device(device), logger(logger)
{

	D3D12_RESOURCE_DESC desc = {};

	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	desc.Alignment = 0;
	desc.Width = static_cast<UINT64>(width);
	desc.Height = static_cast<UINT>(height);
	desc.DepthOrArraySize = static_cast<UINT16>(depth);
	desc.MipLevels = useMipMaps ? mipmapCount : 1;
	desc.Format = static_cast<DXGI_FORMAT>(format),
		desc.SampleDesc.Count = 1,
		desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = device->CreateReservedResource(
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&D3D12Resource)
	);

	D3D12_RESOURCE_DESC resourceDescription = D3D12Resource->GetDesc();
	UINT numTilesForEntireResource = 0;
	D3D12_PACKED_MIP_INFO packedMipInfo;
	D3D12_TILE_SHAPE resourceTileShape;
	UINT numSubresources = resourceDescription.MipLevels;

	std::vector<D3D12_SUBRESOURCE_TILING> subresourceTilings(numSubresources);

	device->GetResourceTiling(
		D3D12Resource.Get(),
		&numTilesForEntireResource,
		&packedMipInfo,
		&resourceTileShape,
		&numSubresources,
		0,
		subresourceTilings.data()
	);

	tilingInfo.TileWidthInTexels = resourceTileShape.WidthInTexels;
	tilingInfo.TileHeightInTexels = resourceTileShape.HeightInTexels;
	tilingInfo.TileDepthInTexels= resourceTileShape.DepthInTexels;
	tilingInfo.SubresourceCount = numSubresources;
	tilingInfo.NumPackedMips = packedMipInfo.NumPackedMips;
	for (int i = 0; i < subresourceTilings.size(); i++)
	{
		tilingInfo.subresourceTilingInfo.emplace_back(SubresourceTilingInfo(
			subresourceTilings[i].WidthInTiles,
			subresourceTilings[i].HeightInTiles,
			subresourceTilings[i].DepthInTiles,
			subresourceTilings[i].StartTileIndexInOverallResource
		));
	}
}

ResourceTilingInfo ReservedResource::GetTilingInfo() const {
	return tilingInfo;
}


bool ReservedResource::UploadDataToTile(
	UINT subresource,
	UINT tileX, UINT tileY, UINT tileZ,
	std::span<const std::byte> data
) {
	return false;
}

bool ReservedResource::UnloadTile(UINT subresource,
	UINT tileX, UINT tileY, UINT tileZ
) {
	return false;
}