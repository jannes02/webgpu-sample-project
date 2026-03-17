#include "Application.h"
#include "ResourceManager.h"

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm.hpp>

#include <dawn/webgpu_cpp.h>
#include <dawn/webgpu_cpp_print.h>
//#include "webgpu-release.h"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <array>

constexpr float PI = 3.14159265358979323846f;

using namespace wgpu;
using glm::mat4x4;
using glm::vec4;
using glm::vec3;
using glm::vec2;

bool Application::onInit() {
	m_bufferSize = 64 * sizeof(float);
	if (!initDevice()) return false;
	initBindGroupLayout();
	initComputePipeline();
	initBuffers();
	initBindGroup();
	return true;
}


bool Application::initDevice() {
    static const auto kTimedWaitAny = InstanceFeatureName::TimedWaitAny;
    InstanceDescriptor instanceDesc{.requiredFeatureCount = 1,
                                          .requiredFeatures = &kTimedWaitAny};
    m_instance = CreateInstance(&instanceDesc);

    Future f1 = m_instance.RequestAdapter(
        nullptr, CallbackMode::WaitAnyOnly,
        [&](RequestAdapterStatus status, Adapter a,
                  StringView message) {
          if (status != RequestAdapterStatus::Success) {
            // std::cout << "RequestAdapter: " << message << "\n";
            exit(0);
          }
          m_adapter = std::move(a);
        });
    m_instance.WaitAny(f1, UINT64_MAX);




    Limits supportedLimits;
    m_adapter.GetLimits(&supportedLimits);
    Limits requiredLimits{
        .maxTextureDimension1D = 4096,
        .maxTextureDimension2D = 4096,
        .maxTextureDimension3D = 2048, // some Intel integrated GPUs have this limit
        .maxTextureArrayLayers = 1,
        .maxBindGroups = 2,
        .maxSampledTexturesPerShaderStage = 3,
        .maxSamplersPerShaderStage = 1,
        .maxStorageBuffersPerShaderStage = 2,
        .maxUniformBuffersPerShaderStage = 2,
        .maxUniformBufferBindingSize = 16 * 4 * sizeof(float),
        .maxStorageBufferBindingSize = m_bufferSize,
        .minStorageBufferOffsetAlignment = supportedLimits.minStorageBufferOffsetAlignment,
        .maxVertexBuffers = 1,
        .maxBufferSize = m_bufferSize,
        .maxVertexAttributes = 6,
        .maxVertexBufferArrayStride = 68,
        .maxInterStageShaderVariables = 17,
        .maxComputeInvocationsPerWorkgroup = 32,
        .maxComputeWorkgroupSizeX = 32,
        .maxComputeWorkgroupSizeY = 1,
        .maxComputeWorkgroupSizeZ = 1,
        .maxComputeWorkgroupsPerDimension = 2,
        };

    DeviceDescriptor deviceDesc{};
    deviceDesc.SetUncapturedErrorCallback([](const Device&,
                                       ErrorType errorType,
                                       StringView message) {
      std::cout << "Error: " << errorType << " - message: " << message << "\n";
    });
    deviceDesc.label = "My fancy Device";
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.defaultQueue.label = "The default queue";

    Future f2 = m_adapter.RequestDevice(
        &deviceDesc, CallbackMode::WaitAnyOnly,
        [&](RequestDeviceStatus status, Device d,
            StringView message) {
          if (status != RequestDeviceStatus::Success) {
            std::cout << "RequestDevice: " << message << "\n";
            exit(0);
          }
          m_device = std::move(d);
        });
    WaitStatus status = m_instance.WaitAny(f2, UINT64_MAX);

    if (status != WaitStatus::Success) {
        std::cout << "WaitStatus: " << status << "\n" << std::endl;
    }


#ifdef WEBGPU_BACKEND_WGPU
	m_device.getQueue().submit(0, nullptr);
#else
	m_instance.ProcessEvents();
#endif

	return true;
}

// void Application::terminateDevice() {
// 	// RAII YAY
// }

void Application::initBindGroup() {
	// Create compute bind group
	std::vector<BindGroupEntry> entries(2, BindGroupEntry{});

	// Input buffer
	entries[0].binding = 0;
	entries[0].buffer = m_inputBuffer;
	entries[0].offset = 0;
	entries[0].size = m_bufferSize;

	// Output buffer
	entries[1].binding = 1;
	entries[1].buffer = m_outputBuffer;
	entries[1].offset = 0;
	entries[1].size = m_bufferSize;

	BindGroupDescriptor bindGroupDesc;
	bindGroupDesc.layout = m_bindGroupLayout;
	bindGroupDesc.entryCount = static_cast<uint32_t>(entries.size());
	bindGroupDesc.entries = entries.data();
	m_bindGroup = m_device.CreateBindGroup(&bindGroupDesc);
}

void Application::initBindGroupLayout() {
	// Create bind group layout
	std::vector<BindGroupLayoutEntry> bindings(2, BindGroupLayoutEntry{});

	// Input buffer
	bindings[0].binding = 0;
	bindings[0].buffer.type = BufferBindingType::ReadOnlyStorage;
	bindings[0].visibility = ShaderStage::Compute;

	// Output buffer
	bindings[1].binding = 1;
	bindings[1].buffer.type = BufferBindingType::Storage;
	bindings[1].visibility = ShaderStage::Compute;

	BindGroupLayoutDescriptor bindGroupLayoutDesc;
	bindGroupLayoutDesc.entryCount = (uint32_t)bindings.size();
	bindGroupLayoutDesc.entries = bindings.data();
	m_bindGroupLayout = m_device.CreateBindGroupLayout(&bindGroupLayoutDesc);
}

void Application::initComputePipeline() {
	// Load compute shader
	ShaderModule computeShaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/compute-shader.wgsl", m_device);

	// Create compute pipeline layout
	PipelineLayoutDescriptor pipelineLayoutDesc;
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
	m_pipelineLayout = m_device.CreatePipelineLayout(&pipelineLayoutDesc);

	// Create compute pipeline
	ComputePipelineDescriptor computePipelineDesc;
	computePipelineDesc.compute.constantCount = 0;
	computePipelineDesc.compute.constants = nullptr;
	computePipelineDesc.compute.entryPoint = "computeStuff";
	computePipelineDesc.compute.module = computeShaderModule;
	computePipelineDesc.layout = m_pipelineLayout;
	m_pipeline = m_device.CreateComputePipeline(&computePipelineDesc);
}

void Application::initBuffers() {
	// Create input/output buffers
	BufferDescriptor bufferDesc;
	bufferDesc.mappedAtCreation = false;
	bufferDesc.size = m_bufferSize;

	bufferDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
	m_inputBuffer = m_device.CreateBuffer(&bufferDesc);

	bufferDesc.usage = BufferUsage::Storage | BufferUsage::CopySrc;
	m_outputBuffer = m_device.CreateBuffer(&bufferDesc);

	// Create an intermediary buffer to which we copy the output and that can be
	// used for reading into the CPU memory.
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
	m_mapBuffer = m_device.CreateBuffer(&bufferDesc);
}


void Application::onCompute() {
	Queue queue = m_device.GetQueue();

	// Fill in input buffer
	std::vector<float> input(m_bufferSize / sizeof(float));
	for (int i = 0; i < input.size(); ++i) {
		input[i] = 0.1f * i;
	}
	queue.WriteBuffer(m_inputBuffer, 0, input.data(), input.size() * sizeof(float));

	// Initialize a command encoder
	CommandEncoderDescriptor encoderDesc = CommandEncoderDescriptor{};
	CommandEncoder encoder = m_device.CreateCommandEncoder(&encoderDesc);

	// Create compute pass
	ComputePassDescriptor computePassDesc;
	computePassDesc.timestampWrites = nullptr;
	ComputePassEncoder computePass = encoder.BeginComputePass(&computePassDesc);

	// Use compute pass
	computePass.SetPipeline(m_pipeline);
	computePass.SetBindGroup(0, m_bindGroup, 0, nullptr);

	uint32_t invocationCount = m_bufferSize / sizeof(float);
	uint32_t workgroupSize = 32;
	// This ceils invocationCount / workgroupSize
	uint32_t workgroupCount = (invocationCount + workgroupSize - 1) / workgroupSize;
	computePass.DispatchWorkgroups(workgroupCount, 1, 1);

	// Finalize compute pass
	computePass.End();

	// Before encoder.finish
	encoder.CopyBufferToBuffer(m_outputBuffer, 0, m_mapBuffer, 0, m_bufferSize);

	// Encode and submit the GPU commands
    CommandBufferDescriptor commandBufferDesc = CommandBufferDescriptor{};
	CommandBuffer commands = encoder.Finish(&commandBufferDesc);
	queue.Submit(1, &commands);

    // MapAsync gibt jetzt ein Future zurück
    wgpu::Future future = m_mapBuffer.MapAsync(
        wgpu::MapMode::Read,
        0,
        m_bufferSize,
        wgpu::CallbackMode::WaitAnyOnly,  // neu: muss explizit angegeben werden
        [&](wgpu::MapAsyncStatus status, wgpu::StringView message) {
            if (status == wgpu::MapAsyncStatus::Success) {
                const float* output =
                    (const float*)m_mapBuffer.GetConstMappedRange(0, m_bufferSize);
                for (int i = 0; i < input.size(); ++i) {
                    std::cout << "input " << input[i]
                              << " became " << output[i] << "\n";
                }
                m_mapBuffer.Unmap();
            } else {
                std::cout << "MapAsync failed: " << message << "\n";
            }
        });

    // Statt done-bool: Future synchron abwarten
    WaitStatus waitStatus = m_instance.WaitAny(future, UINT64_MAX);

	if (waitStatus != WaitStatus::Success) {
	    std::cout << "waitStatus failed: " << waitStatus << "\n";
	}

}