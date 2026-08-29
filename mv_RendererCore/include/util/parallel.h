#ifndef _MV_PARALLEL_H_
#define _MV_PARALLEL_H_

#include <algorithm>
#include <thread>
#include <vector>

#include "util/types.h"

namespace mv
{
	namespace util
	{
		using namespace types;

		// Runs body(i) for i in [0, count), spread across the hardware threads.
		//
		// Strided rather than blocked, because the per-item cost is rarely uniform in the
		// generators that use this: a ray towards the horizon travels much further through
		// the atmosphere than one straight up, and a ridged octave costs more where the
		// domain warp lands it near a lattice edge. Striding averages that out without
		// needing a work queue.
		template <typename Body>
		void parallelFor(u32 count, Body body)
		{
			const u32 threadCount = std::max(1u, std::min(count, std::thread::hardware_concurrency()));

			if (threadCount <= 1)
			{
				for (u32 i = 0; i < count; i++)
				{
					body(i);
				}

				return;
			}

			std::vector<std::thread> threads;
			threads.reserve(threadCount);

			for (u32 t = 0; t < threadCount; t++)
			{
				threads.emplace_back([&, t]()
					{
						for (u32 i = t; i < count; i += threadCount)
						{
							body(i);
						}
					});
			}

			for (auto& thread : threads)
			{
				thread.join();
			}
		}
	}
}

#endif
