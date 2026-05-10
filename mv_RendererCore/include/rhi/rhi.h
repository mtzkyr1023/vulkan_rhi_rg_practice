
#ifndef _MV_RHI_H_
#define _MV_RHI_H_

#include "memory"

#include "util/types.h"

#include "rhi/resource.h"

namespace mv
{
	namespace rhi
	{
		class ICommandBuffer;

		class IRHI
		{
		public:
			virtual ~IRHI() {}

			virtual void initialize(void* hwnd) = 0;
			virtual void deinitialize() = 0;

			virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
			virtual TextureHandle createTexture(const TextureDesc& desc) = 0;

			virtual ICommandBuffer* createCommandBuffer() = 0;

			virtual void submit(ICommandBuffer* commandbuffer) = 0;
		};

		class VulkanRHI : public IRHI
		{
		public:
			VulkanRHI();
			~VulkanRHI();

			void initialize(void* hwnd) override;
			void deinitialize() override;

			BufferHandle createBuffer(const BufferDesc& desc) override;
			TextureHandle createTexture(const TextureDesc& desc) override;

			ICommandBuffer* createCommandBuffer() override;

			void submit(ICommandBuffer* commandbuffer) override;

		private:
			struct Impl;
			std::shared_ptr<Impl> impl_;
		};
	}
}

#endif