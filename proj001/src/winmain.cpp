
#include "Windows.h"

#include "engine.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	try
	{
		Engine engine;

		if (!engine.initialize(hInstance, nCmdShow))
			return -1;

		engine.run();

		engine.deinitialize();

	}
	catch (const std::exception& e)
	{
		MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
		return -1;
	}
	catch (...)
	{
		MessageBoxA(nullptr, "Unknown error", "Error", MB_OK | MB_ICONERROR);
		return -2;
	}

	return 0;
}
