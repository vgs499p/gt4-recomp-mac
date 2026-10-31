#pragma once

// Vulkan extension selection for the native GS backend (TASK-081, release blocker for 1.0.0).
//
// The game needs only VK_KHR_surface and VK_KHR_win32_surface (instance) and VK_KHR_swapchain (device) on a Vulkan 1.2
// GPU. Swapchain maintenance 1 (present fences) is used when the driver has it, in its KHR form (2025+ drivers) or its
// older EXT form (2023+ drivers), with its instance prerequisites (get_surface_capabilities2 and surface_maintenance1
// of the same family). Without it the backend waits on the GS timeline for a slot and uses one present semaphore per
// swapchain image (the plain VK_KHR_swapchain path). These functions are pure: lists in, choices out.

#include <string>
#include <vector>

namespace gt4::vulkan {

// Which swapchain-maintenance family the instance enabled.
enum class MaintenanceFamily { none, ext, khr };

const char *maintenance_family_name(MaintenanceFamily family);

struct InstanceChoice {
  std::vector<std::string> extensions; // in the order they are passed to vkCreateInstance
  MaintenanceFamily family = MaintenanceFamily::none;
};

struct DeviceChoice {
  std::vector<std::string> extensions; // in the order they are passed to vkCreateDevice
  bool present_fences = false;         // swapchain maintenance 1 enabled (VkSwapchainPresentFenceInfo)
  const char *path = "baseline";       // "khr-maintenance1", "ext-maintenance1" or "baseline"
};

// GT4_VULKAN_FORCE_BASELINE: unset, empty or "0" = automatic; "1" = always the baseline path. Anything else throws
// std::invalid_argument.
bool parse_force_baseline(const char *value);

// The window-system surface extension of this platform: VK_KHR_win32_surface on Windows, VK_EXT_metal_surface on
// macOS (MoltenVK, a CAMetalLayer of the game's SDL window).
#ifdef _WIN32
inline constexpr const char *kPlatformSurfaceExtension = "VK_KHR_win32_surface";
#else
inline constexpr const char *kPlatformSurfaceExtension = "VK_EXT_metal_surface";
#endif

// Throws std::runtime_error naming a missing required instance extension (VK_KHR_surface, kPlatformSurfaceExtension).
InstanceChoice select_instance_extensions(const std::vector<std::string> &available, bool force_baseline);

// `swapchain_maintenance_feature` is VkPhysicalDeviceSwapchainMaintenance1Features::swapchainMaintenance1 as queried
// for the device (false when it could not be queried). Throws std::runtime_error when VK_KHR_swapchain is missing.
DeviceChoice select_device_extensions(const std::vector<std::string> &available, MaintenanceFamily instance_family,
                                      bool swapchain_maintenance_feature, bool force_baseline);

// The device extension name of a family ("VK_KHR_swapchain_maintenance1", "VK_EXT_swapchain_maintenance1"), or
// nullptr for none.
const char *swapchain_maintenance_extension(MaintenanceFamily family);

} // namespace gt4::vulkan
