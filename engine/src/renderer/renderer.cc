#include "renderer.hh"
#include "core/application.hh"
#include "renderer/vkcommon.hh"
#include "renderer/debug.hh"
#include "renderer/vkdevice.hh"
#include "renderer/vkpipeline.hh"
#include "renderer/vkswapchain.hh"
#include "renderer/vkmodel.hh"

#include <cstdint>
#include <stdexcept>
#include <vulkan/vulkan_core.h>
#include <stdlib.h>

// Constructor for the renderer 
// and startup behavior
Renderer::Renderer(std::string name, std::string assetPath, uint32_t width, uint32_t height, Platform& platform)
    : m_title(name), 
    m_assetPath(assetPath),
    m_width(width), 
    m_height(height),
    m_platform(platform),
    m_timer{}
{
    m_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    m_vkparams.Allocator = nullptr;
    m_framecounter = 0;
}

// Init behavior
void
Renderer::OnInit() {
    InitVulkan();
    SetupPipeline();
}

// Update behavior
void
Renderer::OnUpdate() {
    m_timer.Tick(nullptr);

    // Update FPS and framecount
    snprintf(m_lastFPS, static_cast<size_t>(32), "%u fps", m_timer.GetFPS());
    m_framecounter++;
}

// Set the window's title text
const std::string 
Renderer::GetWindowTitle() {
    std::string device(m_deviceProperties.deviceName);
    std::string windowTitle;
    windowTitle = m_title + " - " + device + " - " + std::string(m_lastFPS);
    return windowTitle;
}

// Resize behavior
void
Renderer::WindowResize(uint32_t w, uint32_t h) {
    if (!m_initialized)
        return;

    m_initialized = false;

    // Ensure all operations on GPU are done before destroying resources
    vkDeviceWaitIdle(m_vkparams.Device.Device);

    // Recreate swapchain
    m_width = w;
    m_height = h;
    CreateSwapchain(&m_width, &m_height, false);

    // Recreate the framebuffers
    for (size_t i = 0; i < m_graphics.Framebuffers.size(); i++) {
        vkDestroyFramebuffer(
                m_vkparams.Device.Device,
                m_graphics.Framebuffers[i],
                m_vkparams.Allocator);
    }
    CreateFrameBuffers();

    // Command buffers need to be recreated as they may store
    // references to the recreated framebuffer
    vkFreeCommandBuffers(
            m_vkparams.Device.Device,
            m_graphics.GraphicsCommandPool,
            static_cast<uint32_t>(m_graphics.GraphicsCommandBuffers.size()),
            m_graphics.GraphicsCommandBuffers.data());

    AllocateCommandBuffers();

    vkDeviceWaitIdle(m_vkparams.Device.Device);
    m_initialized = true;
}

// Render the scene
void
Renderer::OnRender() {
    // Get the index of the next available image in the swapchain
    uint32_t imageIndex;
    VkResult acquire = vkAcquireNextImageKHR( // acquires the next image in the swapchain
            m_vkparams.Device.Device, 
            m_vkparams.SwapChain.Handle, 
            UINT64_MAX,
            m_graphics.ImageAvailableSemaphore,
            nullptr,
            &imageIndex);
    if (!((acquire == VK_SUCCESS) || (acquire == VK_SUBOPTIMAL_KHR))) {
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
            WindowResize(m_width, m_height);
        else
            VK_CHECK(acquire);
    }

    PopulateCommandBuffer(m_command_buffer_index, imageIndex);
    SubmitCommandBuffer(m_command_buffer_index);
    PresentImage(imageIndex);

    // Wait for the GPU to complete the frame before continuing is best practice
    // vkQueueWaitIdle is used for simplicity
    // (so that we can reuse the command buffer indexed with m_command_buffer_index)
    // THIS IS SUBOPTIMAL because we are waiting on GPU to complete 1 image at a time 
    // before the CPU creates anther. We can use a fence or semaphores later on
    // to do better synchronization, but for now this works fine
    VK_CHECK(vkQueueWaitIdle(m_vkparams.GraphicsQueue.Handle)); // "wait for GPU to idle"

    // Update the command buffer
    m_command_buffer_index = (m_command_buffer_index + 1) % m_command_buffer_count;
}

void
Renderer::PopulateCommandBuffer(uint64_t bufferIndex, uint64_t imgIndex) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.pNext = nullptr;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // We use a single color attachment that is cleared at the start of the sub pass
    VkClearValue clearValues[1];
    clearValues[0].color = {
        {0.0f, 0.2f, 0.4f, 1.0f},
    };

    VkRenderPassBeginInfo beginRenderpassInfo = {};
    beginRenderpassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginRenderpassInfo.pNext = nullptr;
    // set the render area that is affected by the render pass instance
    beginRenderpassInfo.renderArea.offset.x = 0;
    beginRenderpassInfo.renderArea.offset.y = 0;
    beginRenderpassInfo.renderArea.extent.width = m_width;
    beginRenderpassInfo.renderArea.extent.height = m_height;
    // Set clear values for all framebuffer attachments with loadOp set to clear
    beginRenderpassInfo.clearValueCount = 1;
    beginRenderpassInfo.pClearValues = clearValues;
    // Set the renderpass object used to begin an instance of
    beginRenderpassInfo.renderPass = m_graphics.RenderPass;
    // Set the framebuffer to specify the color attachment (render target) where to draw the current frame
    beginRenderpassInfo.framebuffer = m_graphics.Framebuffers[imgIndex];

    // Puts the command buffer into a recording state
    // ONE_TIME_SUBMIT means each recording of the command buffer will
    // be submitted once, and the command buffer will be reset and rerecorded
    // between each submission.
    VK_CHECK(vkBeginCommandBuffer(
                m_graphics.GraphicsCommandBuffers[bufferIndex],
                &beginInfo));

    // Begin the render pass instance
    // This will clear the color attachment
    // The render pass provides the actual image views for the attachment descriptors
    // After beginnign the render pass, command buffers are ready to record the commands
    // for the first subpass of that render pass.
    // The application can record the commands one subpass at a time (if the render pass
    // is composed of multiple subpasses) before ending the render pass instance.
    vkCmdBeginRenderPass(
            m_graphics.GraphicsCommandBuffers[bufferIndex],
            &beginRenderpassInfo,
            VK_SUBPASS_CONTENTS_INLINE);

    // Update the dynamic viewport state
    // Defines rectangular area withing the framebuffer that rendering operations
    // will be mapped to.
    VkViewport viewport = {};
    viewport.height = static_cast<float>(m_height);
    viewport.width = static_cast<float>(m_width);
    viewport.minDepth = static_cast<float>(0.0f);
    viewport.maxDepth = static_cast<float>(1.0f);
    vkCmdSetViewport(
            m_graphics.GraphicsCommandBuffers[bufferIndex],
            0,
            1,
            &viewport);

    // Update dynaic scissor state
    // Scissor defines a rectangular area withing the framebuffer where rendering
    // operations will be restricted.
    VkRect2D scissor = {};
    scissor.extent.width = m_width;
    scissor.extent.height = m_height;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    vkCmdSetScissor(
            m_graphics.GraphicsCommandBuffers[bufferIndex],
            0,
            1,
            &scissor);

    // Bind the graphics pipeline
    m_pipeline->Bind(m_graphics.GraphicsCommandBuffers[bufferIndex]);
    // vkCmdBindPipeline(m_graphics.GraphicsCommandBuffers[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphics.GraphicsPipeline);

    // Bind the triangle vertex buffer (contains position and color)
    m_model->Bind(m_graphics.GraphicsCommandBuffers[bufferIndex]);
    m_model->Draw(m_graphics.GraphicsCommandBuffers[bufferIndex]);

    // Ending the render pass will add an implicit barrier, transitioning the frame buffer
    // color attachment to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system
    vkCmdEndRenderPass(m_graphics.GraphicsCommandBuffers[bufferIndex]);

    VK_CHECK(vkEndCommandBuffer(m_graphics.GraphicsCommandBuffers[bufferIndex]));
}

void
Renderer::SubmitCommandBuffer(uint64_t index) {
    // Pipeline stage at which the queue submission will wait (via a semaphore)
    VkPipelineStageFlags waitStateMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    // The submit info structure specifies a command buffer queue submission batch
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &waitStateMask;                          // pointer to the list of pipeline stages that the semaphore waits will happen at
    submitInfo.waitSemaphoreCount = 1;                                      // one wait semaphore
    submitInfo.signalSemaphoreCount =1;                                     // one signal semaphore
    submitInfo.pCommandBuffers = &m_graphics.GraphicsCommandBuffers[index]; // command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;                                      // one command buffer

    submitInfo.pWaitSemaphores = &m_graphics.ImageAvailableSemaphore;      // semaphore(s) to wait upon before the submitted command buffers begin executing
    submitInfo.pSignalSemaphores = &m_graphics.RenderingFinishedSemaphore; // semaphore(s) to signal when command buffers have been completed

    VK_CHECK(vkQueueSubmit(
                m_vkparams.GraphicsQueue.Handle,
                1,
                &submitInfo,
                VK_NULL_HANDLE));
}

void
Renderer::PresentImage(uint32_t index) {
    // Present current image to the presentation engine
    // Pass the semaphore from the submit info as the wait semaphore for swap chain presentation
    // This ensures that the image is not presented to the windowing system until all commands have been executed
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_vkparams.SwapChain.Handle;
    presentInfo.pImageIndices = &index;

    // Check if a wait semaphore has been specified to wait for before presenting the image
    if (m_graphics.RenderingFinishedSemaphore != VK_NULL_HANDLE) {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_graphics.RenderingFinishedSemaphore;
    }

    VkResult present = vkQueuePresentKHR(m_vkparams.GraphicsQueue.Handle, &presentInfo);
    if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) {
        if (present == VK_ERROR_OUT_OF_DATE_KHR)
            WindowResize(m_width, m_height);
        else 
            VK_CHECK(present)
    }
}


// Destroy in reverse order of creation
void
Renderer::OnDestroy() {
    std::cout << "Destroying renderer" << std::endl;
    m_initialized = false;

    // Ensure all operations on the device have finished before destroying resources
    vkDeviceWaitIdle(m_vkparams.Device.Device);

    // Destroy vertex buffer object and deallocate backing memory
    std::cout << "Destroying vertex buffer and memory...";
    m_model->Destroy();
    std::cout << "destroyed & freed" << std::endl;

    // Destroy pipeline layout and pipeline layout objects
    std::cout << "Destroying pipeline layout and graphics pipeline...";
    m_pipeline->Destroy();
    std::cout << "destroyed" << std::endl;

    std::cout << "Destroying Framebuffers... ";
    // Destroy frame buffers
    for (size_t i = 0; i < m_graphics.Framebuffers.size(); i++) {
        vkDestroyFramebuffer(
                m_vkparams.Device.Device,
                m_graphics.Framebuffers[i],
                m_vkparams.Allocator);
    }
    std::cout << "destroyed" << std::endl;

    std::cout << "Destroying Swapchain Images... ";
    // Destroy the swapchain and its images
    for (size_t i = 0; i < m_vkparams.SwapChain.Images.size(); i++) {
        vkDestroyImageView(
                m_vkparams.Device.Device,
                m_vkparams.SwapChain.Images[i].View,
                m_vkparams.Allocator);
    }
    std::cout << "destroyed" << std::endl;

    std::cout << "Destroying Swapchain... ";
    vkDestroySwapchainKHR(
            m_vkparams.Device.Device,
            m_vkparams.SwapChain.Handle,
            m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;

    // Free allocated commad buffers
    std::cout << "Freeing Graphics Command Pool... ";
    vkFreeCommandBuffers(
            m_vkparams.Device.Device,
            m_graphics.GraphicsCommandPool,
            static_cast<uint32_t>(m_graphics.GraphicsCommandBuffers.size()),
            m_graphics.GraphicsCommandBuffers.data());
    std::cout << "freed" << std::endl;

    // Destroy the renderpass
    std::cout << "Destroying Renderpass... ";
    vkDestroyRenderPass(
            m_vkparams.Device.Device,
            m_graphics.RenderPass,
            m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;

    // Destroy semaphores
    std::cout << "Destroying Semaphores... ";
    vkDestroySemaphore(
            m_vkparams.Device.Device,
            m_graphics.ImageAvailableSemaphore,
            m_vkparams.Allocator);
    vkDestroySemaphore(
            m_vkparams.Device.Device,
            m_graphics.RenderingFinishedSemaphore,
            m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;

    // Destroy command pool
    std::cout << "Destroying Command Pool... ";
    vkDestroyCommandPool(
            m_vkparams.Device.Device,
            m_graphics.GraphicsCommandPool,
            m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;

    // Destroy device
    std::cout << "Destroying Device... ";
    vkDestroyDevice(m_vkparams.Device.Device, m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;

    // Destroy surface
    std::cout << "Destroying Surface... ";
    vkDestroySurfaceKHR(m_vkparams.Instance, m_vkparams.PresentationSurface, m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;

    // Destroy debug messenger
    if (Application::settings.enableValidation) {
        std::cout << "Destroying Debug Messenger... ";
        pfnDestroyDebugUtilsMessengerEXT(m_vkparams.Instance, debugUtilsMessenger, m_vkparams.Allocator);
        std::cout << "destroyed" << std::endl;
    }

    m_platform.destroy_window();

    // Destroy vulkan instance
    std::cout << "Destroying Instance... ";
    vkDestroyInstance(m_vkparams.Instance, m_vkparams.Allocator);
    std::cout << "destroyed" << std::endl;
}

void 
Renderer::InitVulkan() {
    CreateInstance();
    CreateSurface();
    CreateDevice();
    GetDeviceQueue(
            m_vkparams.Device.Device, 
            m_vkparams.GraphicsQueue.FamilyIndex, 
            m_vkparams.GraphicsQueue.Handle);
    CreateSwapchain(&m_width, &m_height, Application::settings.enableVsync);
    CreateRenderPass();
    CreateFrameBuffers();
    AllocateCommandBuffers();
    CreateSyncObjects();
}

void
Renderer::SetupPipeline() {
    CreateVertexBuffer();
    CreatePipelineLayout();
    CreatePipelineObjects();
    m_initialized = true;
}

void
Renderer::CreateVertexBuffer() {
    std::vector <VKModel::Vertex> vertices {
        { { 0.0f, 0.25f , 0.0f }, { 1.0f, 0.0f, 0.0f } },     // v0 (red)
    	{ { -0.25f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f } },  // v1 (green)
        { { 0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }    // v2 (blue)
    };

    m_model = std::make_unique<VKModel>(m_vkparams, vertices);
}

// Create the layout for the pipeline
void
Renderer::CreatePipelineLayout() {
    // Create pipeline layout that will be used to create one or more pipeline objects
    VkPipelineLayoutCreateInfo pPipelineCreateInfo = {};
    pPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pPipelineCreateInfo.pNext = nullptr;
    pPipelineCreateInfo.setLayoutCount = 0;
    pPipelineCreateInfo.pSetLayouts = VK_NULL_HANDLE;

    VK_CHECK(
        vkCreatePipelineLayout(m_vkparams.Device.Device, &pPipelineCreateInfo, m_vkparams.Allocator, &m_graphics.PipelineLayout));


}

void 
Renderer::CreatePipelineObjects() {
    VKPipelineConfig pipelineConfig = {};
    m_pipeline = std::make_unique<VKPipeline>(m_vkparams, m_graphics, pipelineConfig);

    m_pipeline->CreateGraphicsPipeline(
            GetAssetsPath()+"/shaders/vert/triangle.vert.spv", 
            GetAssetsPath()+"/shaders/frag/triangle.frag.spv");
}


void 
Renderer::CreateInstance() {
    VkApplicationInfo appinfo = {};
    appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appinfo.pApplicationName = GetTitle();
    appinfo.pEngineName = GetTitle();
    appinfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME
    };

    // TODO: platform specific ext names
#if defined(Q_PLATFORM_LINUX)
    instanceExtensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined (Q_PLATFORM_WINDOWS)
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    // Validation layer ext
    if (Application::settings.enableValidation) {
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Get the supported extensions
    std::cout << "Supported extensions:" << std::endl;
    uint32_t extensionCount = 0;
    std::vector<std::string> extensionNames;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if (extensionCount > 0) {
        std::vector<VkExtensionProperties> supportedExtensions(extensionCount);

        if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, &supportedExtensions.front()) == VK_SUCCESS) {
            for (size_t i = 0; i < supportedExtensions.size(); i++) {
                std::cout << "\t" << supportedExtensions[i].extensionName << std::endl;
                extensionNames.push_back(supportedExtensions[i].extensionName);
            }
        } else {
            printf("vkEnumerateInstanceExtensionProperties did not return VK_SUCCESS\n");
            exit(1);
        }
    }

    // Create the vulkan instance
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pNext = NULL;
    createInfo.pApplicationInfo = &appinfo;

    // Print out required extensions
    std::cout << "Required extensions:" << std::endl;

    // Check that the extensions we need are supported
    if (instanceExtensions.size() > 0) {
        for (size_t i = 0; i < instanceExtensions.size(); i++) {
            std::cout << "\t" << instanceExtensions[i] << std::endl;
            // Output if requested ext is not available
            if (std::find(extensionNames.begin(), extensionNames.end(), instanceExtensions[i]) == extensionNames.end()) {
                printf("Extension %s not found\n", instanceExtensions[i]);
                exit(1);
            }
        }

        // set extension to enable
        createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        createInfo.ppEnabledExtensionNames = instanceExtensions.data();
    }

    // Validation layer setup
    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
    if (Application::settings.enableValidation) {
        // Check if this layer is available at instance level
        uint32_t instanceLayerCount;
        vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
        std::vector <VkLayerProperties> instanceLayerProps(instanceLayerCount);
        vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProps.data());
        bool validationPresent = false;

        for (size_t i = 0; i < instanceLayerProps.size(); i++) {
            if (strcmp(instanceLayerProps[i].layerName, validationLayerName) == 0) {
                validationPresent = true;
                break;
            }
        }

        if (validationPresent) {
            createInfo.enabledLayerCount = 1;
            createInfo.ppEnabledLayerNames = &validationLayerName;
        } else {
            std::cout << "Validation layer VK_LAYER_KHRONOS_validation not present. Validation is disabled" << std::endl;
            exit(1);
        }

        VK_CHECK(vkCreateInstance(&createInfo, m_vkparams.Allocator, &m_vkparams.Instance));

        // Set callback to handle validation
        if (Application::settings.enableValidation)
            setupDebugUtil(m_vkparams.Instance);
    }
}

// Create the window surface
void
Renderer::CreateSurface() {
    // Use platform-specific surface creation function
    m_platform.create_vulkan_surface(m_vkparams);
    std::cout << "Surface created" << std::endl;
}

// Create the swapchain
// NOTE: this is not just used on startup
//       this is also used to recreate the swapchain
//       for events that require it like 
//       window resizing
void
Renderer::CreateSwapchain(uint32_t *width, uint32_t *height, bool vsync) {
    RecreateSwapchain(m_vkparams, width, height, vsync, m_command_buffer_count);
    std::cout << "Swapchain Created" << std::endl;
}

// Creata a renderpass object
void
Renderer::CreateRenderPass() {
    // This will use a single renderpass with one subpass

    // Descriptors for the attachments used by this renderpass
    std::array<VkAttachmentDescription, 1> attachments = {};

    // Color attachment
    attachments[0].format = m_vkparams.SwapChain.Format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Setup attachment references
    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;                                    // attachment 0 is color
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // attachment layout is used as color during the subpass
    
    // Setup a single subpass reference
    VkSubpassDescription subdesc = {};
    subdesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subdesc.colorAttachmentCount = 1;            // subpass uses one color attachment
    subdesc.pColorAttachments = &colorRef;       // reference to the color attachment in slot 0
    subdesc.pDepthStencilAttachment = nullptr;   // (Depth attachments cannot be used by this sample)
    subdesc.inputAttachmentCount = 0;            // Input attachments can be used to sample from contents of a previous subpass
    subdesc.pInputAttachments = nullptr;         // Input attachments not used yet
    subdesc.preserveAttachmentCount = 0;         // Preserved attachments can be used to loop (and preserve) attachments through subpasses
    subdesc.pPreserveAttachments = nullptr;      // (Preserve attachments not used yet) 
    subdesc.pResolveAttachments = nullptr;       // Resolve attachments are resolved at the end of a sub pass and can be used for things like multisampling

    // Setup subpass dependencies
    std::array<VkSubpassDependency, 1> dependencies = {};

    // Setup dependency and add implicit layout transition from final
    // to initial layout for the color attachment
    // (The actual usage layout is preserved through the layout specified in the attachmetn reference)
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_NONE;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    // Create the render pass object
    VkRenderPassCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());  // number of attachments used by this render pass
    createInfo.pAttachments = attachments.data();  // descriptions of attachments used by the render pass
    createInfo.subpassCount = 1;                                             // we only use one subpass for now
    createInfo.pSubpasses = &subdesc;                                        // Description of the subpass we are using
    createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size()); // number of subpass dependencies
    createInfo.pDependencies = dependencies.data();                          // subpass dependencies used by the render pass
    
    VK_CHECK(vkCreateRenderPass(
            m_vkparams.Device.Device,
            &createInfo,
            m_vkparams.Allocator,
            &m_graphics.RenderPass));

    std::cout << "Renderpass Created" << std::endl;
}

void
Renderer::CreateDevice() {
    // Get the physical device
    uint32_t gpuCount = 0;

    // Get number of physical devices
    vkEnumeratePhysicalDevices(m_vkparams.Instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        std::cout << "No device with Vulkan support found" << std::endl;
        exit(1);
    }

    // Enumerate the devices
    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
    VkResult err = vkEnumeratePhysicalDevices(m_vkparams.Instance, &gpuCount, &physicalDevices.front());
    if (err) {
        throw std::runtime_error("Could not enumerate physical devices\n");
    }

    // Select physical device that has a graphics queue
    for (size_t i = 0; i < gpuCount; i++) {
        if (CheckPhysicalDeviceProperties(physicalDevices[i], m_vkparams)) {
            m_vkparams.Device.PhysicalDevice = physicalDevices[i];
            vkGetPhysicalDeviceProperties(m_vkparams.Device.PhysicalDevice, &m_deviceProperties);
            break;
        }
    }

    // Make sure we have a valid physical device
    if (m_vkparams.Device.PhysicalDevice == VK_NULL_HANDLE
        || m_vkparams.GraphicsQueue.FamilyIndex == UINT32_MAX) {
        throw std::runtime_error("Could not select physical device based on chosen properties\n");
    } else {
        vkGetPhysicalDeviceFeatures(m_vkparams.Device.PhysicalDevice, &m_vkparams.Device.DeviceFeatures);
        vkGetPhysicalDeviceMemoryProperties(m_vkparams.Device.PhysicalDevice, &m_vkparams.Device.DeviceMemoryProperties);
    }

    // Desired queues need to be requested upon logical devices
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

    // Array of normalized vector floating point values (between 0 and 1 inclusive)
    // specifying priotities of work to each requested queue.
    // Higher vals mean higher prio with 0.0 being the lowest
    // Within the same device, queues with higher prio may be allotted more 
    // processing time than queues with lower prio
    const float queuePriorities[] = {1.0f};

    // Request a single graphics queue
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_vkparams.GraphicsQueue.FamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;
    queueCreateInfos.push_back(queueInfo);

    // Add swapchain extension
    std::vector <const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Get the list of supported device extensions
    uint32_t extCount = 0;
    std::vector <std::string> supportedDeviceExtensions;
    vkEnumerateDeviceExtensionProperties(m_vkparams.Device.PhysicalDevice, nullptr, &extCount, nullptr);
    if (extCount > 0) {
        std::vector <VkExtensionProperties> extensions(extCount);
        if (vkEnumerateDeviceExtensionProperties(m_vkparams.Device.PhysicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS) {
            for (size_t i = 0; i < extensions.size(); i++) {
                supportedDeviceExtensions.push_back(extensions[i].extensionName);
            }
        }
    }


    // Create the logical device
    if (CreateLogicalDevice(queueCreateInfos, deviceExtensions, supportedDeviceExtensions, m_vkparams) != VK_SUCCESS) {
        throw std::runtime_error("CreateLogicalDevice() could not create vulkan logical device");
    }

    std::cout << "Device created" << std::endl;
}

// Create a framebuffer to attach color to
// We need to create one for each image in the swapchain
void
Renderer::CreateFrameBuffers() {
    VkImageView attachments[1] = {};
    
    VkFramebufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.renderPass = m_graphics.RenderPass;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = attachments;
    createInfo.width = m_width;
    createInfo.height = m_height;
    createInfo.layers = 1;

    // Create a framebuffer for each swapchain image
    m_graphics.Framebuffers.resize(m_vkparams.SwapChain.Images.size());
    for (size_t i = 0; i < m_graphics.Framebuffers.size(); i++) {
        attachments[0] = m_vkparams.SwapChain.Images[i].View;
        VK_CHECK(vkCreateFramebuffer(
                    m_vkparams.Device.Device,
                    &createInfo,
                    m_vkparams.Allocator,
                    &m_graphics.Framebuffers[i]));
    }

    std::cout << "Framebuffers Created: [" << m_graphics.Framebuffers.size() << "]" << std::endl;
}

void
Renderer::AllocateCommandBuffers() {
    if (!m_graphics.GraphicsCommandPool) {
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.pNext = nullptr;
        cmdPoolInfo.queueFamilyIndex = m_vkparams.GraphicsQueue.FamilyIndex;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(m_vkparams.Device.Device, &cmdPoolInfo, m_vkparams.Allocator, &m_graphics.GraphicsCommandPool));
    }

    // Create one command buffer for each image in the swapchain
    m_graphics.GraphicsCommandBuffers.resize(m_command_buffer_count);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_graphics.GraphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_graphics.GraphicsCommandBuffers.size());

    VK_CHECK(vkAllocateCommandBuffers(
                m_vkparams.Device.Device,
                &allocInfo,
                m_graphics.GraphicsCommandBuffers.data()));

    std::cout << "Command Buffers Allocated: [" << m_graphics.GraphicsCommandBuffers.size() << "]" << std::endl;
}

void
Renderer::CreateSyncObjects() {
    // Create semaphores to synchronize acquiring presentable images before
    // rendering and waiting for drawing to be completed before presenting
    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = nullptr;

    // Return an unsignaled semaphore
    VK_CHECK(vkCreateSemaphore(
                m_vkparams.Device.Device,
                &semInfo,
                m_vkparams.Allocator,
                &m_graphics.ImageAvailableSemaphore));

    // Return an unsignaled semaphore
    VK_CHECK(vkCreateSemaphore(
                m_vkparams.Device.Device,
                &semInfo,
                m_vkparams.Allocator,
                &m_graphics.RenderingFinishedSemaphore));

    std::cout << "Sync Objects Created" << std::endl;
    
}


//
// Destroy Vulkan items
//


void
Renderer::DestroyInstance() {
    // TODO: destroy vulkan instance
}


void
Renderer::DestroySurface() {
    // TODO: destroy vulkan surface 
}
