# UnitySparseVolumetricResource

Unity native plugin (DLL) providing sparse 3D volume textures backed by D3D12 tiled resources. Built for a voxel game to efficiently store world data on the GPU — block albedo, lighting, and other per-voxel properties — streaming only the tiles that are visible or in use rather than uploading the entire volume.