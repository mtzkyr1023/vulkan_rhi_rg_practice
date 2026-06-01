
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
		};
	}
}

#endif