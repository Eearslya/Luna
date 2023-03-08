#pragma once

#include <Luna/Vulkan/Common.hpp>

#define vkBeginCommandBuffer          VULKAN_HPP_DEFAULT_DISPATCHER.vkBeginCommandBuffer
#define vkCmdResetQueryPool           VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdResetQueryPool
#define vkCmdWriteTimestamp           VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdWriteTimestamp
#define vkCreateQueryPool             VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateQueryPool
#define vkDestroyQueryPool            VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyQueryPool
#define vkEndCommandBuffer            VULKAN_HPP_DEFAULT_DISPATCHER.vkEndCommandBuffer
#define vkGetPhysicalDeviceProperties VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceProperties
#define vkGetQueryPoolResults         VULKAN_HPP_DEFAULT_DISPATCHER.vkGetQueryPoolResults
#define vkQueueSubmit                 VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueSubmit
#define vkQueueWaitIdle               VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueWaitIdle

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyVulkan.hpp>

#define LunaCmdZone(cmd, name)                                          \
	ZoneScopedN(name);                                                    \
	TracyVkZone(cmd->GetTracingContext(), cmd->GetCommandBuffer(), name); \
	const auto lunaCmdTracingZone = cmd->Zone(name)

#undef vkBeginCommandBuffer
#undef vkCmdResetQueryPool
#undef vkCmdWriteTimestamp
#undef vkCreateQueryPool
#undef vkDestroyQueryPool
#undef vkEndCommandBuffer
#undef vkGetPhysicalDeviceProperties
#undef vkGetQueryPoolResults
#undef vkQueueSubmit
#undef vkQueueWaitIdle
