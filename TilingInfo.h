#pragma once
#include <memory>
#include <vector>


struct SubresourceTilingInfo {
	unsigned int WidthInTiles;
	unsigned int HeightInTiles;
	unsigned int DepthInTiles;
	unsigned int StartTileIndex;
};

class ResourceTilingInfo {
public:
	unsigned int TileWidthInTexels;
	unsigned int TileHeightInTexels;
	unsigned int TileDepthInTexels;
	unsigned int SubresourceCount;
	unsigned int NumPackedMips;
	std::vector<SubresourceTilingInfo> subresourceTilingInfo;
};
