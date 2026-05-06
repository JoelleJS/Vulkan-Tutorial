#include "utils.h"
#include "const.h"
#include "vulkan/vulkan.hpp"
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#define GLFW_INCLUDE_VULKAN // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <tuple>

std::vector<const char*> getRequiredExtensions() {
    uint32_t glfwRequiredExtensionCount = 0;
    const auto glfwRequiredExtensions =
        glfwGetRequiredInstanceExtensions(&glfwRequiredExtensionCount);
    return std::vector(glfwRequiredExtensions,
                       glfwRequiredExtensions + glfwRequiredExtensionCount);
}

std::vector<const char*> getRequiredLayers() {
    std::vector<const char*> requiredLayers;
    if (enableValidationLayers) {
        std::cout << "Validation layers enabled" << std::endl;
        requiredLayers.assign(validationLayers.begin(), validationLayers.end());
    }
    return requiredLayers;
}

bool supportsVulkanVersion(const vk::raii::PhysicalDevice& device,
                           const uint32_t version) {
    return device.getProperties().apiVersion >= version;
}

std::optional<std::tuple<vk::QueueFamilyProperties, uint32_t>>
getGraphicsQueueFamilyProperties(const vk::raii::PhysicalDevice& device,
                                 const vk::raii::SurfaceKHR& surface) {
    const std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
        if ((queueFamilyProperties[i].queueFlags &
             vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0) &&
            device.getSurfaceSupportKHR(i, surface)) {
            return std::make_tuple(queueFamilyProperties[i], i);
        }
    }
    return {};
}

bool supportsGraphicsQueue(const vk::raii::PhysicalDevice& device,
                           const vk::raii::SurfaceKHR& surface) {
    return getGraphicsQueueFamilyProperties(device, surface).has_value();
}

bool supportsRequiredExtensions(const vk::raii::PhysicalDevice& device) {
    const auto availableDeviceExtensions =
        device.enumerateDeviceExtensionProperties();
    return std::ranges::all_of(
        requiredDeviceExtensions,
        [&availableDeviceExtensions](
            const char* const& requiredDeviceExtension) {
            return std::ranges::any_of(
                availableDeviceExtensions,
                [&requiredDeviceExtension](
                    const vk::ExtensionProperties& availableExtension) {
                    return strcmp(availableExtension.extensionName,
                                  requiredDeviceExtension) == 0;
                });
        });
}

bool supportsRequiredFeatures(const vk::raii::PhysicalDevice& device) {
    const auto features = device.getFeatures2<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    return features.get<vk::PhysicalDeviceVulkan11Features>()
               .shaderDrawParameters &&
           features.get<vk::PhysicalDeviceVulkan13Features>()
               .synchronization2 &&
           features.get<vk::PhysicalDeviceVulkan13Features>()
               .dynamicRendering &&
           features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
               .extendedDynamicState;
}

bool isPhysicalDeviceSuitable(const vk::raii::PhysicalDevice& device,
                              const vk::raii::SurfaceKHR& surface) {
    return supportsVulkanVersion(device, vk::ApiVersion13) &&
           supportsGraphicsQueue(device, surface) &&
           supportsRequiredExtensions(device) &&
           supportsRequiredFeatures(device);
}

vk::SurfaceFormatKHR
chooseSwapSurfaceFormat(const vk::raii::PhysicalDevice& device,
                        const vk::raii::SurfaceKHR& surface) {
    const std::vector<vk::SurfaceFormatKHR> availableFormats =
        device.getSurfaceFormatsKHR(surface);
    const auto formatIt =
        std::ranges::find_if(availableFormats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(const vk::raii::PhysicalDevice& device,
                                         const vk::raii::SurfaceKHR& surface) {
    const std::vector<vk::PresentModeKHR> availablePresentModes =
        device.getSurfacePresentModesKHR(surface);
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
        return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                   return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(const vk::raii::PhysicalDevice& device,
                              const vk::raii::SurfaceKHR& surface,
                              GLFWwindow*& window) {
    const vk::SurfaceCapabilitiesKHR capabilities =
        device.getSurfaceCapabilitiesKHR(surface);
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
}

uint32_t
chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) &&
        (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    file.close();
    return buffer;
}
