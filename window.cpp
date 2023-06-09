//
// Created by drook207 on 12.03.2023.
//
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <cstdio>          // printf, fprintf
#include <cstdlib>         // abort

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "window.h"

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

//#define IMGUI_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define IMGUI_VULKAN_DEBUG_REPORT
#endif


namespace engine {

    static const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    static void glfw_error_callback(int error, const char *description) {
        fprintf(stderr, "GLFW Error %d: %s\n", error, description);
    }

    static void check_vk_result(VkResult err) {
        if (err == 0)
            return;
        fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
        if (err < 0)
            abort();
    }

#ifdef IMGUI_VULKAN_DEBUG_REPORT
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix; // Unused arguments
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // IMGUI_VULKAN_DEBUG_REPORT

    void window::setupVulkan() {

        // Create Vulkan Instance
        {
            uint32_t extensions_count = 0;
            const char **extensions = glfwGetRequiredInstanceExtensions(&extensions_count);

            VkInstanceCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            create_info.enabledExtensionCount = extensions_count;
            create_info.ppEnabledExtensionNames = extensions;
#ifdef IMGUI_VULKAN_DEBUG_REPORT
            // Enabling validation layers
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;

        // Enable debug report extension (we need additional storage, so we duplicate the user array to add our new extension to it)
        const char** extensions_ext = (const char**)malloc(sizeof(const char*) * (extensions_count + 1));
        memcpy(extensions_ext, extensions, extensions_count * sizeof(const char*));
        extensions_ext[extensions_count] = "VK_EXT_debug_report";
        create_info.enabledExtensionCount = extensions_count + 1;
        create_info.ppEnabledExtensionNames = extensions_ext;

        // Create Vulkan Instance
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
        free(extensions_ext);

        // Get the function pointer (required for any extensions)
        auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(vkCreateDebugReportCallbackEXT != NULL);

        // Setup the debug report callback
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = NULL;
        err = vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
#else
            // Create Vulkan Instance without any debug feature
            m_err = vkCreateInstance(&create_info, m_allocator, &m_instance);
            check_vk_result(m_err);
            IM_UNUSED(m_debugReport);
#endif
        }

        // Select GPU
        {
            uint32_t gpu_count;
            m_err = vkEnumeratePhysicalDevices(m_instance, &gpu_count, nullptr);
            check_vk_result(m_err);
            IM_ASSERT(gpu_count > 0);

            auto *gpus = (VkPhysicalDevice *) malloc(sizeof(VkPhysicalDevice) * gpu_count);
            m_err = vkEnumeratePhysicalDevices(m_instance, &gpu_count, gpus);
            check_vk_result(m_err);

            // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
            // most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
            // dedicated GPUs) is out of scope of this sample.
            int use_gpu = 0;
            for (int i = 0; i < (int) gpu_count; i++) {
                VkPhysicalDeviceProperties properties;
                vkGetPhysicalDeviceProperties(gpus[i], &properties);
                if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    use_gpu = i;
                    break;
                }
            }

            m_physicalDevice = gpus[use_gpu];
            free(gpus);
        }

        // Select graphics queue family
        {
            uint32_t count;
            vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, nullptr);
            auto *queues = (VkQueueFamilyProperties *) malloc(sizeof(VkQueueFamilyProperties) * count);
            vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, queues);
            for (uint32_t i = 0; i < count; i++)
                if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    m_queueFamily = i;
                    break;
                }
            free(queues);
            IM_ASSERT(m_queueFamily != (uint32_t) -1);
        }

        // Create Logical Device (with 1 queue)
        {
            int device_extension_count = 1;
            const char *device_extensions[] = {"VK_KHR_swapchain"};
            const float queue_priority[] = {1.0f};
            VkDeviceQueueCreateInfo queue_info[1] = {};
            queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info[0].queueFamilyIndex = m_queueFamily;
            queue_info[0].queueCount = 1;
            queue_info[0].pQueuePriorities = queue_priority;
            VkDeviceCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
            create_info.pQueueCreateInfos = queue_info;
            create_info.enabledExtensionCount = device_extension_count;
            create_info.ppEnabledExtensionNames = device_extensions;
            m_err = vkCreateDevice(m_physicalDevice, &create_info, m_allocator, &m_device);
            check_vk_result(m_err);
            vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
        }

        // Create Descriptor Pool
        {
            VkDescriptorPoolSize pool_sizes[] =
                    {
                            {VK_DESCRIPTOR_TYPE_SAMPLER,                1000},
                            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000},
                            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000},
                            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000},
                            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000},
                            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000},
                            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000},
                            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000}
                    };
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
            pool_info.poolSizeCount = (uint32_t) IM_ARRAYSIZE(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            m_err = vkCreateDescriptorPool(m_device, &pool_info, m_allocator, &m_descriptorPool);
            check_vk_result(m_err);
        }
    }


    // All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
// Your real engine/app may not use them.
    void window::setupVulkanWindow() {
        m_wd->Surface = m_surface;

        // Check for WSI support
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, m_wd->Surface, &res);
        if (res != VK_TRUE) {
            fprintf(stderr, "Error no WSI support on physical device 0\n");
            exit(-1);
        }

        // Select Surface Format
        const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                                      VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        m_wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(m_physicalDevice, m_wd->Surface,
                                                                    requestSurfaceImageFormat,
                                                                    (size_t) IM_ARRAYSIZE(requestSurfaceImageFormat),
                                                                    requestSurfaceColorSpace);

        // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
        VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
        m_wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_physicalDevice, m_wd->Surface, &present_modes[0],
                                                                IM_ARRAYSIZE(present_modes));
        //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(m_minImageCount >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physicalDevice, m_device, m_wd, m_queueFamily, m_allocator,
                                               m_width, m_height, m_minImageCount);
    }

    void window::cleanupVulkan() {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, m_allocator);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
        // Remove the debug report callback
    auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // IMGUI_VULKAN_DEBUG_REPORT

        vkDestroyDevice(m_device, m_allocator);
        vkDestroyInstance(m_instance, m_allocator);
    }

    void window::cleanupVulkanWindow() {
        ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_mainWindowData, m_allocator);
    }

    void window::frameRender() {


        VkSemaphore image_acquired_semaphore = m_wd->FrameSemaphores[m_wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = m_wd->FrameSemaphores[m_wd->SemaphoreIndex].RenderCompleteSemaphore;
        m_err = vkAcquireNextImageKHR(m_device, m_wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE,
                                      &m_wd->FrameIndex);
        if (m_err == VK_ERROR_OUT_OF_DATE_KHR || m_err == VK_SUBOPTIMAL_KHR) {
            m_swapChainRebuild = true;
            return;
        }
        check_vk_result(m_err);

        ImGui_ImplVulkanH_Frame *fd = &m_wd->Frames[m_wd->FrameIndex];
        {
            m_err = vkWaitForFences(m_device, 1, &fd->Fence, VK_TRUE,
                                    UINT64_MAX);    // wait indefinitely instead of periodically checking
            check_vk_result(m_err);

            m_err = vkResetFences(m_device, 1, &fd->Fence);
            check_vk_result(m_err);
        }
        {
            m_err = vkResetCommandPool(m_device, fd->CommandPool, 0);
            check_vk_result(m_err);
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            m_err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
            check_vk_result(m_err);
        }
        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = m_wd->RenderPass;
            info.framebuffer = fd->Framebuffer;
            info.renderArea.extent.width = m_wd->Width;
            info.renderArea.extent.height = m_wd->Height;
            info.clearValueCount = 1;
            info.pClearValues = &m_wd->ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(m_mainDrawData, fd->CommandBuffer);

        // Submit command buffer
        vkCmdEndRenderPass(fd->CommandBuffer);
        {
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &image_acquired_semaphore;
            info.pWaitDstStageMask = &wait_stage;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->CommandBuffer;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &render_complete_semaphore;

            m_err = vkEndCommandBuffer(fd->CommandBuffer);
            check_vk_result(m_err);
            m_err = vkQueueSubmit(m_queue, 1, &info, fd->Fence);
            check_vk_result(m_err);
        }
    }

    void window::framePresent() {
        if (m_swapChainRebuild)
            return;
        VkSemaphore render_complete_semaphore = m_wd->FrameSemaphores[m_wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &m_wd->Swapchain;
        info.pImageIndices = &m_wd->FrameIndex;
        m_err = vkQueuePresentKHR(m_queue, &info);
        if (m_err == VK_ERROR_OUT_OF_DATE_KHR || m_err == VK_SUBOPTIMAL_KHR) {
            m_swapChainRebuild = true;
            return;
        }
        check_vk_result(m_err);
        m_wd->SemaphoreIndex =
                (m_wd->SemaphoreIndex + 1) % m_wd->ImageCount; // Now we can use the next set of semaphores
    }

    int window::create() {
        glfwSetErrorCallback(glfw_error_callback);
        if (!glfwInit())
            return 1;

        // Create window with Vulkan context
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_pWindow = glfwCreateWindow(m_width, m_height, "Dear ImGui GLFW+Vulkan example", nullptr, nullptr);
        if (!glfwVulkanSupported()) {
            printf("GLFW: Vulkan Not Supported\n");
            return 1;
        }

        setupVulkan();

        // Create Window Surface

        m_err = glfwCreateWindowSurface(m_instance, m_pWindow, m_allocator, &m_surface);
        check_vk_result(m_err);

        // Create Framebuffers
        int w, h;
        glfwGetFramebufferSize(m_pWindow, &w, &h);
        m_wd = &m_mainWindowData;
        setupVulkanWindow();

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void) io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
        //io.ConfigViewportsNoAutoMerge = true;
        //io.ConfigViewportsNoTaskBarIcon = true;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(m_pWindow, true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_instance;
        init_info.PhysicalDevice = m_physicalDevice;
        init_info.Device = m_device;
        init_info.QueueFamily = m_queueFamily;
        init_info.Queue = m_queue;
        init_info.PipelineCache = m_pipelineCache;
        init_info.DescriptorPool = m_descriptorPool;
        init_info.Subpass = 0;
        init_info.MinImageCount = m_minImageCount;
        init_info.ImageCount = m_wd->ImageCount;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = m_allocator;
        init_info.CheckVkResultFn = check_vk_result;
        ImGui_ImplVulkan_Init(&init_info, m_wd->RenderPass);

        // Load Fonts
        // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
        // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
        // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
        // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
        // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
        // - Read 'docs/FONTS.md' for more instructions and details.
        // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
        //io.Fonts->AddFontDefault();
        //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
        //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
        //IM_ASSERT(font != NULL);

        // Upload Fonts
        {
            // Use any command queue
            VkCommandPool command_pool = m_wd->Frames[m_wd->FrameIndex].CommandPool;
            VkCommandBuffer command_buffer = m_wd->Frames[m_wd->FrameIndex].CommandBuffer;

            m_err = vkResetCommandPool(m_device, command_pool, 0);
            check_vk_result(m_err);
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            m_err = vkBeginCommandBuffer(command_buffer, &begin_info);
            check_vk_result(m_err);

            ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

            VkSubmitInfo end_info = {};
            end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            end_info.commandBufferCount = 1;
            end_info.pCommandBuffers = &command_buffer;
            m_err = vkEndCommandBuffer(command_buffer);
            check_vk_result(m_err);
            m_err = vkQueueSubmit(m_queue, 1, &end_info, VK_NULL_HANDLE);
            check_vk_result(m_err);

            m_err = vkDeviceWaitIdle(m_device);
            check_vk_result(m_err);
            ImGui_ImplVulkan_DestroyFontUploadObjects();
        }


        return 0;
    }

    void window::cleanup() {

        // Cleanup
        m_err = vkDeviceWaitIdle(m_device);
        check_vk_result(m_err);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        cleanupVulkanWindow();
        cleanupVulkan();

        glfwDestroyWindow(m_pWindow);
        glfwTerminate();

    }

/**
 * @brief Main update loop
 */
    void window::update() {
        // Main loop
        while (!glfwWindowShouldClose(m_pWindow)) {
            // Poll and handle events (inputs, window resize, etc.)
            // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
            // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
            // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
            // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
            glfwPollEvents();

            // Resize swap chain?
            if (m_swapChainRebuild) {
                int width, height;
                glfwGetFramebufferSize(m_pWindow, &width, &height);
                if (width > 0 && height > 0) {
                    ImGui_ImplVulkan_SetMinImageCount(m_minImageCount);
                    ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physicalDevice, m_device, &m_mainWindowData,
                                                           m_queueFamily, m_allocator, width, height, m_minImageCount);
                    m_mainWindowData.FrameIndex = 0;
                    m_swapChainRebuild = false;
                }
            }

            // Start the Dear ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();


            if (m_onUpdateCallback != nullptr) {
                m_onUpdateCallback();
            }

            // Rendering
            ImGui::Render();
            m_mainDrawData = ImGui::GetDrawData();
            const bool main_is_minimized = (m_mainDrawData->DisplaySize.x <= 0.0f ||
                                            m_mainDrawData->DisplaySize.y <= 0.0f);
            m_wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            m_wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            m_wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            m_wd->ClearValue.color.float32[3] = clear_color.w;
            if (!main_is_minimized)
                frameRender();
            ImGuiIO &io = ImGui::GetIO();
            (void) io;
            // Update and Render additional Platform Windows
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            // Present Main Platform Window
            if (!main_is_minimized)
                framePresent();
        }

    }

    window::window(int width, int height) :
            m_width(width), m_height(height) {


    }

    /**
     * @brief Registers a callback that gets invoked every frame
     * @param cb Pointer to callback function withing the user content gets created
     */
    void window::registerOnUpdateCallback(const std::function<void()> &cb) {

        if (cb != nullptr) {
            m_onUpdateCallback = cb;
        }

    }


} // game
