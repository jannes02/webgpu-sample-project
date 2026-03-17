#pragma once

#include <dawn/webgpu_cpp.h>

#include <filesystem>

class ResourceManager {
public:
    using path = std::filesystem::path;

    // Load a shader from a WGSL file into a new shader module
    static wgpu::ShaderModule loadShaderModule(const path& path, wgpu::Device device);
};