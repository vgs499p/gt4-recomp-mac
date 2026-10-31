#include "native_vulkan.hpp"
#include "perf_probe.hpp"

#ifndef _WIN32
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_metal.h>
#include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "install_paths.hpp"
#include "pacing.hpp"
#include "vulkan_extensions.hpp"

namespace gt4::vulkan {
namespace {
VkPresentModeKHR vulkan_present_mode(gt4::pacing::PresentMode mode) {
  switch (mode) {
  case gt4::pacing::PresentMode::fifo: return VK_PRESENT_MODE_FIFO_KHR;
  case gt4::pacing::PresentMode::mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
  case gt4::pacing::PresentMode::immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}
} // namespace


PresentationRect presentation_rect(std::uint32_t width, std::uint32_t height,
                                   Aspect aspect) {
  if (!width || !height)
    return {0, 0, 0, 0};
  const std::uint64_t numerator = aspect == Aspect::widescreen ? 16 : 4;
  const std::uint64_t denominator = aspect == Aspect::widescreen ? 9 : 3;
  const auto fitted_width = static_cast<std::uint32_t>(std::max<std::uint64_t>(
      1, std::min<std::uint64_t>(width, height * numerator / denominator)));
  const auto fitted_height = static_cast<std::uint32_t>(std::max<std::uint64_t>(
      1, std::min<std::uint64_t>(height, width * denominator / numerator)));
  return {(width - fitted_width) / 2, (height - fitted_height) / 2,
          fitted_width, fitted_height};
}

PresentationRect presentation_rect_ratio(std::uint32_t width, std::uint32_t height,
                                         double ratio) {
  if (!width || !height || !(ratio > 0.0))
    return {0, 0, 0, 0};
  const double fitted_by_height = static_cast<double>(height) * ratio;
  std::uint32_t fitted_width = width, fitted_height = height;
  if (fitted_by_height <= static_cast<double>(width))
    fitted_width = static_cast<std::uint32_t>(std::max(1.0, std::floor(fitted_by_height + 0.5)));
  else
    fitted_height = static_cast<std::uint32_t>(
        std::max(1.0, std::floor(static_cast<double>(width) / ratio + 0.5)));
  fitted_width = std::min(fitted_width, width);
  fitted_height = std::min(fitted_height, height);
  return {(width - fitted_width) / 2, (height - fitted_height) / 2,
          fitted_width, fitted_height};
}

namespace {
// TASK-080: race view presentation (set_race_view_presentation). The ratio is stored as its bit pattern.
std::atomic<bool> g_race_view_fill{false};
std::atomic<std::uint64_t> g_race_view_ratio_bits{0};
} // namespace

void set_race_view_presentation(bool fill, double ratio) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &ratio, sizeof bits);
  g_race_view_ratio_bits.store(bits);
  g_race_view_fill.store(fill);
}

namespace {

// Only the Vulkan entry points used by this backend are resolved from the
// loader.
// clang-format off
#define VULKAN_FUNCTIONS(X) \
  X(vkCreateInstance) \
  X(vkEnumeratePhysicalDevices) \
  X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceMemoryProperties) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkCreateDevice) \
  X(vkCreateBuffer) \
  X(vkGetBufferMemoryRequirements) \
  X(vkAllocateMemory) \
  X(vkBindBufferMemory) \
  X(vkMapMemory) \
  X(vkDestroyBuffer) \
  X(vkFreeMemory) \
  X(vkCreateCommandPool) \
  X(vkAllocateCommandBuffers) \
  X(vkResetCommandBuffer) \
  X(vkEndCommandBuffer) \
  X(vkQueueSubmit) \
  X(vkQueueWaitIdle) \
  X(vkDeviceWaitIdle) \
  X(vkEnumerateDeviceExtensionProperties) \
  X(vkCreateFence) \
  X(vkResetFences) \
  X(vkCreateDescriptorSetLayout) \
  X(vkCreatePipelineLayout) \
  X(vkCreateDescriptorPool) \
  X(vkUpdateDescriptorSets) \
  X(vkCreateShaderModule) \
  X(vkDestroyShaderModule) \
  X(vkCmdBindPipeline) \
  X(vkCmdBindDescriptorSets) \
  X(vkCmdDispatch) \
  PLATFORM_SURFACE_FUNCTION(X) \
  X(vkGetPhysicalDeviceSurfaceSupportKHR) \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
  X(vkCreateSwapchainKHR) \
  X(vkGetSwapchainImagesKHR) \
  X(vkQueuePresentKHR) \
  X(vkCreateImage) \
  X(vkBindImageMemory) \
  X(vkCmdCopyBufferToImage) \
  X(vkCmdBlitImage) \
  X(vkCmdClearColorImage) \
  X(vkGetPhysicalDeviceFeatures2) \
  X(vkDestroyFence) \
  X(vkDestroySemaphore) \
  X(vkDestroyPipeline) \
  X(vkDestroyDescriptorPool) \
  X(vkDestroyPipelineLayout) \
  X(vkDestroyDescriptorSetLayout) \
  X(vkDestroyCommandPool) \
  X(vkDestroySurfaceKHR) \
  X(vkDestroyDevice) \
  X(vkDestroyInstance) \
  X(vkUnmapMemory) \
  X(vkDestroySwapchainKHR) \
  X(vkGetPhysicalDeviceFeatures) \
  X(vkGetDeviceQueue) \
  X(vkAllocateDescriptorSets) \
  X(vkCreateComputePipelines) \
  X(vkBeginCommandBuffer) \
  X(vkWaitForFences) \
  X(vkCmdPipelineBarrier) \
  X(vkCreateSemaphore) \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  X(vkGetPhysicalDeviceFormatProperties) \
  X(vkDestroyImage) \
  X(vkGetImageMemoryRequirements) \
  X(vkAcquireNextImageKHR) \
  X(vkWaitSemaphores) \
  X(vkGetSemaphoreCounterValue)
// clang-format on

#ifdef _WIN32
#define PLATFORM_SURFACE_FUNCTION(X) X(vkCreateWin32SurfaceKHR)
constexpr const char *kSurfaceName = "Win32";
#else
#define PLATFORM_SURFACE_FUNCTION(X) X(vkCreateMetalSurfaceEXT)
constexpr const char *kSurfaceName = "Metal";
#endif

#define DECLARE_FUNCTION(name) PFN_##name name = nullptr;
VULKAN_FUNCTIONS(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

void check(VkResult result, const char *operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) +
                             " failed: VkResult=" + std::to_string(result));
  }
}

std::uint32_t memory_type(const VkPhysicalDeviceMemoryProperties &properties,
                          std::uint32_t allowed,
                          VkMemoryPropertyFlags required) {
  for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((allowed & (1u << index)) &&
        (properties.memoryTypes[index].propertyFlags & required) == required)
      return index;
  }
  throw std::runtime_error("No Vulkan memory type satisfies requested flags");
}

} // namespace

Context::~Context() {
  if (queue_ && vkQueueWaitIdle)
    vkQueueWaitIdle(queue_);
  if (device_) {
    wait_all_presentations();
    for (auto *buffer : buffers_) {
      if (buffer->mapped)
        vkUnmapMemory(device_, buffer->memory);
      if (buffer->handle)
        vkDestroyBuffer(device_, buffer->handle, nullptr);
      if (buffer->memory)
        vkFreeMemory(device_, buffer->memory, nullptr);
      delete buffer;
    }
    if (!present_fences_ && vkDeviceWaitIdle)
      vkDeviceWaitIdle(device_);
    destroy_image();
    for (const VkSemaphore semaphore : image_rendered_)
      vkDestroySemaphore(device_, semaphore, nullptr);
    if (swapchain_)
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    for (auto &slot : slots_) {
      if (slot.acquired)
        vkDestroySemaphore(device_, slot.acquired, nullptr);
      if (slot.rendered)
        vkDestroySemaphore(device_, slot.rendered, nullptr);
      if (slot.present_fence)
        vkDestroyFence(device_, slot.present_fence, nullptr);
    }
    if (timeline_)
      vkDestroySemaphore(device_, timeline_, nullptr);
    for (auto pipeline : pipelines_)
      if (pipeline)
        vkDestroyPipeline(device_, pipeline, nullptr);
    if (descriptor_pool_)
      vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    if (pipeline_layout_)
      vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (descriptor_layout_)
      vkDestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
    if (command_pool_)
      vkDestroyCommandPool(device_, command_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (surface_)
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_ && vkDestroyInstance)
    vkDestroyInstance(instance_, nullptr);
#ifdef _WIN32
  if (loader_)
    FreeLibrary(loader_);
#else
  if (metal_view_)
    SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metal_view_));
  if (loader_)
    dlclose(loader_);
#endif
}

void Context::initialize(const std::filesystem::path &build_time_shaders) {
  // TASK-059: <executable folder>\shaders when installed, else the build-time folder (install_paths.cpp).
  const std::filesystem::path shaders = gt4::install::choose_shader_directory(build_time_shaders);
  std::fprintf(stderr, "[vulkan] shader folder: %s\n", shaders.string().c_str());
  {
    // TASK-081: a release build has no build-time fallback, so a missing installed folder stops here with a clear
    // message instead of a later per-file error.
    std::error_code folder_error;
    if (shaders.empty() || !std::filesystem::is_directory(shaders, folder_error))
      throw std::runtime_error("The game's shader folder is missing (" + shaders.string() +
                               "). Reinstall GT4Recomp.");
  }
  aspect_ = Aspect::widescreen;
  // Unknown names throw here; support on this surface is checked at swapchain creation.
  present_mode_ = gt4::pacing::parse_present_mode(std::getenv("GT4_PRESENT_MODE"));
  std::fprintf(stderr, "[vulkan] present mode request: %s\n",
               gt4::pacing::present_mode_name(present_mode_));
  if (const char *sync = std::getenv("GT4_GS_SYNC")) {
    if (std::strcmp(sync, "1") == 0)
      synchronous_ = true;
    else if (std::strcmp(sync, "0") != 0)
      throw std::runtime_error("GT4_GS_SYNC must be 0 or 1");
  }
#ifdef _WIN32
  loader_ =
      LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!loader_)
    throw std::runtime_error("Vulkan loader unavailable in System32");
  const auto get_instance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(loader_, "vkGetInstanceProcAddr"));
#else
  // The Vulkan loader bundled in the app (Contents/Frameworks), then the Homebrew and /usr/local installs; the loader
  // finds the MoltenVK driver through its ICD manifest.
  for (const char *candidate : {"@executable_path/../Frameworks/libvulkan.1.dylib", "libvulkan.1.dylib",
                                "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib"}) {
    loader_ = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
    if (loader_)
      break;
  }
  if (!loader_)
    throw std::runtime_error("Vulkan loader (libvulkan.1.dylib) unavailable; install vulkan-loader and molten-vk");
  const auto get_instance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      dlsym(loader_, "vkGetInstanceProcAddr"));
#endif
  if (!get_instance)
    throw std::runtime_error("Vulkan loader lacks vkGetInstanceProcAddr");
  vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
      get_instance(nullptr, "vkCreateInstance"));
  if (!vkCreateInstance)
    throw std::runtime_error("Vulkan loader lacks vkCreateInstance");
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "GT4 native Vulkan pilot";
  application.apiVersion = VK_API_VERSION_1_2;
  // TASK-081: only VK_KHR_surface and VK_KHR_win32_surface are required. The swapchain-maintenance prerequisites
  // (2023+ EXT, 2025+ KHR drivers) are enabled when present (vulkan_extensions.cpp); GT4_VULKAN_FORCE_BASELINE=1
  // forces the plain path.
  force_baseline_ =
      parse_force_baseline(std::getenv("GT4_VULKAN_FORCE_BASELINE"));
  const auto enumerate_instance_extensions =
      reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          get_instance(nullptr, "vkEnumerateInstanceExtensionProperties"));
  if (!enumerate_instance_extensions)
    throw std::runtime_error(
        "Vulkan loader lacks vkEnumerateInstanceExtensionProperties");
  std::vector<std::string> available_instance;
  {
    std::uint32_t extension_count = 0;
    check(enumerate_instance_extensions(nullptr, &extension_count, nullptr),
          "enumerate instance extensions");
    std::vector<VkExtensionProperties> listed(extension_count);
    check(enumerate_instance_extensions(nullptr, &extension_count,
                                        listed.data()),
          "enumerate instance extensions");
    for (std::uint32_t index = 0; index < extension_count; ++index)
      available_instance.emplace_back(listed[index].extensionName);
  }
  const InstanceChoice instance_choice =
      select_instance_extensions(available_instance, force_baseline_);
  maintenance_family_ = instance_choice.family;
  std::vector<const char *> extensions;
  for (const auto &name : instance_choice.extensions)
    extensions.push_back(name.c_str());
  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifndef _WIN32
  // MoltenVK is a portability driver: the loader lists it only with portability enumeration.
  if (std::find(available_instance.begin(), available_instance.end(),
                VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) != available_instance.end()) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
#endif
  instance_info.pApplicationInfo = &application;
  instance_info.enabledExtensionCount =
      static_cast<std::uint32_t>(extensions.size());
  instance_info.ppEnabledExtensionNames = extensions.data();
  check(vkCreateInstance(&instance_info, nullptr, &instance_),
        "vkCreateInstance");
#define LOAD_FUNCTION(name)                                                    \
  name = reinterpret_cast<PFN_##name>(get_instance(instance_, #name));
  VULKAN_FUNCTIONS(LOAD_FUNCTION)
#undef LOAD_FUNCTION
  std::uint32_t count = 0;
  check(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
        "enumerate GPUs");
  std::vector<VkPhysicalDevice> devices(count);
  check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
        "enumerate GPUs");
  for (const auto candidate : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    if (!physical_ ||
        properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      physical_ = candidate;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      break;
  }
  if (!physical_)
    throw std::runtime_error("No Vulkan GPU available");
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_, &properties);
  limits_ = properties.limits;
  if (properties.apiVersion < VK_API_VERSION_1_2 ||
      properties.limits.maxPerStageDescriptorStorageBuffers < 7 ||
      properties.limits.maxComputeWorkGroupInvocations < 256 ||
      256 % properties.limits.minUniformBufferOffsetAlignment != 0)
    throw std::runtime_error("GPU limits do not support the GS compute layout");
  std::fprintf(
      stderr,
      "[vulkan] device=%s api=%u.%u.%u; direct compute, scale=1\n",
      properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion),
      VK_VERSION_MINOR(properties.apiVersion),
      VK_VERSION_PATCH(properties.apiVersion));
  vkGetPhysicalDeviceMemoryProperties(physical_, &memory_properties_);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, families.data());
  bool found = false;
  for (std::uint32_t index = 0; index < count; ++index) {
    if ((families[index].queueFlags &
         (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
      queue_family_ = index;
      found = true;
      break;
    }
  }
  if (!found)
    throw std::runtime_error("No Vulkan graphics/compute queue");
  std::fprintf(stderr, "[vulkan] queue family %u: graphics=%d compute=%d\n",
               queue_family_,
               (families[queue_family_].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? 1 : 0,
               (families[queue_family_].queueFlags & VK_QUEUE_COMPUTE_BIT) ? 1 : 0);
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = queue_family_;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkPhysicalDeviceFeatures available{};
  vkGetPhysicalDeviceFeatures(physical_, &available);
  VkPhysicalDeviceFeatures features{};
  features.robustBufferAccess = available.robustBufferAccess;
  std::vector<std::string> available_device;
  {
    std::uint32_t extension_count = 0;
    check(vkEnumerateDeviceExtensionProperties(physical_, nullptr,
                                               &extension_count, nullptr),
          "enumerate device extensions");
    std::vector<VkExtensionProperties> listed(extension_count);
    check(vkEnumerateDeviceExtensionProperties(physical_, nullptr,
                                               &extension_count, listed.data()),
          "enumerate device extensions");
    for (std::uint32_t index = 0; index < extension_count; ++index)
      available_device.emplace_back(listed[index].extensionName);
  }
  // The KHR and EXT swapchain-maintenance structures share one layout and one
  // sType value; the feature is queried only when the device offers the
  // extension of the instance's family.
  const char *maintenance_name =
      swapchain_maintenance_extension(maintenance_family_);
  const bool maintenance_offered =
      maintenance_name &&
      std::find(available_device.begin(), available_device.end(),
                maintenance_name) != available_device.end();
  VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{};
  maintenance.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{};
  timeline_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceFeatures2 queried{};
  queried.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  queried.pNext = &timeline_features;
  if (maintenance_offered && !force_baseline_) {
    maintenance.pNext = &timeline_features;
    queried.pNext = &maintenance;
  }
  vkGetPhysicalDeviceFeatures2(physical_, &queried);
  if (!timeline_features.timelineSemaphore)
    throw std::runtime_error("Vulkan pilot requires timeline semaphores");
  const DeviceChoice device_choice = select_device_extensions(
      available_device, maintenance_family_,
      maintenance_offered && maintenance.swapchainMaintenance1 == VK_TRUE,
      force_baseline_);
  present_fences_ = device_choice.present_fences;
  std::vector<const char *> device_extensions;
  for (const auto &name : device_choice.extensions)
    device_extensions.push_back(name.c_str());
#ifndef _WIN32
  // A portability-subset device (MoltenVK) must enable the extension.
  if (std::find(available_device.begin(), available_device.end(), "VK_KHR_portability_subset") !=
      available_device.end())
    device_extensions.push_back("VK_KHR_portability_subset");
#endif
  // Enable exactly the features that were queried: timeline semaphores (core
  // 1.2) and, on the fast path, swapchainMaintenance1.
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_enable{};
  timeline_enable.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timeline_enable.timelineSemaphore = VK_TRUE;
  VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance_enable{};
  maintenance_enable.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
  maintenance_enable.swapchainMaintenance1 = VK_TRUE;
  maintenance_enable.pNext = &timeline_enable;
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &features;
  device_info.enabledExtensionCount =
      static_cast<std::uint32_t>(device_extensions.size());
  device_info.pNext = present_fences_
                          ? static_cast<const void *>(&maintenance_enable)
                          : static_cast<const void *>(&timeline_enable);
  device_info.ppEnabledExtensionNames = device_extensions.data();
  std::fprintf(stderr,
               "[vulkan] presentation path: %s (instance family %s, "
               "present fences %s%s)\n",
               device_choice.path,
               maintenance_family_name(maintenance_family_),
               present_fences_ ? "on" : "off",
               force_baseline_ ? ", forced by GT4_VULKAN_FORCE_BASELINE=1" : "");
  check(vkCreateDevice(physical_, &device_info, nullptr, &device_),
        "vkCreateDevice");
#define REQUIRE_FUNCTION(name)                                                 \
  if (!name)                                                                   \
    throw std::runtime_error("Vulkan entry point unavailable: " #name);
  VULKAN_FUNCTIONS(REQUIRE_FUNCTION)
#undef REQUIRE_FUNCTION
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.queueFamilyIndex = queue_family_;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
        "command pool");
  VkSemaphoreTypeCreateInfo timeline_type{};
  timeline_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  timeline_type.initialValue = 0;
  VkSemaphoreCreateInfo timeline_info{};
  timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  timeline_info.pNext = &timeline_type;
  check(vkCreateSemaphore(device_, &timeline_info, nullptr, &timeline_),
        "GS timeline semaphore");
  std::array<VkCommandBuffer, kSlotCount> command_buffers{};
  VkCommandBufferAllocateInfo commands{};
  commands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commands.commandPool = command_pool_;
  commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commands.commandBufferCount = kSlotCount;
  check(vkAllocateCommandBuffers(device_, &commands, command_buffers.data()),
        "command buffers");
  VkSemaphoreCreateInfo semaphore{};
  semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  for (std::uint32_t index = 0; index < kSlotCount; ++index) {
    slots_[index].command = command_buffers[index];
    check(vkCreateSemaphore(device_, &semaphore, nullptr,
                            &slots_[index].acquired),
          "acquire semaphore");
    check(vkCreateSemaphore(device_, &semaphore, nullptr,
                            &slots_[index].rendered),
          "present semaphore");
    check(vkCreateFence(device_, &fence_info, nullptr,
                        &slots_[index].present_fence),
          "presentation fence");
  }
  std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
  for (std::uint32_t index = 0; index < bindings.size(); ++index) {
    bindings[index].binding = index;
    bindings[index].descriptorCount = 1;
    bindings[index].descriptorType =
        index == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                   : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
  layout_info.pBindings = bindings.data();
  check(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr,
                                    &descriptor_layout_),
        "descriptor layout");
  VkPipelineLayoutCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_info.setLayoutCount = 1;
  pipeline_info.pSetLayouts = &descriptor_layout_;
  check(vkCreatePipelineLayout(device_, &pipeline_info, nullptr,
                               &pipeline_layout_),
        "pipeline layout");
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7 * kSlotCount},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSlotCount}};
  VkDescriptorPoolCreateInfo descriptors{};
  descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptors.maxSets = kSlotCount;
  descriptors.poolSizeCount = 2;
  descriptors.pPoolSizes = sizes;
  check(
      vkCreateDescriptorPool(device_, &descriptors, nullptr, &descriptor_pool_),
      "descriptor pool");
  std::array<VkDescriptorSetLayout, kSlotCount> layouts{};
  layouts.fill(descriptor_layout_);
  std::array<VkDescriptorSet, kSlotCount> sets{};
  VkDescriptorSetAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocation.descriptorPool = descriptor_pool_;
  allocation.descriptorSetCount = kSlotCount;
  allocation.pSetLayouts = layouts.data();
  check(vkAllocateDescriptorSets(device_, &allocation, sets.data()),
        "descriptor sets");
  for (std::uint32_t index = 0; index < kSlotCount; ++index)
    slots_[index].descriptors = sets[index];
  const char *entries[] = {"upload",    "copy_read", "copy_write",
                           "load_clut", "display",   "raster",
                           "resolve",   "snapshot",  "decode"};
  for (std::size_t index = 0; index < pipelines_.size(); ++index) {
    std::ifstream input(shaders / (std::string(entries[index]) + ".spv"),
                        std::ios::binary | std::ios::ate);
    if (!input)
      throw std::runtime_error("Missing SPIR-V shader: " +
                               std::string(entries[index]));
    const auto bytes = static_cast<std::size_t>(input.tellg());
    if (bytes < 20 || bytes % 4)
      throw std::runtime_error("Invalid SPIR-V size");
    std::vector<std::uint32_t> words(bytes / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(words.data()), bytes) ||
        words[0] != 0x07230203u)
      throw std::runtime_error("Invalid SPIR-V content");
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = bytes;
    module_info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_, &module_info, nullptr, &module),
          "shader module");
    VkComputePipelineCreateInfo compute{};
    compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute.stage.module = module;
    compute.stage.pName = entries[index];
    compute.layout = pipeline_layout_;
    const auto result = vkCreateComputePipelines(
        device_, VK_NULL_HANDLE, 1, &compute, nullptr, &pipelines_[index]);
    vkDestroyShaderModule(device_, module, nullptr);
    check(result, entries[index]);
  }
  ring_ = std::make_unique<SubmitRing>(
      kSlotCount, [this] { return completed_submission(); },
      [this](std::uint64_t value) { wait_timeline(value); });
  for (auto &slot : slots_)
    ensure_slot_capacity(slot, 0, 64 * 1024);
  std::fprintf(stderr, "[vulkan] GS submissions %s, ring of %u slots\n",
               synchronous_ ? "synchronous (GT4_GS_SYNC=1)" : "asynchronous",
               kSlotCount);
}

Buffer *Context::create_buffer(std::uint64_t bytes) {
  if (!bytes)
    throw std::runtime_error("Cannot allocate zero-byte Vulkan buffer");
  auto *buffer = new Buffer;
  buffers_.push_back(buffer);
  buffer->size = bytes;
  VkBufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size = bytes;
  info.usage =
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  check(vkCreateBuffer(device_, &info, nullptr, &buffer->handle), "buffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, buffer->handle, &requirements);
  const auto host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  auto type =
      memory_type(memory_properties_, requirements.memoryTypeBits, host_flags);
  for (std::uint32_t index = 0; index < memory_properties_.memoryTypeCount;
       ++index) {
    if ((requirements.memoryTypeBits & (1u << index)) &&
        (memory_properties_.memoryTypes[index].propertyFlags &
         (host_flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ==
            (host_flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      type = index;
      break;
    }
  }
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = type;
  check(vkAllocateMemory(device_, &allocation, nullptr, &buffer->memory),
        "buffer memory");
  check(vkBindBufferMemory(device_, buffer->handle, buffer->memory, 0),
        "bind buffer");
  check(vkMapMemory(device_, buffer->memory, 0, bytes, 0, &buffer->mapped),
        "map coherent buffer");
  std::memset(buffer->mapped, 0, static_cast<std::size_t>(bytes));
  return buffer;
}

void Context::destroy_buffer(Buffer *buffer) {
  if (!buffer)
    return;
  wait_idle();
  if (buffer->mapped)
    vkUnmapMemory(device_, buffer->memory);
  vkDestroyBuffer(device_, buffer->handle, nullptr);
  vkFreeMemory(device_, buffer->memory, nullptr);
  std::erase(buffers_, buffer);
  delete buffer;
}

void Context::write(Buffer *buffer, std::uint64_t offset, const void *source,
                    std::uint64_t bytes) {
  GT4_PERF_SCOPE(gt4::perf::Category::gs_upload);
  if (!buffer || offset > buffer->size || bytes > buffer->size - offset)
    throw std::runtime_error("Vulkan buffer write outside allocation");
  if (bytes)
    std::memcpy(static_cast<std::uint8_t *>(buffer->mapped) + offset, source,
                static_cast<std::size_t>(bytes));
#if defined(GT4_PERF_PROBE) && !defined(GT4_PERF_FRAME_ONLY)
  gt4::perf::Probe::shared().bytes(false, bytes);
#endif
}

void Context::read(Buffer *buffer, std::uint64_t offset, void *destination,
                   std::uint64_t bytes) {
  GT4_PERF_SCOPE(gt4::perf::Category::gs_upload);
  if (!buffer || offset > buffer->size || bytes > buffer->size - offset)
    throw std::runtime_error("Vulkan buffer read outside allocation");
  wait_submissions();
  if (bytes)
    std::memcpy(destination,
                static_cast<std::uint8_t *>(buffer->mapped) + offset,
                static_cast<std::size_t>(bytes));
#if defined(GT4_PERF_PROBE) && !defined(GT4_PERF_FRAME_ONLY)
  gt4::perf::Probe::shared().bytes(true, bytes);
#endif
}

void Context::bind_buffers(const std::array<Buffer *, 8> &buffers) {
  for (std::uint32_t index = 0; index < buffers.size(); ++index) {
    // Indices 2 and 3 may be null: submit() never reads them.
    if (!buffers[index] && (index == 2 || index == 3)) {
      shared_[index] = nullptr;
      continue;
    }
    if (!buffers[index] || buffers[index]->size < 256 ||
        buffers[index]->size > limits_.maxStorageBufferRange)
      throw std::runtime_error("GS descriptor buffer exceeds Vulkan limits");
    shared_[index] = buffers[index];
  }
}

void Context::memory_barrier(VkCommandBuffer command,
                             VkPipelineStageFlags source,
                             VkPipelineStageFlags destination,
                             VkAccessFlags writes, VkAccessFlags reads) {
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = writes;
  barrier.dstAccessMask = reads;
  vkCmdPipelineBarrier(command, source, destination, 0, 1, &barrier, 0,
                       nullptr, 0, nullptr);
}

std::uint64_t Context::completed_submission() {
  std::uint64_t value = 0;
  check(vkGetSemaphoreCounterValue(device_, timeline_, &value),
        "read GS timeline");
  return value;
}

void Context::wait_timeline(std::uint64_t value) {
  GT4_PERF_SCOPE(gt4::perf::Category::gs_wait);
  VkSemaphoreWaitInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  info.semaphoreCount = 1;
  info.pSemaphores = &timeline_;
  info.pValues = &value;
  check(vkWaitSemaphores(device_, &info, UINT64_MAX), "wait GS timeline");
}

void Context::wait_submissions() { ring_->drain(); }

void Context::wait_presentation(Slot &slot) {
  if (!slot.presented)
    return;
  GT4_PERF_SCOPE(gt4::perf::Category::presentation_wait);
  if (present_fences_) {
    check(vkWaitForFences(device_, 1, &slot.present_fence, VK_TRUE,
                          UINT64_MAX),
          "wait presentation completion");
  } else {
    // Baseline (no swapchain maintenance 1): the slot's command buffer and
    // acquire semaphore are free once its submission finished on the GPU. The
    // present semaphore belongs to the swapchain image, which the presentation
    // engine hands back through vkAcquireNextImageKHR.
    wait_timeline(slot.presented_id);
  }
  slot.presented = false;
}

void Context::wait_all_presentations() {
  for (auto &slot : slots_)
    wait_presentation(slot);
}

void Context::ensure_slot_capacity(Slot &slot, std::uint64_t arena_bytes,
                                   std::uint64_t parameter_bytes) {
  constexpr std::uint64_t kChunk = 4ull << 20;
  const std::uint64_t data_bytes = std::max<std::uint64_t>(arena_bytes, 16);
  if (!slot.data || slot.data->size < data_bytes) {
    const std::uint64_t current = slot.data ? slot.data->size : 0;
    const std::uint64_t rounded = (data_bytes + kChunk - 1) & ~(kChunk - 1);
    const std::uint64_t grown = std::max<std::uint64_t>(current * 2, rounded);
    destroy_buffer(slot.data);
    slot.data = create_buffer(grown);
  }
  const std::uint64_t parameter_need =
      std::max<std::uint64_t>(parameter_bytes, 256);
  if (!slot.parameters || slot.parameters->size < parameter_need) {
    const std::uint64_t current = slot.parameters ? slot.parameters->size : 0;
    const std::uint64_t grown = std::max<std::uint64_t>(
        current * 2, std::max<std::uint64_t>(parameter_need, 64 * 1024));
    destroy_buffer(slot.parameters);
    slot.parameters = create_buffer(grown);
  }
}

void Context::update_descriptors(const Slot &slot, bool bound_buffers) {
  std::array<VkDescriptorBufferInfo, 8> information{};
  std::array<VkWriteDescriptorSet, 8> writes{};
  for (std::uint32_t index = 0; index < writes.size(); ++index) {
    Buffer *buffer = index == 2   ? (bound_buffers ? shared_[2] : slot.parameters)
                     : index == 3 ? (bound_buffers ? shared_[3] : slot.data)
                                  : shared_[index];
    if (!buffer)
      throw std::runtime_error("GS descriptor buffer is not bound");
    if (index != 2 && buffer->size > limits_.maxStorageBufferRange)
      throw std::runtime_error("GS descriptor buffer exceeds Vulkan limits");
    information[index].buffer = buffer->handle;
    information[index].range = index == 2 ? 256 : buffer->size;
    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[index].dstSet = slot.descriptors;
    writes[index].dstBinding = index;
    writes[index].descriptorCount = 1;
    writes[index].pBufferInfo = &information[index];
    writes[index].descriptorType =
        index == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                   : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
                         writes.data(), 0, nullptr);
}

void Context::validate_dispatches(const Dispatch *commands, std::size_t count,
                                  std::uint64_t parameter_capacity) const {
  for (std::size_t index = 0; index < count; ++index) {
    const auto &dispatch = commands[index];
    if (dispatch.pipeline >= pipelines_.size() ||
        dispatch.x > limits_.maxComputeWorkGroupCount[0] ||
        dispatch.y > limits_.maxComputeWorkGroupCount[1] ||
        static_cast<std::uint64_t>(dispatch.parameter) * 256 > UINT32_MAX ||
        static_cast<std::uint64_t>(dispatch.parameter) * 256 + 256 >
            parameter_capacity)
      throw std::runtime_error(
          "Invalid GS Vulkan dispatch: pipeline=" +
          std::to_string(dispatch.pipeline) + " x=" +
          std::to_string(dispatch.x) + " y=" + std::to_string(dispatch.y) +
          " parameter=" + std::to_string(dispatch.parameter) +
          " parameter_bytes=" + std::to_string(parameter_capacity));
  }
}

void Context::record_dispatches(VkCommandBuffer command,
                                VkDescriptorSet descriptors,
                                const Dispatch *commands, std::size_t count) {
  // Host uploads of this slot are visible to the shaders.
  memory_barrier(command,
                 VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                     VK_ACCESS_SHADER_WRITE_BIT);
  for (std::size_t index = 0; index < count; ++index) {
    const auto &dispatch = commands[index];
    // Vulkan permits zero workgroups; fully clipped batches do no shader work.
    if ((!dispatch.x || !dispatch.y) && zero_dispatches_++ == 0)
      std::fprintf(stderr,
                   "[vulkan] first zero-work dispatch pipeline=%u x=%u y=%u "
                   "parameter=%u\n",
                   dispatch.pipeline, dispatch.x, dispatch.y,
                   dispatch.parameter);
    const std::uint32_t offset = dispatch.parameter * 256;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipelines_[dispatch.pipeline]);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout_, 0, 1, &descriptors, 1, &offset);
    vkCmdDispatch(command, dispatch.x, dispatch.y, 1);
#if defined(GT4_PERF_PROBE) && !defined(GT4_PERF_FRAME_ONLY)
    gt4::perf::Probe::shared().dispatch(std::uint64_t(dispatch.x) * dispatch.y);
#endif
    // Every dispatch fully orders the next one (kept deliberately).
    memory_barrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  }
  memory_barrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT);
}

void Context::wait_idle() {
  GT4_PERF_SCOPE(gt4::perf::Category::gs_wait);
  check(vkQueueWaitIdle(queue_), "queue idle");
}

void Context::attach_window(HWND window, std::uint32_t width,
                            std::uint32_t height) {
  if (!window || window_)
    throw std::runtime_error(std::string("Invalid or already attached ") + kSurfaceName + " window");
  window_ = window;
  window_width_ = width;
  window_height_ = height;
#ifdef _WIN32
  VkWin32SurfaceCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  info.hinstance = GetModuleHandleW(nullptr);
  info.hwnd = window;
  check(vkCreateWin32SurfaceKHR(instance_, &info, nullptr, &surface_),
        "Win32 Vulkan surface");
#else
  // A CAMetalLayer-backed view over the SDL window's content; width and height are its size in pixels.
  SDL_MetalView view = SDL_Metal_CreateView(window);
  if (!view)
    throw std::runtime_error(std::string("SDL_Metal_CreateView failed: ") + SDL_GetError());
  metal_view_ = view;
  VkMetalSurfaceCreateInfoEXT info{};
  info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  info.pLayer = static_cast<const CAMetalLayer *>(SDL_Metal_GetLayer(view));
  check(vkCreateMetalSurfaceEXT(instance_, &info, nullptr, &surface_),
        "Metal Vulkan surface");
#endif
  VkBool32 supported = VK_FALSE;
  check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queue_family_, surface_,
                                             &supported),
        "surface support");
  if (!supported)
    throw std::runtime_error(
        std::string("Selected GPU queue cannot present to this ") + kSurfaceName + " surface");
  rebuild_swapchain();
}

void Context::resize(std::uint32_t width, std::uint32_t height) {
  window_width_ = width;
  window_height_ = height;
  resized_ = true;
}

void Context::rebuild_swapchain() {
  if (!window_width_ || !window_height_)
    return;
  wait_idle();
  wait_all_presentations();
  // Without present fences, vkDeviceWaitIdle is the only wait that covers the
  // presentation engine's semaphore waits before the swapchain is replaced.
  if (!present_fences_)
    check(vkDeviceWaitIdle(device_), "device idle before swapchain rebuild");
  VkSurfaceCapabilitiesKHR capabilities{};
  check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_,
                                                  &capabilities),
        "surface capabilities");
  if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      !(capabilities.supportedCompositeAlpha &
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
    throw std::runtime_error(
        std::string(kSurfaceName) + " surface does not support opaque transfer presentation");
  std::uint32_t count = 0;
  check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count,
                                             nullptr),
        "surface formats");
  std::vector<VkSurfaceFormatKHR> formats(count);
  check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count,
                                             formats.data()),
        "surface formats");
  auto format = VK_FORMAT_UNDEFINED;
  for (const auto &candidate : formats) {
    if (candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
        (candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
         candidate.format == VK_FORMAT_R8G8B8A8_UNORM)) {
      format = candidate.format;
      break;
    }
  }
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    format = VK_FORMAT_B8G8R8A8_UNORM;
  if (format == VK_FORMAT_UNDEFINED)
    throw std::runtime_error(
        "No UNORM swapchain format; do not add implicit sRGB conversion");
  VkFormatProperties source_features{}, destination_features{};
  vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM,
                                      &source_features);
  vkGetPhysicalDeviceFormatProperties(physical_, format, &destination_features);
  if (!(source_features.optimalTilingFeatures &
        VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !(destination_features.optimalTilingFeatures &
        VK_FORMAT_FEATURE_BLIT_DST_BIT))
    throw std::runtime_error(
        "GPU formats do not support the native presentation blit");
  extent_ = capabilities.currentExtent;
  if (extent_.width == UINT32_MAX) {
    extent_.width = std::clamp(window_width_, capabilities.minImageExtent.width,
                               capabilities.maxImageExtent.width);
    extent_.height =
        std::clamp(window_height_, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }
  if (!extent_.width || !extent_.height) {
    resized_ = true;
    return;
  }
  std::uint32_t images = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount)
    images = std::min(images, capabilities.maxImageCount);
  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface_;
  info.minImageCount = images;
  info.imageFormat = format;
  info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  // Overlay layers (RivaTuner, Steam) draw into swapchain images, so request
  // color-attachment use whenever the surface supports it. Transfer is kept.
  info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
    info.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  } else {
    std::fprintf(stderr, "[vulkan] surface lacks COLOR_ATTACHMENT usage; "
                         "overlay layers may not draw\n");
  }
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = capabilities.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = vulkan_present_mode(present_mode_);
  {
    std::uint32_t mode_count = 0;
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_,
                                                    &mode_count, nullptr),
          "surface present modes");
    std::vector<VkPresentModeKHR> modes(mode_count);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_,
                                                    &mode_count, modes.data()),
          "surface present modes");
    if (std::find(modes.begin(), modes.end(), info.presentMode) == modes.end())
      throw std::runtime_error(std::string("GT4_PRESENT_MODE ") +
                               gt4::pacing::present_mode_name(present_mode_) +
                               " is not supported by this surface");
  }
  info.clipped = VK_TRUE;
  info.oldSwapchain = swapchain_;
  // Implicit overlay layers may add bits to the create info, so log the request.
  const VkImageUsageFlags requested_usage = info.imageUsage;
  VkSwapchainKHR next = VK_NULL_HANDLE;
  check(vkCreateSwapchainKHR(device_, &info, nullptr, &next),
        "create swapchain");
  if (swapchain_)
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = next;
  resized_ = false;
  // Baseline path: one present semaphore per swapchain image. The device is
  // idle here (wait_idle above, plus vkDeviceWaitIdle for pending presents), so
  // the previous set can be destroyed, signalled or not.
  for (const VkSemaphore semaphore : image_rendered_)
    vkDestroySemaphore(device_, semaphore, nullptr);
  image_rendered_.clear();
  if (!present_fences_) {
    std::uint32_t image_count = 0;
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr),
          "swapchain images");
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    image_rendered_.resize(image_count, VK_NULL_HANDLE);
    for (auto &semaphore : image_rendered_)
      check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore),
            "per-image present semaphore");
  }
  std::fprintf(stderr,
               "[vulkan] %s swapchain %ux%u, format=%d, requested usage=0x%x "
               "(surface supports 0x%x); GPU blit, no temporal field blend\n",
               kSurfaceName, extent_.width, extent_.height, static_cast<int>(format),
               static_cast<unsigned>(requested_usage),
               static_cast<unsigned>(capabilities.supportedUsageFlags));
}

void Context::destroy_image() {
  if (display_image_)
    vkDestroyImage(device_, display_image_, nullptr);
  if (display_memory_)
    vkFreeMemory(device_, display_memory_, nullptr);
  display_image_ = VK_NULL_HANDLE;
  display_memory_ = VK_NULL_HANDLE;
}

void Context::image_barrier(VkCommandBuffer command, VkImage image,
                            VkImageLayout old_layout, VkImageLayout new_layout,
                            VkPipelineStageFlags source,
                            VkPipelineStageFlags destination,
                            VkAccessFlags writes, VkAccessFlags reads) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcAccessMask = writes;
  barrier.dstAccessMask = reads;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command, source, destination, 0, 0, nullptr, 0, nullptr,
                       1, &barrier);
}

void Context::ensure_display_image(std::uint32_t width, std::uint32_t height) {
  if (width == image_width_ && height == image_height_)
    return;
  // The display image is shared by every submission; drain them before replacing it.
  wait_idle();
  destroy_image();
  VkImageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
  info.extent = {width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  check(vkCreateImage(device_, &info, nullptr, &display_image_),
        "display image");
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, display_image_, &requirements);
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      memory_type(memory_properties_, requirements.memoryTypeBits,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(device_, &allocation, nullptr, &display_memory_),
        "display image memory");
  check(vkBindImageMemory(device_, display_image_, display_memory_, 0),
        "bind display image");
  image_width_ = width;
  image_height_ = height;
}

void Context::record_presentation(VkCommandBuffer command,
                                  const Submission &submission,
                                  VkImage target) {
  const std::uint32_t width = submission.present_width;
  const std::uint32_t height = submission.present_height;
  memory_barrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                              VK_PIPELINE_STAGE_HOST_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT);
  // Contents are replaced completely; UNDEFINED deliberately discards the
  // previous frame.
  image_barrier(command, display_image_, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(command, submission.present_source->handle,
                         display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &copy);
  image_barrier(command, display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  // The acquire semaphore is waited at TRANSFER; the transition must be ordered after it.
  image_barrier(command, target, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(command, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       &black, 1, &range);
  memory_barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT);
  // TASK-080: the race view at a ratio other than 16:9 fills a rectangle of that ratio (linear filter, the frame is
  // stretched by a non-integer factor); everything else keeps the 16:9 pillarbox and nearest filter.
  const bool race_view_fill = g_race_view_fill.load();
  double race_view_ratio = 0.0;
  if (race_view_fill) {
    const std::uint64_t bits = g_race_view_ratio_bits.load();
    std::memcpy(&race_view_ratio, &bits, sizeof race_view_ratio);
  }
  const auto destination =
      race_view_fill ? presentation_rect_ratio(extent_.width, extent_.height, race_view_ratio)
                     : presentation_rect(extent_.width, extent_.height, aspect_);
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {static_cast<int>(width), static_cast<int>(height), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[0] = {static_cast<int>(destination.left),
                        static_cast<int>(destination.top), 0};
  blit.dstOffsets[1] = {static_cast<int>(destination.left + destination.width),
                        static_cast<int>(destination.top + destination.height),
                        1};
  vkCmdBlitImage(command, display_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 race_view_fill ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
  // The rendered semaphore signals at ALL_COMMANDS, so presentation follows this transition.
  image_barrier(command, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0);
}

bool Context::acquire_image(Slot &slot, const Submission &submission,
                            VkImage &target, std::uint32_t &image) {
  if (!window_ || !window_width_ || !window_height_)
    return false;
  if (resized_ || !swapchain_)
    rebuild_swapchain();
  if (!extent_.width || !extent_.height)
    return false;
  const std::uint32_t width = submission.present_width;
  const std::uint32_t height = submission.present_height;
  if (!submission.present_source || !width || !height ||
      static_cast<std::uint64_t>(width) * height * 4 >
          submission.present_source->size)
    throw std::runtime_error("Invalid native Vulkan frame size");
  ensure_display_image(width, height);
  std::uint32_t count = 0;
  check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr),
        "swapchain images");
  std::vector<VkImage> images(count);
  check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data()),
        "swapchain images");
  VkResult acquired;
  {
    GT4_PERF_SCOPE(gt4::perf::Category::acquire_wait);
    acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                     slot.acquired, VK_NULL_HANDLE, &image);
  }
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    resized_ = true;
    return false;
  }
  if (acquired == VK_SUBOPTIMAL_KHR)
    resized_ = true;
  else
    check(acquired, "acquire swapchain image");
  if (image >= images.size())
    throw std::runtime_error("Invalid acquired swapchain image index");
  target = images[image];
  return true;
}

void Context::execute(const Dispatch *commands, std::size_t count) {
  Submission submission;
  submission.commands = commands;
  submission.command_count = count;
  submission.use_bound_buffers = true;
  submit(submission);
}

std::uint64_t Context::submit(const Submission &submission, bool *presented) {
  if (presented)
    *presented = false;
  if (!submission.command_count && !submission.present_source)
    return 0;
  if (submission.command_count && !submission.commands)
    throw std::runtime_error("GS submission has no dispatch list");
  const std::uint64_t id = ring_->next_id();
  // Blocks only while the submission that used this slot is still in flight.
  Slot &slot = slots_[ring_->reserve()];
  wait_presentation(slot);
  if (!submission.use_bound_buffers) {
    ensure_slot_capacity(slot, submission.arena_bytes,
                         submission.parameter_bytes);
    if (submission.arena_bytes)
      write(slot.data, 0, submission.arena, submission.arena_bytes);
    if (submission.parameter_bytes)
      write(slot.parameters, 0, submission.parameters,
            submission.parameter_bytes);
  }
  Buffer *parameters =
      submission.use_bound_buffers ? shared_[2] : slot.parameters;
  if (!parameters)
    throw std::runtime_error("GS dispatch parameters are not bound");
  // Validate before acquiring or signaling anything: a throw after the
  // acquire semaphore is signaled would leave it unbalanced.
  validate_dispatches(submission.commands, submission.command_count,
                      parameters->size);
  update_descriptors(slot, submission.use_bound_buffers);
  if (submission.present_source && id > kMaxPresentsInFlight)
    wait_presentation(
        slots_[ring_->slot_for(id - kMaxPresentsInFlight)]);
  VkImage target = VK_NULL_HANDLE;
  std::uint32_t image = 0;
  const bool presenting =
      submission.present_source && acquire_image(slot, submission, target, image);
  GT4_PERF_SCOPE(gt4::perf::Category::gs_execute);
  check(vkResetCommandBuffer(slot.command, 0), "reset GS command buffer");
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(slot.command, &begin), "begin GS command buffer");
  record_dispatches(slot.command, slot.descriptors, submission.commands,
                    submission.command_count);
  if (presenting)
    record_presentation(slot.command, submission, target);
  check(vkEndCommandBuffer(slot.command), "end GS command buffer");
  // Submissions share one queue, but Vulkan orders separate submissions only
  // through synchronization. Each waits for its predecessor's timeline value.
  VkSemaphore waits[2] = {timeline_, slot.acquired};
  VkPipelineStageFlags wait_stages[2] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT};
  std::uint64_t wait_values[2] = {id - 1, 0};
  // The present semaphore: per slot with present fences, per swapchain image on
  // the baseline path (reused only after that image is acquired again).
  const VkSemaphore rendered =
      presenting && !present_fences_ ? image_rendered_.at(image) : slot.rendered;
  VkSemaphore signals[2] = {timeline_, rendered};
  std::uint64_t signal_values[2] = {id, 0};
  const std::uint32_t wait_count = presenting ? 2 : 1;
  VkTimelineSemaphoreSubmitInfo timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  timeline.waitSemaphoreValueCount = wait_count;
  timeline.pWaitSemaphoreValues = wait_values;
  timeline.signalSemaphoreValueCount = wait_count;
  timeline.pSignalSemaphoreValues = signal_values;
  VkSubmitInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  info.pNext = &timeline;
  info.waitSemaphoreCount = wait_count;
  info.pWaitSemaphores = waits;
  info.pWaitDstStageMask = wait_stages;
  info.commandBufferCount = 1;
  info.pCommandBuffers = &slot.command;
  info.signalSemaphoreCount = wait_count;
  info.pSignalSemaphores = signals;
  check(vkQueueSubmit(queue_, 1, &info, VK_NULL_HANDLE), "queue GS submit");
  if (ring_->commit() != id)
    throw std::runtime_error("GS submission ids out of order");
  if (presenting) {
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &rendered;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image;
    VkSwapchainPresentFenceInfoKHR completion{};
    completion.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
    completion.swapchainCount = 1;
    completion.pFences = &slot.present_fence;
    if (present_fences_) {
      check(vkResetFences(device_, 1, &slot.present_fence),
            "reset presentation fence");
      present_info.pNext = &completion;
    }
    slot.presented_id = id;
    const auto result = vkQueuePresentKHR(queue_, &present_info);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
        result != VK_ERROR_OUT_OF_DATE_KHR &&
        result != VK_ERROR_SURFACE_LOST_KHR)
      check(result, "present submission");
    // The fence signals when this presentation completes, including failures.
    slot.presented = true;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
      resized_ = true;
    else if (result != VK_SUCCESS)
      check(result, "present frame");
    ++presentations_;
#if defined(GT4_PERF_PROBE) && !defined(GT4_PERF_FRAME_ONLY)
    gt4::perf::Probe::shared().presentation(
        std::uint64_t(submission.present_width) * submission.present_height * 4);
#endif
    if (presented)
      *presented = true;
  }
  if (synchronous_) {
    wait_timeline(id);
    wait_presentation(slot);
  }
  return id;
}

} // namespace gt4::vulkan
