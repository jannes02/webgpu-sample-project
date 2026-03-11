#include <fstream>
#include <iostream>

#include <GLFW/glfw3.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
#include <dawn/webgpu_cpp_print.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_glfw.h>

wgpu::Instance instance;
wgpu::Adapter adapter;
wgpu::Device device;
wgpu::RenderPipeline pipeline;

wgpu::Surface surface;
wgpu::TextureFormat format;
const uint32_t kWidth = 512;
const uint32_t kHeight = 512;

std::string vertexCode;
std::string fragmentCode;

const bool isDev = true;

void ConfigureSurface() {
    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(adapter, &capabilities);
    format = capabilities.formats[0];

    wgpu::SurfaceConfiguration config{.device = device,
                                      .format = format,
                                      .width = kWidth,
                                      .height = kHeight};
    surface.Configure(&config);
}

void Init() {
    static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc{.requiredFeatureCount = 1,
                                          .requiredFeatures = &kTimedWaitAny};
    instance = wgpu::CreateInstance(&instanceDesc);

    wgpu::Future f1 = instance.RequestAdapter(
      nullptr, wgpu::CallbackMode::WaitAnyOnly,
      [](wgpu::RequestAdapterStatus status, wgpu::Adapter a,
         wgpu::StringView message) {
        if (status != wgpu::RequestAdapterStatus::Success) {
          std::cout << "RequestAdapter: " << message << "\n";
          exit(0);
        }
        adapter = std::move(a);
      });
    instance.WaitAny(f1, UINT64_MAX);

    wgpu::DeviceDescriptor desc{};
    desc.SetUncapturedErrorCallback([](const wgpu::Device&,
                                     wgpu::ErrorType errorType,
                                     wgpu::StringView message) {
        std::cout << "Error: " << errorType << " - message: " << message << "\n";
  });

  wgpu::Future f2 = adapter.RequestDevice(
      &desc, wgpu::CallbackMode::WaitAnyOnly,
      [](wgpu::RequestDeviceStatus status, wgpu::Device d,
        wgpu::StringView message) {
        if (status != wgpu::RequestDeviceStatus::Success) {
            std::cout << "RequestDevice: " << message << "\n";
            exit(0);
        }
        device = std::move(d);
      });
    wgpu::WaitStatus waitStatus2 = instance.WaitAny(f2, UINT64_MAX);
}

std::string LoadShaderFromFile(const std::string& path) {
    std::string pathStr = isDev ? "../shaders/" + path : path;
    std::ifstream file(pathStr);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + pathStr);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


void CreateRenderPipeline() {
    vertexCode = LoadShaderFromFile("main.vertex.wgsl");
    wgpu::ShaderSourceWGSL vert{{.code = vertexCode.c_str()}};
    fragmentCode = LoadShaderFromFile("main.fragment.wgsl");
    wgpu::ShaderSourceWGSL frag{{.code = fragmentCode.c_str()}};

    const wgpu::ShaderModuleDescriptor vertDescriptor{.nextInChain = &vert, .label = "Vertex Shader"};
    wgpu::ShaderModule vertModule = device.CreateShaderModule(&vertDescriptor);

    const wgpu::ShaderModuleDescriptor fragDescriptor{.nextInChain = &frag, .label = "Fragment Shader"};
    wgpu::ShaderModule fragModule = device.CreateShaderModule(&fragDescriptor);

    wgpu::ColorTargetState colorTargetState{.format = format};

    wgpu::FragmentState fragmentState{.module = fragModule, .targetCount = 1, .targets = &colorTargetState};


    wgpu::RenderPipelineDescriptor descriptor{
        .vertex = {.module = vertModule},
        .fragment = &fragmentState};
    pipeline = device.CreateRenderPipeline(&descriptor);
}

void Render() {
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);


    wgpu::RenderPassColorAttachment attachment{
        .view = surfaceTexture.texture.CreateView(),
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store};

    wgpu::RenderPassDescriptor renderpass{.colorAttachmentCount = 1,
                                          .colorAttachments = &attachment};

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderpass);
    pass.SetPipeline(pipeline);
    pass.Draw(3);
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);
}

void InitGraphics() {
    ConfigureSurface();
    CreateRenderPipeline();
}

void Start() {
    if (!glfwInit()) {
        return;
  }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(kWidth, kHeight, "WebGPU window", nullptr, nullptr);
    surface = wgpu::glfw::CreateSurfaceForWindow(instance, window);

    InitGraphics();

#if defined(__EMSCRIPTEN__)
  emscripten_set_main_loop(Render, 0, false);
#else
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      Render();
      surface.Present();
      instance.ProcessEvents();
    }
#endif
}

int main() {
    Init();
    Start();
}
