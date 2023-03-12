//
// Created by drook207 on 12.03.2023.
//

#ifndef TICTACTOE_WINDOW_H
#define TICTACTOE_WINDOW_H

#include <functional>
#include "vulkan/vulkan.h"
#include "imgui_impl_vulkan.h"
#include "GLFW/glfw3.h"

namespace engine {


    class window {

    public:
        explicit window(int width = 1024, int height = 768);

        [[nodiscard]] int create();

        void cleanup();

        void update();

        void registerOnUpdateCallback(const std::function<void()> &cb);


    private:

        void setupVulkan();

        void setupVulkanWindow();

        void cleanupVulkan();

        void cleanupVulkanWindow();

        void frameRender();

        void framePresent();

        //Vulkan
        VkAllocationCallbacks *m_allocator = nullptr;
        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        uint32_t m_queueFamily = (uint32_t) -1;
        VkQueue m_queue = VK_NULL_HANDLE;
        VkDebugReportCallbackEXT m_debugReport = VK_NULL_HANDLE;
        VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkResult m_err = VK_NOT_READY;
        VkSurfaceKHR m_surface{};

        //GLFW3
        GLFWwindow *m_pWindow = nullptr;

        //ImGui
        ImGui_ImplVulkanH_Window m_mainWindowData;
        int m_minImageCount = 2;
        bool m_swapChainRebuild = false;
        ImGui_ImplVulkanH_Window *m_wd = nullptr;
        ImDrawData *m_mainDrawData = nullptr;

        //Interns
        int m_width, m_height;
        std::function<void()> m_onUpdateCallback = nullptr;


    };

} // game

#endif //TICTACTOE_WINDOW_H
