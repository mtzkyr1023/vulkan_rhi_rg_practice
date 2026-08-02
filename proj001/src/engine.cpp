
#include "engine.h"

#include "rg/render_graph.h"

LRESULT CALLBACK WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);

bool Engine::initialize(HINSTANCE hInstance, int nCmdShow)
{
	if (!initializeWindow(hInstance, nCmdShow))
		return false;

	if (!initializeRHI(hwnd_))
		return false;

	return true;
}

void Engine::deinitialize()
{
	deinitializeRHI();
	deinitializeWindow();
}

void Engine::run()
{

    MSG msg{};
    while (true)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
		
		if (msg.message == WM_QUIT)
		{
			break;
		}
        else
        {
            tick();
        }
    }
}

bool Engine::initializeWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"RenderGraphSample";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"RenderGraph Sample",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        720,
        nullptr,
        nullptr,
        hInstance,
        this);

    if (!hwnd_)
    {
        return false;
    }

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

	return true;
}

bool Engine::initializeRHI(void* hwnd)
{
	rhi_ = mv::rhi::IRHI::createVulkanRHI();
	rhi_->initialize(hwnd);

	return true;
}


void Engine::deinitializeWindow()
{
	if (hwnd_)
	{
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}

void Engine::deinitializeRHI()
{
	if (rhi_)
	{
		rhi_->deinitialize();
		rhi_ = nullptr;
	}
}

void Engine::tick()
{
	mv::rhi::FrameContext context = rhi_->beginFrame();

	mv::rg::RenderGraph rg(rhi_);

	mv::rg::RGTextureHandle backbuffer = rg.importTexture("Backbuffer", context.backbuffer, mv::rg::ERGTextureUsage::Undefined);

	rg.addPass(
		{
			.name = "Clear Pass",
			.setup = [&](mv::rg::Builder& builder)
			{
				builder.accessTexture(backbuffer, mv::rg::ERGTextureUsage::ColorAttachment);
			},
			.execute = [&](mv::rg::Context& context)
			{
				float clearColor[4] = { 0.2f, 0.3f, 0.3f, 1.0f };
				context.rhi()->clearRenderTarget(clearColor);
			}
		}
	);

	rg.compile();

	rg.execute();

	rhi_->endFrame();
}


LRESULT CALLBACK WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}