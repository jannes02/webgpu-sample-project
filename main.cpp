// ============================================================================
// WebGPU / Dawn – minimal triangle example
//
// This program opens a GLFW window and renders a single triangle using the
// WebGPU API via Google's Dawn backend.  Shader source is loaded from
// external .wgsl files at runtime so they can be edited without recompiling.
// ============================================================================

#include <fstream>
#include <iostream>

#include <GLFW/glfw3.h>

//#include "dawn/src/dawn/native/Adapter.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
#include <dawn/webgpu_cpp_print.h>   // Enables std::cout << for Dawn enum types
#include <webgpu/webgpu_cpp.h>       // C++ wrapper around the raw WebGPU API
#include <webgpu/webgpu_glfw.h>      // Helper: create a WebGPU surface from a GLFW window

using namespace wgpu;

// Global WebGPU objects
// The WebGPU object hierarchy is:  Instance → Adapter → Device → everything else
Instance instance;        // Entry point; owns the event loop and all child objects
Adapter   adapter;        // Represents a physical GPU (or software fallback)
Device    device;         // Logical device; used to create all GPU resources
RenderPipeline pipeline;  // Compiled vertex + fragment shader pair with fixed-function state
PipelineLayout pipelineLayout;
BindGroupLayout bindGroupLayout;
BindGroup bindGroup;

// Swap-chain / surface state
Surface      surface;  // Platform window surface (backed by GLFW here)
TextureFormat format;  // Preferred swap-chain texture format reported by the surface

const uint32_t kWidth  = 1600;
const uint32_t kHeight = 900;

Limits deviceLimits;

// Shader source (loaded once from disk, kept alive for the pipeline lifetime)
std::string vertexCode;
std::string fragmentCode;

// Vertex Buffer
std::vector<float> pointData;
// = {
//     // x,   y,     r,   g,   b
//     -0.5, -0.5,   1.0, 0.0, 0.0,
//     +0.5, -0.5,   0.0, 1.0, 0.0,
//     +0.5, +0.5,   0.0, 0.0, 1.0,
//     -0.5, +0.5,   1.0, 1.0, 0.0
// };
std::vector<uint16_t> indexData;
// = {
//     0, 1, 2, // Triangle #0 connects points #0, #1 and #2
//     0, 2, 3  // Triangle #1 connects points #0, #2 and #3
// };
//uint32_t vertexCount = static_cast<uint32_t>(vertexData.size() / 5);
Buffer pointBuffer;
Buffer indexBuffer;
Buffer uniformBuffer;
uint32_t uniformStride;
uint32_t indexCount;
float currentTime = 1.0f;

struct MyUniforms {
    std::array<float, 4> color;  // offset = 0 * sizeof(vec4f) -> OK
    float time;                  // offset = 16 = 4 * sizeof(f32) -> OK
    float _pad[3];
};
// Have the compiler check byte alignment
static_assert(sizeof(MyUniforms) % 16 == 0);

// When isDev is true, shaders are loaded from the ../shaders/ directory
// (convenient during development without an install step).
const bool isDev = true;


// ConfigureSurface
//
// Queries the surface for the texture formats it supports, picks the first
// (preferred) one, and configures the swap-chain dimensions and device.
// Must be called after the device and surface are both ready.
void ConfigureSurface() {
    // Ask the surface what it can do on this adapter
    SurfaceCapabilities capabilities;
    surface.GetCapabilities(adapter, &capabilities);
    format = capabilities.formats[0];  // Use the driver-preferred format

    SurfaceConfiguration config{
        .device = device,
        .format = format,
        .width  = kWidth,
        .height = kHeight
    };
    surface.Configure(&config);
}


// Init
//
// Bootstraps the WebGPU object hierarchy synchronously:
//   1. Create an Instance (with TimedWaitAny support so we can block below)
//   2. Request an Adapter  (selects the GPU)
//   3. Request a Device    (opens a logical connection to that GPU)
//
// Both request calls use WaitAnyOnly callbacks so the futures can be awaited
// with instance.WaitAny(), keeping initialization simple and sequential.
void Init() {
    // --- 1. Instance ---------------------------------------------------------
    // TimedWaitAny is required to use instance.WaitAny() on native platforms.
    static const auto kTimedWaitAny = InstanceFeatureName::TimedWaitAny;
    InstanceDescriptor instanceDesc{
        .requiredFeatureCount = 1,
        .requiredFeatures     = &kTimedWaitAny
    };
    instance = CreateInstance(&instanceDesc);

    // --- 2. Adapter ----------------------------------------------------------
    // nullptr = default options (let Dawn pick the best available adapter).
    Future f1 = instance.RequestAdapter(
        nullptr, CallbackMode::WaitAnyOnly,
        [](RequestAdapterStatus status, Adapter a,
           StringView message) {
            if (status != RequestAdapterStatus::Success) {
                std::cout << "RequestAdapter: " << message << "\n";
                exit(0);
            }
            adapter = std::move(a);
        });
    instance.WaitAny(f1, UINT64_MAX);  // Block until the adapter is ready

    // --- 3. Device -----------------------------------------------------------
    DeviceDescriptor desc{};

    // Global error callback: any uncaptured GPU error will be printed here.
    desc.SetUncapturedErrorCallback(
        [](const Device&, ErrorType errorType,
           StringView message) {
            std::cout << "Error: " << errorType << " - message: " << message << "\n";
        });
    desc.SetDeviceLostCallback(
        CallbackMode::WaitAnyOnly,  // ← CallbackMode erforderlich!
        [](const Device&, DeviceLostReason reason, StringView message) {
        std::cout << "Device lost: " << reason << " - " << message << std::endl;
        });
    //
    // wgpu::Limits limits{};
    // limits.maxDynamicUniformBuffersPerPipelineLayout = 16;
    // limits.maxDynamicStorageBuffersPerPipelineLayout = 16;
    // desc.requiredLimits = &limits;



    Future f2 = adapter.RequestDevice(
        &desc, CallbackMode::WaitAnyOnly,
        [](RequestDeviceStatus status, Device d,
           StringView message) {
            if (status != RequestDeviceStatus::Success) {
                std::cout << "RequestDevice: " << message << "\n";
                exit(0);
            }
            device = std::move(d);
        });
    WaitStatus waitStatus2 = instance.WaitAny(f2, UINT64_MAX);  // Block until device is ready

    bool success = adapter.GetLimits(&deviceLimits);
    assert(success);
}


// LoadShaderFromFile
//
// Reads a WGSL shader file from disk and returns its contents as a string.
// The path is relative to the ../shaders/ directory in dev mode.
std::string LoadShaderFromFile(const std::string& path) {
    std::string pathStr = isDev ? "../shaders/" + path : path;
    std::ifstream file(pathStr);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + pathStr);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

#include <vector>
#include <filesystem>
#include <sstream>
#include <string>

bool loadGeometry(
    const std::filesystem::path& path,
    std::vector<float>& pointData,
    std::vector<uint16_t>& indexData
) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    pointData.clear();
    indexData.clear();

    enum class Section {
        None,
        Points,
        Indices,
    };
    Section currentSection = Section::None;

    float value;
    uint16_t index;
    std::string line;
    while (!file.eof()) {
        getline(file, line);

        // overcome the `CRLF` problem
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line == "[points]") {
            currentSection = Section::Points;
        }
        else if (line == "[indices]") {
            currentSection = Section::Indices;
        }
        else if (line[0] == '#' || line.empty()) {
            // Do nothing, this is a comment
        }
        else if (currentSection == Section::Points) {
            std::istringstream iss(line);
            // Get x, y, r, g, b
            for (int i = 0; i < 5; ++i) {
                iss >> value;
                pointData.push_back(value);
            }
        }
        else if (currentSection == Section::Indices) {
            std::istringstream iss(line);
            // Get corners #0 #1 and #2
            for (int i = 0; i < 3; ++i) {
                iss >> index;
                indexData.push_back(index);
            }
        }
    }
    return true;
}

/**
 * Round 'value' up to the next multiplier of 'step'.
 */
uint32_t ceilToNextMultiple(uint32_t value, uint32_t step) {
    uint32_t divide_and_ceil = value / step + (value % step == 0 ? 0 : 1);
    return step * divide_and_ceil;
}


// CreateRenderPipeline
//
// Loads WGSL source from disk, compiles two shader modules (vertex + fragment),
// and assembles them into a RenderPipeline.
//
// WebGPU pipeline overview:
//   ShaderModule   – compiled WGSL code for one shader stage
//   ColorTargetState – describes the render target format and blend settings
//   FragmentState    – ties the fragment ShaderModule to its render targets
//   RenderPipelineDescriptor – combines all stages + fixed-function state
void CreateRenderPipeline() {
    // Load WGSL source and wrap it in the Dawn-specific chained struct
    vertexCode = LoadShaderFromFile("main.vertex.wgsl");
    ShaderSourceWGSL vert{{.code = vertexCode.c_str()}};

    fragmentCode = LoadShaderFromFile("main.fragment.wgsl");
    ShaderSourceWGSL frag{{.code = fragmentCode.c_str()}};

    // Compile the vertex shader module
    const ShaderModuleDescriptor vertDescriptor{
        .nextInChain = &vert,
        .label       = "Vertex Shader"
    };
    ShaderModule vertModule = device.CreateShaderModule(&vertDescriptor);

    // Compile the fragment shader module
    const ShaderModuleDescriptor fragDescriptor{
        .nextInChain = &frag,
        .label       = "Fragment Shader"
    };
    ShaderModule fragModule = device.CreateShaderModule(&fragDescriptor);

    // Describe the single color render target (matches the swap-chain format)
    ColorTargetState colorTargetState{.format = format};

    // Bind the fragment module to its output targets
    FragmentState fragmentState{
        .module      = fragModule,
        .entryPoint  = "fragmentMain",
        .targetCount = 1,
        .targets     = &colorTargetState
    };

    // // Assemble the full render pipeline (no vertex buffers needed here –
    // // vertex positions are generated procedurally inside the vertex shader)
    // wgpu::RenderPipelineDescriptor descriptor{
    //     .vertex   = {.module = vertModule},
    //     .fragment = &fragmentState
    // };

    VertexAttribute vertexAttributes[2];
    vertexAttributes[0].format = VertexFormat::Float32x2;  // vec2f in WGSL
    vertexAttributes[0].offset = 0;
    vertexAttributes[0].shaderLocation = 0;
    vertexAttributes[1].format = VertexFormat::Float32x3;  // vec2f in WGSL
    vertexAttributes[1].offset = 2 * sizeof(float);              // @location(1)
    vertexAttributes[1].shaderLocation = 1;

    VertexBufferLayout vertexBufferLayout{
        .stepMode = VertexStepMode::Vertex,   // pro Vertex (nicht pro Instance)
        .arrayStride = 5 * sizeof(float),         // 2 floats = 8 bytes pro Vertex
        .attributeCount = static_cast<uint32_t>(std::size(vertexAttributes)),                    // Nur Position
        .attributes = vertexAttributes};


    BindGroupLayoutEntry bindingLayout{
        .binding = 0,
        .visibility = ShaderStage::Vertex | ShaderStage::Fragment,};
    bindingLayout.buffer.hasDynamicOffset = true;
    bindingLayout.buffer.type = BufferBindingType::Uniform;
    bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

    BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
        .entryCount = 1,
        .entries = &bindingLayout,};
    bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    PipelineLayoutDescriptor pipelineLayoutDescriptor{
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = (BindGroupLayout*)&bindGroupLayout};
    pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);


    // ========== PIPELINE DESCRIPTOR (ERWEITERT) ==========
    RenderPipelineDescriptor descriptor{
        .layout = pipelineLayout,
        .vertex   = {
            .module = vertModule,
            .entryPoint = "vertexMain",
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        },
        .fragment = &fragmentState,
    };

    pipeline = device.CreateRenderPipeline(&descriptor);
}

void CreateBindGroups() {

    BindGroupEntry bindingEntry{
        .binding = 0,
        .buffer = uniformBuffer,
        .offset = 0,
        .size = sizeof(MyUniforms)};

    BindGroupDescriptor bindGroupDescriptor{
        .layout = bindGroupLayout,
        .entryCount = 1,
        .entries = &bindingEntry};
    bindGroup = device.CreateBindGroup(&bindGroupDescriptor);
}


// Render
//
// Records and submits a single frame to the GPU.
//
// WebGPU recording model:
//   CommandEncoder     – records a sequence of GPU commands
//   RenderPassEncoder  – sub-encoder for draw calls within one render pass
//   CommandBuffer      – the finished, immutable command recording
//   Queue::Submit      – hands the command buffer off to the GPU for execution
void Render() {
    // Acquire the next texture from the swap-chain
    SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);

    // Describe a render pass that clears the target and stores the result
    RenderPassColorAttachment attachment{
        .view    = surfaceTexture.texture.CreateView(),
        .loadOp  = LoadOp::Clear,   // Clear the texture at pass start
        .storeOp = StoreOp::Store    // Keep the rendered result after the pass
    };

    RenderPassDescriptor renderpass{
        .colorAttachmentCount = 1,
        .colorAttachments     = &attachment
    };

    // Record commands
    CommandEncoder encoder = device.CreateCommandEncoder();
    RenderPassEncoder pass = encoder.BeginRenderPass(&renderpass);
    pass.SetPipeline(pipeline);
    pass.SetVertexBuffer(0, pointBuffer, 0, pointBuffer.GetSize());
    pass.SetIndexBuffer(indexBuffer, IndexFormat::Uint16, 0, indexBuffer.GetSize());

    uint32_t dynamicOffset = 0;
    pass.SetBindGroup(0, bindGroup, 1, &dynamicOffset);
    pass.DrawIndexed(indexCount, 1, 0, 0, 0);  // Draw 3 vertices (one triangle); no vertex buffer needed

    dynamicOffset = 1 * uniformStride;
    pass.SetBindGroup(0, bindGroup, 1, &dynamicOffset);
    pass.DrawIndexed(indexCount, 1, 0, 0, 0);  // Draw 3 vertices (one triangle); no vertex buffer needed

    pass.End();

    // Finalise recording and submit to the GPU queue
    CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

}


void CreateBuffers() {

    loadGeometry("../resources/webgpu.txt", pointData, indexData);

    indexCount = static_cast<uint32_t>(indexData.size());

    const size_t pointSize = pointData.size() * sizeof(float);
    const size_t pointSizePadded = (pointSize + 3) & ~3;
    BufferDescriptor pointBufferDescriptor;
    pointBufferDescriptor.size = pointSizePadded;
    pointBufferDescriptor.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    pointBufferDescriptor.mappedAtCreation = false;
    pointBuffer = device.CreateBuffer(&pointBufferDescriptor);
    device.GetQueue().WriteBuffer(pointBuffer, 0, pointData.data(), pointSizePadded);

    const size_t indexSize = indexData.size() * sizeof(uint16_t);
    const size_t indexSizePadded = (indexSize + 3) & ~3;
    BufferDescriptor indexBufferDescriptor;
    indexBufferDescriptor.size = indexSizePadded;
    indexBufferDescriptor.usage = BufferUsage::CopyDst | BufferUsage::Index;
    indexBufferDescriptor.mappedAtCreation = false;
    indexBuffer = device.CreateBuffer(&indexBufferDescriptor);
    device.GetQueue().WriteBuffer(indexBuffer, 0, indexData.data(), indexSizePadded);

    uniformStride = ceilToNextMultiple(
        (uint32_t)sizeof(MyUniforms),
        (uint32_t)256);//(uint32_t)deviceLimits.minUniformBufferOffsetAlignment);
    BufferDescriptor uniformBufferDescriptor;
    uniformBufferDescriptor.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    uniformBufferDescriptor.size = uniformStride + sizeof(MyUniforms);  // but only one float is actually used
    uniformBufferDescriptor.mappedAtCreation = false;
    uniformBuffer = device.CreateBuffer(&uniformBufferDescriptor);

    MyUniforms uniforms;
    uniforms.time = 1.0f;
    uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
    device.GetQueue().WriteBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));

    uniforms.time = 2.0f;
    uniforms.color = {1.0f, 1.0f, 1.0f, 0.7f};
    device.GetQueue().WriteBuffer(uniformBuffer, uniformStride, &uniforms, sizeof(MyUniforms));
}

// InitGraphics
//
// Called once after the GLFW window and WebGPU surface are ready.
// Configures the swap-chain and creates the render pipeline.
void InitGraphics() {
    ConfigureSurface();
    CreateBuffers(); //todo: vertexBuffer.release();
    CreateRenderPipeline();
    CreateBindGroups();

}

void Terminate() {
    std::cout << "Terminating..." << std::endl;
    // pointBuffer.Destroy();
    // indexBuffer.Destroy();
    //glfwTerminate();
}


// Start
//
// Creates the GLFW window, ties it to a WebGPU surface, initialises graphics,
// then runs the main loop.
//
// On Emscripten the loop is handed to the browser's requestAnimationFrame via
// emscripten_set_main_loop; on native platforms it is a simple while loop.
void Start() {
    if (!glfwInit()) {
        return;
    }

    // Disable OpenGL context creation – WebGPU manages its own graphics context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(kWidth, kHeight, "WebGPU window", nullptr, nullptr);

    // Create a WebGPU surface that wraps the native window handle
    surface = glfw::CreateSurfaceForWindow(instance, window);

    InitGraphics();

#if defined(__EMSCRIPTEN__)
    // Browser environment: yield control back to the JS event loop each frame
    emscripten_set_main_loop(Render, 0, false);
#else
    // Native environment: poll events, render, present, and process GPU callbacks
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        MyUniforms uniforms{};
        float time = static_cast<float>(glfwGetTime());
        float time2 = static_cast<float>(glfwGetTime() + 1);
        device.GetQueue().WriteBuffer(uniformBuffer, offsetof(MyUniforms, time), &time, sizeof(float));
        device.GetQueue().WriteBuffer(uniformBuffer, uniformStride + offsetof(MyUniforms, time), &time2, sizeof(float));

        // // Update uniform buffer only color example
        // uniforms.color = { 1.0f, 0.5f, 0.0f, 1.0f };
        // queue.writeBuffer(uniformBuffer, sizeof(float), &uniforms.color, sizeof(Color));
        // //                               ^^^^^^^^^^^^^ offset of `color` in the uniform struct

        Render();
        surface.Present();          // Flip the swap-chain (show the rendered frame)
        instance.ProcessEvents();   // Drive async callbacks (e.g. buffer map completions)
        //vertexData.at(1) += 0.01;
        //CreateBuffers();
    }
    Terminate();
#endif
}





// Entry point
int main() {
    Init();   // Set up WebGPU instance, adapter and device
    Start();  // Create window, build pipeline, run render loop
    return 0;
}