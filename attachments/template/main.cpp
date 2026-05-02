#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "const.h"
#include "utils.h"

class HelloTriangleApplication {
  public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

  private:
    GLFWwindow* window = nullptr;

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue graphicsQueue = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::SurfaceFormatKHR swapChainSurfaceFormat = {};
    vk::Extent2D swapChainExtent = {};
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages = {};
    std::vector<vk::raii::ImageView> swapChainImageViews = {};

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createGraphicsPipeline();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanup() {
        glfwDestroyWindow(window);

        glfwTerminate();
    }

    void createInstance() {
        constexpr vk::ApplicationInfo appInfo{
            .pApplicationName = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion14};

        const auto requiredExtensions = getRequiredExtensions();
        const auto extensionProperties =
            context.enumerateInstanceExtensionProperties();
        const auto missingExtension = std::ranges::find_if(
            requiredExtensions,
            [&extensionProperties](const auto& requiredExtension) {
                return std::ranges::none_of(
                    extensionProperties,
                    [requiredExtension](const auto& extensionProperty) {
                        return strcmp(extensionProperty.extensionName,
                                      requiredExtension) == 0;
                    });
            });
        if (missingExtension != requiredExtensions.end()) {
            throw std::runtime_error("Required GLFW extension not supported: " +
                                     std::string(*missingExtension));
        }

        const auto requiredLayers = getRequiredLayers();
        const auto layerProperties = context.enumerateInstanceLayerProperties();
        const auto missingLayer = std::ranges::find_if(
            requiredLayers, [&layerProperties](auto const& requiredLayer) {
                return std::ranges::none_of(
                    layerProperties,
                    [requiredLayer](const auto& layerProperty) {
                        return strcmp(layerProperty.layerName, requiredLayer) ==
                               0;
                    });
            });
        if (missingLayer != requiredLayers.end()) {
            throw std::runtime_error("Required layer not supported: " +
                                     std::string(*missingLayer));
        };

        const vk::InstanceCreateInfo createInfo{
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = (uint32_t)requiredLayers.size(),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = (uint32_t)requiredExtensions.size(),
            .ppEnabledExtensionNames = requiredExtensions.data(),
        };

        instance = vk::raii::Instance(context, createInfo);
    }

    void createSurface() {
        VkSurfaceKHR _surface;
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) !=
            0) {
            throw std::runtime_error("failed to create window surface!");
        }
        surface = vk::raii::SurfaceKHR(instance, _surface);
    }

    void pickPhysicalDevice() {
        const std::vector<vk::raii::PhysicalDevice> physicalDevices =
            instance.enumeratePhysicalDevices();
        const auto physicalDeviceIt = std::ranges::find_if(
            physicalDevices, [&](const vk::raii::PhysicalDevice& _device) {
                return isPhysicalDeviceSuitable(_device, surface);
            });
        if (physicalDeviceIt == physicalDevices.end()) {
            throw std::runtime_error(
                "failed to find GPUs with Vulkan support!");
        }
        physicalDevice = *physicalDeviceIt;
        std::cout << physicalDevice.getProperties().deviceName << std::endl;
    }

    void createLogicalDevice() {
        const uint32_t graphicsIndex = std::get<1>(
            getGraphicsQueueFamilyProperties(physicalDevice, surface).value());
        const float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = graphicsIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

        const vk::StructureChain<
            vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain{
                {}, // vk::PhysicalDeviceFeatures2 (empty for now)
                {.shaderDrawParameters = true},
                {.dynamicRendering =
                     true}, // Enable dynamic rendering from Vulkan 1.3
                {.extendedDynamicState =
                     true} // Enable extended dynamic state from the extension
            };
        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount =
                static_cast<uint32_t>(requiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data()};

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
    }

    void createSwapchain() {
        swapChainSurfaceFormat =
            chooseSwapSurfaceFormat(physicalDevice, surface);
        const vk::PresentModeKHR presentMode =
            chooseSwapPresentMode(physicalDevice, surface);
        swapChainExtent = chooseSwapExtent(physicalDevice, surface, window);
        const vk::SurfaceCapabilitiesKHR surfaceCapabilities =
            physicalDevice.getSurfaceCapabilitiesKHR(surface);
        const uint32_t imageCount = surfaceCapabilities.minImageCount + 1;

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{
            .surface = *surface,
            .minImageCount = imageCount,
            .imageFormat = swapChainSurfaceFormat.format,
            .imageColorSpace = swapChainSurfaceFormat.colorSpace,
            .imageExtent = swapChainExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = presentMode,
            .clipped = true};

        swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
        swapChainImages = swapChain.getImages();
    }

    void createImageViews() {
        assert(swapChainImageViews.empty());
        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType = vk::ImageViewType::e2D,
            .format = swapChainSurfaceFormat.format,
            .components = {vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity},
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        for (auto& image : swapChainImages) {
            imageViewCreateInfo.image = image;
            swapChainImageViews.emplace_back(device, imageViewCreateInfo);
        }
    }

    [[nodiscard]] vk::raii::ShaderModule
    createShaderModule(const std::vector<char>& code) {
        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())};
        vk::raii::ShaderModule shaderModule{device, createInfo};
        return shaderModule;
    }

    void createGraphicsPipeline() {
        auto shaderCode = readFile("build/VulkanTutorial/shaders/slang.spv");
        vk::raii::ShaderModule shaderModule = createShaderModule(shaderCode);
        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shaderModule,
            .pName = "vertMain"};
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shaderModule,
            .pName = "fragMain"};
        vk::PipelineShaderStageCreateInfo shaderStages[] = {
            vertShaderStageInfo, fragShaderStageInfo};
    }
};

int main() {
    try {
        HelloTriangleApplication app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
