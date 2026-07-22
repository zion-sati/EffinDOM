#include "LinuxVulkanSurface.h"

#include "SDL3/SDL.h"

#include <algorithm>
#include <utility>

#if defined(EFFINDOM_SKIA_VULKAN)

#include "SDL3/SDL_vulkan.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"

#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace effindom::v2::native {
namespace {

bool VkSucceeded(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return true;
    SDL_Log("EffinDOM Vulkan failed at %s (VkResult %d)", operation, static_cast<int>(result));
    return false;
}

bool HasInstanceExtension(const char* name) {
    std::uint32_t count = 0U;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

std::optional<SkColorType> ColorTypeForFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return kRGBA_8888_SkColorType;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return kBGRA_8888_SkColorType;
        default:
            return std::nullopt;
    }
}

const char* PresentModeName(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT: return "fifo-latest-ready";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo-relaxed";
        default: return "fifo";
    }
}

} // namespace

struct LinuxVulkanSurface::Impl {
    struct RetiredSwapchain {
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        std::vector<sk_sp<SkSurface>> surfaces;
        std::vector<VkSemaphore> render_finished;
        std::uint64_t last_present_id = 0U;
    };

    explicit Impl(SDL_Window* value) : window(value) {
        const char* diagnostic = SDL_GetEnvironmentVariable(
            SDL_GetEnvironment(), "EFFINDOM_LINUX_CHARACTERIZE");
        characterize = diagnostic != nullptr && diagnostic[0] != '\0' && diagnostic[0] != '0';
    }

    bool Initialize() {
        last_swapchain_change_ns = 0U;
        std::uint32_t extension_count = 0U;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (extensions == nullptr || extension_count == 0U) {
            SDL_Log("EffinDOM Vulkan initialization failed: %s", SDL_GetError());
            return false;
        }

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "EffinDOM";
        app_info.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
        app_info.pEngineName = "EffinDOM";
        app_info.engineVersion = VK_MAKE_VERSION(2, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_1;

        std::vector<const char*> requested_extensions(
            extensions, extensions + extension_count);
        surface_maintenance =
            HasInstanceExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
            HasInstanceExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (surface_maintenance) {
            requested_extensions.push_back(
                VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            requested_extensions.push_back(
                VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        }

        VkInstanceCreateInfo instance_info{};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;
        instance_info.enabledExtensionCount =
            static_cast<std::uint32_t>(requested_extensions.size());
        instance_info.ppEnabledExtensionNames = requested_extensions.data();
        if (!VkSucceeded(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance")) {
            return false;
        }
        instance_extensions = std::move(requested_extensions);

        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &native_surface)) {
            SDL_Log("EffinDOM Vulkan surface creation failed: %s", SDL_GetError());
            return false;
        }
        if (!SelectPhysicalDeviceAndQueue() || !CreateDeviceAndContext()) return false;
        if (characterize) {
            SDL_Log("EffinDOM Vulkan hardware present feedback: %s",
                hardware_present_wait ? "enabled" : "unavailable");
        }
        if (!CreateSwapchain()) return false;
        ++generation;
        return true;
    }

    void ReclaimRetiredSwapchainsAfterQuiet() {
        const std::uint64_t now_ns = SDL_GetTicksNS();
        if (!retired_swapchains.empty() && last_swapchain_change_ns != 0U &&
            now_ns - last_swapchain_change_ns >= 250'000'000U) {
            // Reclaim resize-era swapchains only after geometry has been
            // quiet, never from the interactive resize path itself.
            vkDeviceWaitIdle(device);
            ReleaseRetiredSwapchains();
        }
    }

    void ReclaimCompletedRetiredSwapchains() {
        if (!hardware_present_wait || wait_for_present == nullptr) return;
        auto retired = retired_swapchains.begin();
        while (retired != retired_swapchains.end()) {
            const VkResult result = retired->last_present_id == 0U
                ? VK_SUCCESS
                : wait_for_present(device, retired->handle,
                    retired->last_present_id, 0U);
            if (result != VK_SUCCESS) {
                ++retired;
                continue;
            }
            retired->surfaces.clear();
            for (VkSemaphore semaphore : retired->render_finished) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                }
            }
            if (retired->handle != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device, retired->handle, nullptr);
            }
            retired = retired_swapchains.erase(retired);
        }
    }

    bool PreviousPresentCompleted() {
        if (!hardware_present_wait || last_present_id == 0U) return true;
        const VkResult result = wait_for_present(device, swapchain, last_present_id, 0U);
        if (result == VK_SUCCESS) {
            last_present_id = 0U;
            return true;
        }
        if (result == VK_TIMEOUT) {
            current_surface = nullptr;
            return false;
        }
        // Out-of-date handling remains centralized in acquire/present. Do not
        // turn a presentation-feedback failure into an event-pump stall.
        last_present_id = 0U;
        return true;
    }

    bool HasDeviceExtension(const char* name) const {
        std::uint32_t count = 0U;
        if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr) !=
            VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(
                physical_device, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
        return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    }

    bool SelectPhysicalDeviceAndQueue() {
        std::uint32_t device_count = 0U;
        if (!VkSucceeded(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
                "vkEnumeratePhysicalDevices(count)") || device_count == 0U) return false;
        std::vector<VkPhysicalDevice> devices(device_count);
        if (!VkSucceeded(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()),
                "vkEnumeratePhysicalDevices")) return false;

        for (VkPhysicalDevice candidate : devices) {
            std::uint32_t queue_count = 0U;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, queues.data());
            for (std::uint32_t index = 0U; index < queue_count; ++index) {
                VkBool32 present_supported = VK_FALSE;
                if (vkGetPhysicalDeviceSurfaceSupportKHR(
                        candidate, index, native_surface, &present_supported) != VK_SUCCESS) continue;
                if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U && present_supported) {
                    physical_device = candidate;
                    queue_family = index;
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(candidate, &properties);
                    SDL_Log("EffinDOM Skia Vulkan device: %s", properties.deviceName);
                    return true;
                }
            }
        }
        SDL_Log("EffinDOM Vulkan initialization failed: no graphics/present queue was found");
        return false;
    }

    bool CreateDeviceAndContext() {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1U;
        queue_info.pQueuePriorities = &priority;

        VkPhysicalDeviceFeatures features{};
        device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        const bool has_latest_ready_extension =
            HasDeviceExtension(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
        const bool has_present_id_extension = HasDeviceExtension(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        const bool has_present_wait_extension = HasDeviceExtension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        const bool has_swapchain_maintenance_extension = surface_maintenance &&
            HasDeviceExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

        VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT available_latest_ready{};
        available_latest_ready.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT;
        VkPhysicalDevicePresentIdFeaturesKHR available_present_id{};
        available_present_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        VkPhysicalDevicePresentWaitFeaturesKHR available_present_wait{};
        available_present_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT available_maintenance{};
        available_maintenance.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 available_features{};
        available_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        void* available_chain = nullptr;
        if (has_present_id_extension && has_present_wait_extension) {
            available_present_wait.pNext = available_chain;
            available_chain = &available_present_wait;
            available_present_id.pNext = available_chain;
            available_chain = &available_present_id;
        }
        if (has_latest_ready_extension) {
            available_latest_ready.pNext = available_chain;
            available_chain = &available_latest_ready;
        }
        if (has_swapchain_maintenance_extension) {
            available_maintenance.pNext = available_chain;
            available_chain = &available_maintenance;
        }
        available_features.pNext = available_chain;
        if (available_chain != nullptr) {
            vkGetPhysicalDeviceFeatures2(physical_device, &available_features);
        }
        if (has_latest_ready_extension) {
            fifo_latest_ready = available_latest_ready.presentModeFifoLatestReady == VK_TRUE;
            if (fifo_latest_ready) {
                device_extensions.push_back(
                    VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
            }
        }
        hardware_present_wait = has_present_id_extension && has_present_wait_extension &&
            available_present_id.presentId == VK_TRUE &&
            available_present_wait.presentWait == VK_TRUE;
        if (hardware_present_wait) {
            device_extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            device_extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        }
        swapchain_maintenance = has_swapchain_maintenance_extension &&
            available_maintenance.swapchainMaintenance1 == VK_TRUE;
        if (swapchain_maintenance) {
            device_extensions.push_back(
                VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        }

        VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT enabled_latest_ready{};
        enabled_latest_ready.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT;
        enabled_latest_ready.presentModeFifoLatestReady = fifo_latest_ready ? VK_TRUE : VK_FALSE;
        VkPhysicalDevicePresentIdFeaturesKHR enabled_present_id{};
        enabled_present_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        enabled_present_id.presentId = hardware_present_wait ? VK_TRUE : VK_FALSE;
        VkPhysicalDevicePresentWaitFeaturesKHR enabled_present_wait{};
        enabled_present_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
        enabled_present_wait.presentWait = hardware_present_wait ? VK_TRUE : VK_FALSE;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT enabled_maintenance{};
        enabled_maintenance.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        enabled_maintenance.swapchainMaintenance1 =
            swapchain_maintenance ? VK_TRUE : VK_FALSE;
        void* enabled_chain = nullptr;
        if (swapchain_maintenance) {
            enabled_maintenance.pNext = enabled_chain;
            enabled_chain = &enabled_maintenance;
        }
        if (hardware_present_wait) {
            enabled_present_wait.pNext = enabled_chain;
            enabled_chain = &enabled_present_wait;
            enabled_present_id.pNext = enabled_chain;
            enabled_chain = &enabled_present_id;
        }
        if (fifo_latest_ready) {
            enabled_latest_ready.pNext = enabled_chain;
            enabled_chain = &enabled_latest_ready;
        }

        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.pNext = enabled_chain;
        device_info.queueCreateInfoCount = 1U;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
        device_info.ppEnabledExtensionNames = device_extensions.data();
        device_info.pEnabledFeatures = &features;
        if (!VkSucceeded(vkCreateDevice(physical_device, &device_info, nullptr, &device),
                "vkCreateDevice")) return false;
        vkGetDeviceQueue(device, queue_family, 0U, &queue);
        if (hardware_present_wait) {
            wait_for_present = reinterpret_cast<PFN_vkWaitForPresentKHR>(
                vkGetDeviceProcAddr(device, "vkWaitForPresentKHR"));
            hardware_present_wait = wait_for_present != nullptr;
        }

        get_proc = [this](const char* name, VkInstance requested_instance, VkDevice requested_device) {
            if (requested_device != VK_NULL_HANDLE) {
                return vkGetDeviceProcAddr(requested_device, name);
            }
            return vkGetInstanceProcAddr(requested_instance, name);
        };
        vk_extensions.init(
            get_proc,
            instance,
            physical_device,
            static_cast<std::uint32_t>(instance_extensions.size()),
            instance_extensions.data(),
            static_cast<std::uint32_t>(device_extensions.size()),
            device_extensions.data());

        skgpu::VulkanBackendContext backend{};
        backend.fInstance = instance;
        backend.fPhysicalDevice = physical_device;
        backend.fDevice = device;
        backend.fQueue = queue;
        backend.fGraphicsQueueIndex = queue_family;
        backend.fMaxAPIVersion = VK_API_VERSION_1_1;
        backend.fVkExtensions = &vk_extensions;
        backend.fDeviceFeatures = &features;
        backend.fGetProc = get_proc;
        context = GrDirectContexts::MakeVulkan(backend);
        if (context == nullptr) {
            SDL_Log("EffinDOM Vulkan initialization failed: Skia rejected the backend context");
            return false;
        }
        return true;
    }

    bool CreateSwapchain(bool tolerate_transient_failure = false) {
        const std::uint64_t started_ns = SDL_GetTicksNS();
        const bool recreating = swapchain != VK_NULL_HANDLE;
        VkSurfaceCapabilitiesKHR capabilities{};
        if (!VkSucceeded(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physical_device, native_surface, &capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return false;

        std::uint32_t format_count = 0U;
        if (!VkSucceeded(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physical_device, native_surface, &format_count, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(count)") || format_count == 0U) return false;
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        if (!VkSucceeded(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physical_device, native_surface, &format_count, formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR")) return false;

        std::optional<SkColorType> selected_color_type;
        VkSurfaceFormatKHR selected_format{};
        for (const VkSurfaceFormatKHR& candidate : formats) {
            const auto candidate_color_type = ColorTypeForFormat(candidate.format);
            if (!candidate_color_type.has_value()) continue;
            selected_format = candidate;
            selected_color_type = candidate_color_type;
            if (candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                (candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
                 candidate.format == VK_FORMAT_R8G8B8A8_UNORM)) break;
        }
        if (!selected_color_type.has_value()) {
            SDL_Log("EffinDOM Vulkan swapchain failed: no Skia-compatible surface format");
            return false;
        }

        int drawable_width = 0;
        int drawable_height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height)) return false;
        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            extent.width = std::clamp(
                static_cast<std::uint32_t>(std::max(drawable_width, 1)),
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width);
            extent.height = std::clamp(
                static_cast<std::uint32_t>(std::max(drawable_height, 1)),
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);
        }
        if (extent.width == 0U || extent.height == 0U) return false;

        std::uint32_t image_count = std::max(capabilities.minImageCount + 1U, 2U);
        if (capabilities.maxImageCount != 0U) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0U) {
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0U) {
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> alpha_modes = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        };
        for (const auto candidate : alpha_modes) {
            if ((capabilities.supportedCompositeAlpha & candidate) != 0U) {
                composite_alpha = candidate;
                break;
            }
        }

        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        std::uint32_t present_mode_count = 0U;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                physical_device, native_surface, &present_mode_count, nullptr) == VK_SUCCESS &&
            present_mode_count != 0U) {
            std::vector<VkPresentModeKHR> present_modes(present_mode_count);
            if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physical_device, native_surface, &present_mode_count,
                    present_modes.data()) == VK_SUCCESS) {
                if (characterize) {
                    std::string modes;
                    for (const VkPresentModeKHR mode : present_modes) {
                        if (!modes.empty()) modes += ',';
                        modes += std::to_string(static_cast<int>(mode));
                    }
                    SDL_Log("EffinDOM Vulkan present modes: %s", modes.c_str());
                }
                if (fifo_latest_ready &&
                    std::find(present_modes.begin(), present_modes.end(),
                        VK_PRESENT_MODE_FIFO_LATEST_READY_EXT) != present_modes.end()) {
                    // Retain vblank/hardware pacing while allowing the
                    // presentation engine to skip obsolete ready frames.
                    present_mode = VK_PRESENT_MODE_FIFO_LATEST_READY_EXT;
                } else if (std::find(present_modes.begin(), present_modes.end(),
                               VK_PRESENT_MODE_MAILBOX_KHR) != present_modes.end()) {
                    present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                }
            }
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = native_surface;
        info.minImageCount = image_count;
        info.imageFormat = selected_format.format;
        info.imageColorSpace = selected_format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1U;
        info.imageUsage = usage;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = composite_alpha;
        info.presentMode = present_mode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = swapchain;

        VkSwapchainPresentScalingCreateInfoEXT scaling_info{};
        if (swapchain_maintenance) {
            VkSurfacePresentModeEXT surface_present_mode{};
            surface_present_mode.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT;
            surface_present_mode.presentMode = present_mode;
            VkPhysicalDeviceSurfaceInfo2KHR surface_info{};
            surface_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
            surface_info.pNext = &surface_present_mode;
            surface_info.surface = native_surface;
            VkSurfacePresentScalingCapabilitiesEXT scaling_capabilities{};
            scaling_capabilities.sType =
                VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT;
            VkSurfaceCapabilities2KHR capabilities2{};
            capabilities2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
            capabilities2.pNext = &scaling_capabilities;
            const VkResult scaling_result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(
                physical_device, &surface_info, &capabilities2);
            resize_scaling = scaling_result == VK_SUCCESS &&
                (scaling_capabilities.supportedPresentScaling &
                    VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT) != 0U &&
                (scaling_capabilities.supportedPresentGravityX &
                    VK_PRESENT_GRAVITY_MIN_BIT_EXT) != 0U &&
                (scaling_capabilities.supportedPresentGravityY &
                    VK_PRESENT_GRAVITY_MIN_BIT_EXT) != 0U;
            if (resize_scaling) {
                scaling_info.sType =
                    VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT;
                scaling_info.scalingBehavior =
                    VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT;
                scaling_info.presentGravityX = VK_PRESENT_GRAVITY_MIN_BIT_EXT;
                scaling_info.presentGravityY = VK_PRESENT_GRAVITY_MIN_BIT_EXT;
                info.pNext = &scaling_info;
            }
        }

        const std::uint64_t query_finished_ns = SDL_GetTicksNS();
        VkSwapchainKHR next_swapchain = VK_NULL_HANDLE;
        const VkResult create_result =
            vkCreateSwapchainKHR(device, &info, nullptr, &next_swapchain);
        const std::uint64_t create_finished_ns = SDL_GetTicksNS();
        if (create_result != VK_SUCCESS) {
            const double duration_ms =
                static_cast<double>(create_finished_ns - started_ns) / 1'000'000.0;
            const char* deadline = duration_ms > 30.0 ? " *** OVER 30 MS ***" : "";
            SDL_Log("EffinDOM Vulkan swapchain recreate: FAILED result=%d "
                    "duration_ms=%.3f query_ms=%.3f vk_create_ms=%.3f%s",
                static_cast<int>(create_result), duration_ms,
                static_cast<double>(query_finished_ns - started_ns) / 1'000'000.0,
                static_cast<double>(create_finished_ns - query_finished_ns) / 1'000'000.0,
                deadline);
            if (tolerate_transient_failure &&
                create_result == VK_ERROR_INITIALIZATION_FAILED &&
                swapchain != VK_NULL_HANDLE) {
                // X11 may briefly report a drawable extent that its Vulkan
                // WSI path cannot instantiate during an interactive resize.
                // Retain the GPU backend and retry from the pending frame.
                current_surface = nullptr;
                swapchain_retry_pending = true;
                if (characterize) {
                    SDL_Log("EffinDOM Vulkan swapchain: transient create retry");
                }
                return true;
            }
            return VkSucceeded(create_result, "vkCreateSwapchainKHR");
        }

        std::uint32_t actual_count = 0U;
        if (!VkSucceeded(vkGetSwapchainImagesKHR(device, next_swapchain, &actual_count, nullptr),
                "vkGetSwapchainImagesKHR(count)") || actual_count == 0U) {
            vkDestroySwapchainKHR(device, next_swapchain, nullptr);
            return false;
        }
        std::vector<VkImage> next_images(actual_count);
        if (!VkSucceeded(vkGetSwapchainImagesKHR(
                device, next_swapchain, &actual_count, next_images.data()),
                "vkGetSwapchainImagesKHR")) {
            vkDestroySwapchainKHR(device, next_swapchain, nullptr);
            return false;
        }
        next_images.resize(actual_count);
        const std::uint64_t images_finished_ns = SDL_GetTicksNS();
        std::vector<sk_sp<SkSurface>> next_surfaces(actual_count);
        std::vector<VkSemaphore> next_render_finished(actual_count, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (std::size_t index = 0U; index < next_images.size(); ++index) {
            GrVkImageInfo image_info{};
            image_info.fImage = next_images[index];
            image_info.fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.fFormat = selected_format.format;
            image_info.fImageUsageFlags = usage;
            image_info.fSampleCount = 1U;
            image_info.fLevelCount = 1U;
            image_info.fCurrentQueueFamily = queue_family;
            image_info.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            const GrBackendRenderTarget target = GrBackendRenderTargets::MakeVk(
                static_cast<int>(extent.width), static_cast<int>(extent.height), image_info);
            next_surfaces[index] = SkSurfaces::WrapBackendRenderTarget(
                context.get(), target, kTopLeft_GrSurfaceOrigin, *selected_color_type,
                SkColorSpace::MakeSRGB(), nullptr);
            if (next_surfaces[index] == nullptr ||
                !VkSucceeded(vkCreateSemaphore(device, &semaphore_info, nullptr,
                    &next_render_finished[index]), "vkCreateSemaphore")) {
                for (VkSemaphore semaphore : next_render_finished) {
                    if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, semaphore, nullptr);
                }
                next_surfaces.clear();
                vkDestroySwapchainKHR(device, next_swapchain, nullptr);
                return false;
            }
        }
        const std::uint64_t wrapping_finished_ns = SDL_GetTicksNS();

        // Creating a replacement retires the old swapchain. Do not wait for
        // the device here: compositors are allowed to hold presented images
        // throughout an interactive resize, and waiting would block SDL's
        // event pump until mouse-up. Keep the old resources alive until the
        // final device-idle shutdown/recovery boundary.
        if (swapchain != VK_NULL_HANDLE) {
            retired_swapchains.push_back(RetiredSwapchain{
                swapchain,
                std::move(surfaces),
                std::move(render_finished),
                last_present_id,
            });
        }
        swapchain = next_swapchain;
        width = extent.width;
        height = extent.height;
        format = selected_format.format;
        color_type = *selected_color_type;
        usage_flags = usage;
        images = std::move(next_images);
        surfaces = std::move(next_surfaces);
        render_finished = std::move(next_render_finished);
        current_surface = nullptr;
        swapchain_retry_pending = false;
        recreate_after_present = false;
        last_present_id = 0U;
        last_swapchain_change_ns = SDL_GetTicksNS();
        ++generation;
        const double duration_ms =
            static_cast<double>(SDL_GetTicksNS() - started_ns) / 1'000'000.0;
        if (characterize) {
            const char* deadline = duration_ms > 30.0 ? " *** OVER 30 MS ***" : "";
            SDL_Log("EffinDOM Vulkan swapchain %s: size=%ux%u mode=%s retired=%zu "
                    "duration_ms=%.3f query_ms=%.3f vk_create_ms=%.3f "
                    "images_ms=%.3f skia_wrap_ms=%.3f finalize_ms=%.3f%s",
                recreating ? "recreate" : "create",
                width, height, PresentModeName(present_mode),
                retired_swapchains.size(), duration_ms,
                static_cast<double>(query_finished_ns - started_ns) / 1'000'000.0,
                static_cast<double>(create_finished_ns - query_finished_ns) / 1'000'000.0,
                static_cast<double>(images_finished_ns - create_finished_ns) / 1'000'000.0,
                static_cast<double>(wrapping_finished_ns - images_finished_ns) / 1'000'000.0,
                static_cast<double>(SDL_GetTicksNS() - wrapping_finished_ns) / 1'000'000.0,
                deadline);
        }
        return true;
    }

    void ReleaseSwapchainResources() {
        current_surface = nullptr;
        surfaces.clear();
        for (VkSemaphore semaphore : render_finished) {
            if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, semaphore, nullptr);
        }
        render_finished.clear();
        images.clear();
    }

    void ReleaseRetiredSwapchains() {
        for (RetiredSwapchain& retired : retired_swapchains) {
            retired.surfaces.clear();
            for (VkSemaphore semaphore : retired.render_finished) {
                if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, semaphore, nullptr);
            }
            if (retired.handle != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device, retired.handle, nullptr);
            }
        }
        retired_swapchains.clear();
    }

    bool Acquire() {
        const std::uint64_t started_ns = SDL_GetTicksNS();
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore acquired = VK_NULL_HANDLE;
        if (!VkSucceeded(vkCreateSemaphore(device, &semaphore_info, nullptr, &acquired),
                "vkCreateSemaphore(acquire)")) return false;
        VkResult result = vkAcquireNextImageKHR(
            device, swapchain, 0U,
            acquired, VK_NULL_HANDLE, &image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDestroySemaphore(device, acquired, nullptr);
            return CreateSwapchain() && Acquire();
        }
        if (result == VK_NOT_READY || result == VK_TIMEOUT) {
            // A compositor may temporarily own every image during resize.
            // Keep the frame pending and return to SDL instead of blocking.
            vkDestroySemaphore(device, acquired, nullptr);
            current_surface = nullptr;
            if (characterize) {
                SDL_Log("EffinDOM Vulkan acquire: unavailable duration_ms=%.3f",
                    static_cast<double>(SDL_GetTicksNS() - started_ns) / 1'000'000.0);
            }
            return true;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            vkDestroySemaphore(device, acquired, nullptr);
            return false;
        }
        recreate_after_present = result == VK_SUBOPTIMAL_KHR;
        current_surface = surfaces[image_index].get();
        const GrBackendSemaphore wait_semaphore = GrBackendSemaphores::MakeVk(acquired);
        if (!current_surface->wait(1, &wait_semaphore, true)) {
            vkDestroySemaphore(device, acquired, nullptr);
            current_surface = nullptr;
            return false;
        }
        if (characterize) {
            SDL_Log("EffinDOM Vulkan acquire: image=%u duration_ms=%.3f",
                image_index,
                static_cast<double>(SDL_GetTicksNS() - started_ns) / 1'000'000.0);
        }
        return true;
    }

    void Shutdown() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        ReleaseSwapchainResources();
        ReleaseRetiredSwapchains();
        if (context != nullptr) {
            context->abandonContext();
            context.reset();
        }
        if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        if (native_surface != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(instance, native_surface, nullptr);
        }
        native_surface = VK_NULL_HANDLE;
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        physical_device = VK_NULL_HANDLE;
    }

    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR native_surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    VkFormat format = VK_FORMAT_UNDEFINED;
    SkColorType color_type = kUnknown_SkColorType;
    VkImageUsageFlags usage_flags = 0U;
    std::vector<const char*> instance_extensions;
    std::vector<const char*> device_extensions;
    std::vector<VkImage> images;
    std::vector<sk_sp<SkSurface>> surfaces;
    std::vector<VkSemaphore> render_finished;
    std::vector<RetiredSwapchain> retired_swapchains;
    skgpu::VulkanGetProc get_proc;
    skgpu::VulkanExtensions vk_extensions;
    sk_sp<GrDirectContext> context;
    SkSurface* current_surface = nullptr;
    std::uint32_t image_index = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t recovery_count = 0U;
    std::uint64_t last_swapchain_change_ns = 0U;
    bool recovery_requested = false;
    bool swapchain_retry_pending = false;
    bool recreate_after_present = false;
    bool fifo_latest_ready = false;
    bool hardware_present_wait = false;
    bool surface_maintenance = false;
    bool swapchain_maintenance = false;
    bool resize_scaling = false;
    bool characterize = false;
    PFN_vkWaitForPresentKHR wait_for_present = nullptr;
    std::uint64_t present_serial = 0U;
    std::uint64_t last_present_id = 0U;
};

LinuxVulkanSurface::LinuxVulkanSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<LinuxVulkanSurface> LinuxVulkanSurface::Create(SDL_Window* window) {
    auto impl = std::make_unique<Impl>(window);
    if (!impl->Initialize()) {
        impl->Shutdown();
        return nullptr;
    }
    return std::unique_ptr<LinuxVulkanSurface>(new LinuxVulkanSurface(std::move(impl)));
}

LinuxVulkanSurface::~LinuxVulkanSurface() { impl_->Shutdown(); }

bool LinuxVulkanSurface::PrepareFrame(std::uint32_t width, std::uint32_t height, float) {
    if (impl_->recovery_requested) {
        const std::uint64_t recoveries = impl_->recovery_count + 1U;
        impl_->Shutdown();
        impl_->recovery_count = recoveries;
        impl_->recovery_requested = false;
        if (!impl_->Initialize()) {
            // A runtime failure never changes the selected backend. Leave the
            // frame pending and retry Vulkan initialization on the next turn.
            impl_->Shutdown();
            impl_->recovery_requested = true;
            return true;
        }
    }
    width = std::max(width, 1U);
    height = std::max(height, 1U);
    impl_->ReclaimCompletedRetiredSwapchains();
    const bool size_changed = width != impl_->width || height != impl_->height;
    if (size_changed && !impl_->CreateSwapchain(true)) {
        return false;
    }
    if (impl_->swapchain_retry_pending) return true;
    if (!size_changed) impl_->ReclaimRetiredSwapchainsAfterQuiet();
    if (!size_changed && !impl_->PreviousPresentCompleted()) return true;
    return impl_->Acquire();
}

bool LinuxVulkanSurface::QueryOutputSize(int& width, int& height) const {
    return SDL_GetWindowSizeInPixels(impl_->window, &width, &height);
}

SkCanvas* LinuxVulkanSurface::Canvas() const {
    return impl_->current_surface == nullptr ? nullptr : impl_->current_surface->getCanvas();
}

SkSurface* LinuxVulkanSurface::Surface() const { return impl_->current_surface; }

bool LinuxVulkanSurface::Present() {
    if (impl_->current_surface == nullptr) return false;
    const std::uint64_t started_ns = SDL_GetTicksNS();
    GrBackendSemaphore signal = GrBackendSemaphores::MakeVk(
        impl_->render_finished[impl_->image_index]);
    GrFlushInfo flush_info{};
    flush_info.fNumSemaphores = 1U;
    flush_info.fSignalSemaphores = &signal;
    const auto present_state = skgpu::MutableTextureStates::MakeVulkan(
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, impl_->queue_family);
    if (impl_->context->flush(impl_->current_surface, flush_info, &present_state) !=
        GrSemaphoresSubmitted::kYes) return false;
    if (!impl_->context->submit()) return false;

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1U;
    present_info.pWaitSemaphores = &impl_->render_finished[impl_->image_index];
    present_info.swapchainCount = 1U;
    present_info.pSwapchains = &impl_->swapchain;
    present_info.pImageIndices = &impl_->image_index;
    const std::uint64_t present_id = ++impl_->present_serial;
    VkPresentIdKHR present_id_info{};
    if (impl_->hardware_present_wait) {
        present_id_info.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
        present_id_info.swapchainCount = 1U;
        present_id_info.pPresentIds = &present_id;
        present_info.pNext = &present_id_info;
    }
    const VkResult result = vkQueuePresentKHR(impl_->queue, &present_info);
    if (impl_->hardware_present_wait &&
        (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
        impl_->last_present_id = present_id;
    }
    if (impl_->characterize) {
        SDL_Log("EffinDOM Vulkan present: result=%d duration_ms=%.3f",
            static_cast<int>(result),
            static_cast<double>(SDL_GetTicksNS() - started_ns) / 1'000'000.0);
    }
    impl_->current_surface = nullptr;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        impl_->recreate_after_present) {
        return impl_->CreateSwapchain();
    }
    return VkSucceeded(result, "vkQueuePresentKHR");
}

void LinuxVulkanSurface::RequestRecovery() { impl_->recovery_requested = true; }
bool LinuxVulkanSurface::HandleRecoveryEvent(const SDL_Event&) { return false; }
std::uint64_t LinuxVulkanSurface::Generation() const { return impl_->generation; }
std::uint64_t LinuxVulkanSurface::RecoveryCount() const { return impl_->recovery_count; }

} // namespace effindom::v2::native

#else

namespace effindom::v2::native {

struct LinuxVulkanSurface::Impl {};

std::unique_ptr<LinuxVulkanSurface> LinuxVulkanSurface::Create(SDL_Window*) { return nullptr; }
LinuxVulkanSurface::LinuxVulkanSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LinuxVulkanSurface::~LinuxVulkanSurface() = default;
bool LinuxVulkanSurface::PrepareFrame(std::uint32_t, std::uint32_t, float) { return false; }
bool LinuxVulkanSurface::QueryOutputSize(int&, int&) const { return false; }
bool LinuxVulkanSurface::Present() { return false; }
void LinuxVulkanSurface::RequestRecovery() {}
bool LinuxVulkanSurface::HandleRecoveryEvent(const SDL_Event&) { return false; }
SkCanvas* LinuxVulkanSurface::Canvas() const { return nullptr; }
SkSurface* LinuxVulkanSurface::Surface() const { return nullptr; }
std::uint64_t LinuxVulkanSurface::Generation() const { return 0U; }
std::uint64_t LinuxVulkanSurface::RecoveryCount() const { return 0U; }

} // namespace effindom::v2::native

#endif
