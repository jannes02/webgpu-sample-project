#include "ResourceManager.h"

#include <fstream>
#include <iostream>

using namespace wgpu;

ShaderModule ResourceManager::loadShaderModule(const path& path, Device device) {
	std::ifstream file(path);
	if (!file.is_open()) {
	    std::cerr << "Unable to open file: " << path << std::endl;
		return nullptr;
	}
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	std::string shaderSource(size, ' ');
	file.seekg(0);
	file.read(shaderSource.data(), size);

    ShaderSourceWGSL shaderSourceObject{{.code = shaderSource.c_str()}};
    const ShaderModuleDescriptor shaderModuleDescriptor{
        .nextInChain = &shaderSourceObject,
    };

    ShaderModule shaderModule = device.CreateShaderModule(&shaderModuleDescriptor);


	return shaderModule;
}