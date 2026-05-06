#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

#ifndef UTILS_H
#define UTILS_H
std::vector<const char*> getRequiredExtensions();
std::vector<const char*> getRequiredLayers();
std::optional<std::tuple<vk::QueueFamilyProperties, uint32_t>>
getGraphicsQueueFamilyProperties(const vk::raii::PhysicalDevice& device,
                                 const vk::raii::SurfaceKHR& surface);
bool isPhysicalDeviceSuitable(const vk::raii::PhysicalDevice& device,
                              const vk::raii::SurfaceKHR& surface);
vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const vk::raii::PhysicalDevice& device, const vk::raii::SurfaceKHR& surface);
vk::PresentModeKHR chooseSwapPresentMode(const vk::raii::PhysicalDevice& device, const vk::raii::SurfaceKHR& surface);
vk::Extent2D chooseSwapExtent(const vk::raii::PhysicalDevice& device,
                              const vk::raii::SurfaceKHR& surface,
                              GLFWwindow*& window);
std::vector<char> readFile(const std::string& filename);
uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities);
#endif