#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>

WGPUSurface SDL_GetWGPUSurface(WGPUInstance instance, SDL_Window *window);
