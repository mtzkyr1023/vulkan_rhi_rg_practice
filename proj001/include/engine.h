
#ifndef _ENGINE_H_
#define _ENGINE_H_

#include "rhi/rhi.h"
#include "rg/render_graph.h"

#include "Windows.h"

class Engine
{
public:

	bool initialize(HINSTANCE hInstance, int nCmdShow);
	void deinitialize();

	void run();


private:
	bool initializeWindow(HINSTANCE hInstance, int nCmdShow);
	bool initializeRHI(void* hwnd);

	void deinitializeWindow();
	void deinitializeRHI();

	void tick();

private:
	std::shared_ptr<mv::rhi::IRHI> rhi_;

	HWND hwnd_ = nullptr;
};


#endif
