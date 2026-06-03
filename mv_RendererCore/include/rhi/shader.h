#ifndef _MV_SHADER_H_
#define _MV_SHADER_H_

#include "util/types.h"

namespace mv
{
	namespace rhi
	{
		using namespace types;

		using ShaderHandle = u32;

		enum class EShaderType
		{
			eVertex = 0,
			eGeometory,
			eFragment,

			eCompute,

			eTask,
			eMesh,
		};
	}
}

#endif