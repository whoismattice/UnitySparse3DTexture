#pragma once

struct ResourceTilingInfo {
	unsigned int TileWidthInTexels;
	unsigned int TileHeightInTexels;
	unsigned int TileDepthInTexels;
	unsigned int SubresourceCount;
	unsigned int NumPackedMips;
};

struct SubresourceTilingInfo {
	unsigned int WidthInTiles;
	unsigned int HeightInTiles;
	unsigned int DepthInTiles;
	unsigned int StartTileIndex;
};
