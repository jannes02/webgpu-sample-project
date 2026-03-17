#pragma once

#include <dawn/webgpu_cpp.h>

class Application {
public:
	// A function called only once at the beginning. Returns false is init failed.
	bool onInit();

	// Where the GPU computation is actually issued
	void onCompute();

	// A function called only once at the very end.
	//void onFinish();

private:
	// Detailed steps
	bool initDevice();

	void initBindGroup();

	void initBindGroupLayout();

	void initComputePipeline();

	void initBuffers();

private:
	uint32_t m_bufferSize = 0;
	// Everything that is initialized in `onInit` and needed in `onCompute`.
	wgpu::Instance m_instance = nullptr;
    wgpu::Adapter m_adapter = nullptr;
	wgpu::Device m_device = nullptr;
	wgpu::PipelineLayout m_pipelineLayout = nullptr;
	wgpu::ComputePipeline m_pipeline = nullptr;
	wgpu::Buffer m_inputBuffer = nullptr;
	wgpu::Buffer m_outputBuffer = nullptr;
	wgpu::Buffer m_mapBuffer = nullptr;
	wgpu::BindGroup m_bindGroup = nullptr;
	wgpu::BindGroupLayout m_bindGroupLayout = nullptr;
	void *m_uncapturedErrorCallback = nullptr;
	void *m_deviceLostCallback = nullptr;
};