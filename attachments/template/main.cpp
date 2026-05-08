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
    uint32_t queueIndex = ~0;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::SurfaceFormatKHR swapChainSurfaceFormat = {};
    vk::Extent2D swapChainExtent = {};
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages = {};
    std::vector<vk::raii::ImageView> swapChainImageViews = {};
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;
    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    uint32_t frameIndex = 0;
    bool framebufferResized = false;

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width,
                                          int height) {
        auto app = reinterpret_cast<HelloTriangleApplication*>(
            glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createGraphicsPipeline();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    void cleanupSwapchain() {
        swapChainImageViews.clear();
        swapChain = nullptr;
    }

    void recreateSwapchain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        device.waitIdle();

        cleanupSwapchain();

        createSwapchain();
        createImageViews();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }
        device.waitIdle();
    }

    void cleanup() {
        cleanupSwapchain();

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
        queueIndex = std::get<1>(
            getGraphicsQueueFamilyProperties(physicalDevice, surface).value());
        const float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = queueIndex,
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
                {.synchronization2 = true,
                 .dynamicRendering =
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
        graphicsQueue = vk::raii::Queue(device, queueIndex, 0);
    }

    void createSwapchain() {
        swapChainSurfaceFormat =
            chooseSwapSurfaceFormat(physicalDevice, surface);
        const vk::PresentModeKHR presentMode =
            chooseSwapPresentMode(physicalDevice, surface);
        swapChainExtent = chooseSwapExtent(physicalDevice, surface, window);
        const vk::SurfaceCapabilitiesKHR surfaceCapabilities =
            physicalDevice.getSurfaceCapabilitiesKHR(surface);
        const uint32_t minImageCount =
            chooseSwapMinImageCount(surfaceCapabilities);

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{
            .surface = *surface,
            .minImageCount = minImageCount,
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

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
            .topology = vk::PrimitiveTopology::eTriangleList};

        vk::Viewport viewport{0.0f,
                              0.0f,
                              static_cast<float>(swapChainExtent.width),
                              static_cast<float>(swapChainExtent.height),
                              0.0f,
                              1.0f};

        vk::Rect2D scissor{vk::Offset2D{0, 0}, swapChainExtent};

        vk::PipelineViewportStateCreateInfo viewportState{
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor};

        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f};

        vk::PipelineMultisampleStateCreateInfo multisampling{
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = vk::False};

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR |
                              vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB |
                              vk::ColorComponentFlagBits::eA};

        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment};

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = 0, .pushConstantRangeCount = 0};
        pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format};

        vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                           vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain = {{.stageCount = 2,
                                        .pStages = shaderStages,
                                        .pVertexInputState = &vertexInputInfo,
                                        .pInputAssemblyState = &inputAssembly,
                                        .pViewportState = &viewportState,
                                        .pRasterizationState = &rasterizer,
                                        .pMultisampleState = &multisampling,
                                        .pColorBlendState = &colorBlending,
                                        .pDynamicState = &dynamicState,
                                        .layout = pipelineLayout,
                                        .renderPass = nullptr},
                                       pipelineRenderingCreateInfo};

        graphicsPipeline = vk::raii::Pipeline(
            device, nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    }

    void createCommandPool() {
        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = queueIndex};
        commandPool = vk::raii::CommandPool(device, poolInfo);
    }

    void createCommandBuffers() {
        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT};

        commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
    }

    void createSyncObjects() {
        assert(presentCompleteSemaphores.empty() &&
               renderFinishedSemaphores.empty() && inFlightFences.empty());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            renderFinishedSemaphores.emplace_back(device,
                                                  vk::SemaphoreCreateInfo());
        }
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            presentCompleteSemaphores.emplace_back(device,
                                                   vk::SemaphoreCreateInfo());
            inFlightFences.emplace_back(
                device, vk::FenceCreateInfo{
                            .flags = vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void drawFrame() {
        vk::raii::Fence& drawFence = inFlightFences[frameIndex];
        vk::raii::Semaphore& presentCompleteSemaphore =
            presentCompleteSemaphores[frameIndex];
        vk::raii::CommandBuffer& commandBuffer = commandBuffers[frameIndex];

        vk::Result fenceResult =
            device.waitForFences(*drawFence, vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw std::runtime_error("failed to wait for fence!");
        }
        auto [result, imageIndex] = swapChain.acquireNextImage(
            UINT64_MAX, *presentCompleteSemaphore, nullptr);
        if (result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapchain();
            return;
        }
        if (result != vk::Result::eSuccess &&
            result != vk::Result::eSuboptimalKHR) {
            assert(result == vk::Result::eTimeout ||
                   result == vk::Result::eNotReady);
            throw std::runtime_error("failed to acquire swap chain image!");
        }
        device.resetFences(*drawFence);

        commandBuffer.reset();
        recordCommandBuffer(commandBuffer, imageIndex);

        vk::PipelineStageFlags waitDestinationStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::raii::Semaphore& renderFinishedSemaphore =
            renderFinishedSemaphores[imageIndex];
        const vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*presentCompleteSemaphore,
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSemaphore};
        graphicsQueue.submit(submitInfo, *drawFence);

        const vk::PresentInfoKHR presentInfoKHR{.waitSemaphoreCount = 1,
                                                .pWaitSemaphores =
                                                    &*renderFinishedSemaphore,
                                                .swapchainCount = 1,
                                                .pSwapchains = &*swapChain,
                                                .pImageIndices = &imageIndex};
        result = graphicsQueue.presentKHR(presentInfoKHR);
        if ((result == vk::Result::eSuboptimalKHR) ||
            (result == vk::Result::eErrorOutOfDateKHR) || framebufferResized) {
            framebufferResized = false;
            recreateSwapchain();
        } else {
            // There are no other success codes than eSuccess; on any error
            // code, presentKHR already threw an exception.
            assert(result == vk::Result::eSuccess);
        }

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void recordCommandBuffer(vk::raii::CommandBuffer& commandBuffer,
                             uint32_t imageIndex) {
        commandBuffer.begin({});
        transition_image_layout(
            commandBuffer, imageIndex, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {}, // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite,         // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput  // dstStage
        );
        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {
            .imageView = swapChainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor};
        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentInfo};
        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   *graphicsPipeline);
        commandBuffer.setViewport(
            0, vk::Viewport(
                   0.0f, 0.0f, static_cast<float>(swapChainExtent.width),
                   static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
        commandBuffer.setScissor(
            0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
        commandBuffer.draw(3, 1, 0, 0);
        commandBuffer.endRendering();
        transition_image_layout(
            commandBuffer, imageIndex, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,         // srcAccessMask
            {},                                                 // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe           // dstStage
        );
        commandBuffer.end();
    }

    void transition_image_layout(vk::raii::CommandBuffer& commandBuffer,
                                 uint32_t imageIndex,
                                 vk::ImageLayout old_layout,
                                 vk::ImageLayout new_layout,
                                 vk::AccessFlags2 src_access_mask,
                                 vk::AccessFlags2 dst_access_mask,
                                 vk::PipelineStageFlags2 src_stage_mask,
                                 vk::PipelineStageFlags2 dst_stage_mask) {
        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapChainImages[imageIndex],
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};
        vk::DependencyInfo dependencyInfo = {.dependencyFlags = {},
                                             .imageMemoryBarrierCount = 1,
                                             .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(dependencyInfo);
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
