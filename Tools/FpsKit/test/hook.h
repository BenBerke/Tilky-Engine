// Force-included into private copies of the engine files that open windows, read
// input or read the clock, so the play test runs hidden, with fed input, at a
// fixed 60 frames per second.
#include <SDL3/SDL.h>
#undef SDL_WINDOW_MAXIMIZED
#define SDL_WINDOW_MAXIMIZED SDL_WINDOW_HIDDEN
void HarnessBeforeSwap(SDL_Window* window);
#define SDL_GL_SwapWindow(w) (HarnessBeforeSwap(w), SDL_GL_SwapWindow(w))
#define SDL_ShowWindow(w) (true)
#define SDL_RaiseWindow(w) (true)
const bool* HarnessKeyboardState(int* numkeys);
#define SDL_GetKeyboardState(n) HarnessKeyboardState(n)
SDL_MouseButtonFlags HarnessMouseState(float* x, float* y);
#define SDL_GetMouseState(x, y) HarnessMouseState(x, y)
SDL_MouseButtonFlags HarnessRelativeMouseState(float* x, float* y);
#define SDL_GetRelativeMouseState(x, y) HarnessRelativeMouseState(x, y)
#define SDL_GetGlobalMouseState(x, y) HarnessMouseState(x, y)
Uint64 HarnessCounter();
#define SDL_GetPerformanceCounter() HarnessCounter()
#define SDL_GetPerformanceFrequency() (Uint64{60})
SDL_Window* HarnessWindow();
#define SDL_GetMouseFocus() HarnessWindow()
