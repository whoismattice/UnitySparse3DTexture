#include "pch.h"
#include <stdexcept>
#include "FixedHeap.h"

#include "RenderingPlugin.h"

RenderingPlugin::RenderingPlugin(IUnityInterfaces* unityInterface) : s_UnityInterfaces(unityInterface) {

	try {
		s_Graphics = unityInterface->Get<IUnityGraphics>();
		s_Log = unityInterface->Get<IUnityLog>();
	}
	catch (const std::runtime_error& ex)
	{
		Log(ex.what());
	}
}

void RenderingPlugin::InitializeGraphicsDevice()
{
	try {
		s_D3D12 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v6>();
		s_Device = s_D3D12->GetDevice();
		if (s_D3D12 == nullptr || s_Device == nullptr)
		{
			return;
		}
		g_tileHeap = std::make_unique<FixedHeap>(s_Device, 512 * 1024 * 1024);
		InitialiseSrvDescriptorHeap();
		Log("Found appropriate D3D12 device");
		Log("TODO: CHECK FEATURE LEVEL");
		initialized = true;
	}
	catch (const std::runtime_error& ex)
	{
		LogError(ex.what());
	}
}

void RenderingPlugin::Log(const std::string& message)
{
	UNITY_LOG(s_Log, message.c_str());
}

void RenderingPlugin::LogError(const std::string& message)
{
	UNITY_LOG_ERROR(s_Log, message.c_str());
}

void RenderingPlugin::InitialiseSrvDescriptorHeap() {
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

	heapDesc.NumDescriptors = 256;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	s_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_SrvDescriptorHeap));
	g_DescriptorSize = s_Device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
	);
}

ReservedResource* RenderingPlugin::CreateVolumetricResource(
	UINT width, UINT height, UINT depth, 
	bool useMipmaps,
	UINT mipmapCount,
	DXGI_FORMAT format
) {
	if (!initialized) {
		LogError("CreateVolumetricResource called before plugin initialised with D3D12 device");
		return nullptr;
	}
	try {
		
		g_resources.push_back(std::make_unique<ReservedResource>(
			width, height, depth,
			useMipmaps,
			static_cast<UINT>(useMipmaps ? mipmapCount : 1),
			format, s_Device, s_Log
		));

		return g_resources.back().get();
	}
	catch (const std::runtime_error& ex)
	{
		LogError(ex.what());
		return nullptr;
	}
}
 
std::unique_ptr<std::vector<ResourceTilingInfo>> RenderingPlugin::GetSubresourceTilingInfo(const ReservedResource& resource)
{
	try {
		for (const auto& var : g_resources)
		{
			if (var->resource == resource.resource)
			{
				return var->GetTilingInfo();
			}
			return std::make_unique<std::vector<ResourceTilingInfo>>();
		}

	} catch (const std::runtime_error& ex)
	{
		LogError(ex.what());
	}
}