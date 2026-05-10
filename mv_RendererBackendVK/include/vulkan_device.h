
#ifndef _MV_VULKAN_RENDERER_H_
#define _MV_VULKAN_RENDERER_H_

#include "windows.h"

#include <vector>

#include "vulkan/vulkan.h"

#include "renderer.h"

namespace mv
{
	namespace renderer
	{
		class VulkanRenderer : public IRenderer
		{
		public:
			bool initialize(void* hwnd) override;
			void deinitialize() override;

			void shutdown() override;

			void render() override;

		private:
			void createInstance();
			void createSurface(void* hwnd);
			void pickPhysicalDevice();
			void createDevice();
			void createSwapChain(void* hwnd);
			void createCommandSystem();
			void createSyncObjects();

			void beginFrame();
			void endFrame();
			void present();

		private:
			VkInstance instance_ = VK_NULL_HANDLE;
			VkSurfaceKHR surface_ = VK_NULL_HANDLE;

			VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
			VkDevice device_ = VK_NULL_HANDLE;

			VkQueue graphicsQueue_ = VK_NULL_HANDLE;
			uint32_t graphicsQueueFamilyIndex_ = -1;

			VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
			VkSwapchainKHR oldSwapchain_ = VK_NULL_HANDLE;
			std::vector<VkImage> swapchainImages_;
			std::vector<VkImageView> swapchainImageViews_;

			struct FrameContext
			{
				VkCommandPool commandPool = VK_NULL_HANDLE;

				std::vector<VkCommandBuffer> commandBuffers;

				std::vector<VkSemaphore> imageAvailableSemaphores;
				std::vector<VkSemaphore> renderFinishedSemaphores;
				std::vector<VkFence> inFlightFences;
			};

			std::vector<FrameContext> frames_;
		};
	}
}

#endif
