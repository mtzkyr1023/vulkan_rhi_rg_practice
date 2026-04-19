
#ifndef _MV_RENDERERCORE_H_
#define _MV_RENDERER_H_

#ifdef MV_RENDERERCORE_EXPORTS
#define MV_RENDERERCORE_API __declspec(dllexport)
#else
#define MV_RENDERERCORE_API __declspec(dllimport)
#endif

namespace mv
{
	namespace renderer
	{
		class MV_RENDERERCORE_API IRenderer
		{
		public:
			virtual bool initialize(void* hwnd) = 0;
			virtual void deinitialize() = 0;

			virtual void shutdown() = 0;
			virtual void render() = 0;

		protected:
			virtual ~IRenderer() {}
		};
	}
}

#endif
