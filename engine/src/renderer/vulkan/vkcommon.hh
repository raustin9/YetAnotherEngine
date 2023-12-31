#pragma once

#include "stdafx.hh"
#include <cstdint>
#include <vulkan/vulkan_core.h>

struct QueueParameters {
    VkQueue Handle;
    uint32_t FamilyIndex;

    QueueParameters() :
        Handle(VK_NULL_HANDLE),
        FamilyIndex(UINT32_MAX) {
    }
};

struct ImageParameters {
    VkImage Handle;
    VkImageView View;
    VkSampler Sampler;
    VkDeviceMemory Memory;

    ImageParameters() :
        Handle(VK_NULL_HANDLE),
        View(VK_NULL_HANDLE),
        Sampler(VK_NULL_HANDLE),
        Memory(VK_NULL_HANDLE) {
    }
};

struct BufferParameters {
    VkBuffer Handle;
    VkDeviceMemory Memory;
    uint32_t Size;
    BufferParameters() :
        Handle(VK_NULL_HANDLE),
        Memory(VK_NULL_HANDLE),
        Size(0) {
    }
};

struct SwapChainParameters {
    VkSwapchainKHR Handle;
    VkFormat Format;
    VkColorSpaceKHR ColorSpace;
    std::vector<ImageParameters> Images;
    VkExtent2D Extent;

    SwapChainParameters() :
        Handle(VK_NULL_HANDLE),
        Format(VK_FORMAT_UNDEFINED),
        ColorSpace(VK_COLORSPACE_SRGB_NONLINEAR_KHR),
        Images(),
        Extent() {
    } 
};

struct VKDeviceParameters {
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    VkPhysicalDeviceProperties PhysicalDeviceProperties;
    VkPhysicalDeviceMemoryProperties DeviceMemoryProperties;
    VkPhysicalDeviceFeatures DeviceFeatures;

    VKDeviceParameters() :
        PhysicalDevice(VK_NULL_HANDLE),
        Device(VK_NULL_HANDLE) {
    }
};

// Holds commonly used fields in vulkan like logical/physical devices,
// command buffers, etc
struct VKCommonParameters {
    VkInstance                    Instance;
    QueueParameters               GraphicsQueue;
    QueueParameters               PresentQueue;
    VkSurfaceKHR                  PresentationSurface;
    SwapChainParameters           SwapChain;
    VkAllocationCallbacks*        Allocator;
    VKDeviceParameters            Device;
   
    // Formerly graphics params
    VkRenderPass                        RenderPass;
    std::vector<VkFramebuffer>          Framebuffers;
    VkCommandPool                       GraphicsCommandPool;
    std::vector<VkCommandBuffer>        GraphicsCommandBuffers;
    VkPipeline                          GraphicsPipeline;
    VkPipelineLayout                    PipelineLayout;
    VkSemaphore                         ImageAvailableSemaphore;
    VkSemaphore                         RenderingFinishedSemaphore;
    VkDescriptorSetLayout               DescriptorSetLayout;
    VkDescriptorPool                    DescriptorPool;
    std::vector<VkDescriptorSet>        DescriptorSets;

    // Constructor
    VKCommonParameters() :
        Instance(VK_NULL_HANDLE),
        GraphicsQueue(),
        PresentQueue(),
        PresentationSurface(VK_NULL_HANDLE),
        SwapChain() ,
        Device() ,
        RenderPass(VK_NULL_HANDLE),
        Framebuffers(),
        GraphicsCommandPool(VK_NULL_HANDLE),
        GraphicsCommandBuffers(),
        GraphicsPipeline(VK_NULL_HANDLE),
        ImageAvailableSemaphore(VK_NULL_HANDLE),
        RenderingFinishedSemaphore(VK_NULL_HANDLE) {
    }
};

std::string errorString(VkResult errorCode);

// Macro to easily check the result of a vulkan api call to make
// sure that we get the correct result
#define VK_CHECK(f)																			                                  \
{																										                      \
    VkResult res = (f);																					                      \
    if (res != VK_SUCCESS)																				                      \
    {																									                      \
        std::cout << "Fatal : VkResult is \"" << errorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
        assert(res == VK_SUCCESS);																		                      \
    }																									                      \
}
