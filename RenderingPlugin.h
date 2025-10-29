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
#include "ReservedResouce.h"
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

	std::unique_ptr<std::vector<ResourceTilingInfo>> GetSubresourceTilingInfo(const ReservedResource& resource);
		

	bool MapTileToHeap(
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		UINT tileOffsetInHeap);

	bool UnmapTileFromHeap(
		UINT subResource,
		UINT tileX, UINT tileY, UINT tileZ,
		UINT tileOffsetInHeap);

	UINT AllocateTileToHeap();

	

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

};