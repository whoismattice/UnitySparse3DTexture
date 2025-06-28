#pragma once

struct TilingInfo {
	unsigned int TileWidthInTexels;
	unsigned int TileHeightInTexels;
	unsigned int TileDepthInTexels;

	unsigned int SubresourceWidthInTiles;
	unsigned int SubresourceHeightInTiles;
};
