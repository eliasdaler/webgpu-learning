/**
 * This is an extension of SDL2 for WebGPU, abstracting away the details of
 * OS-specific operations.
 *
 * Changes from sdl2webpu:
 *  1. Rewriting it to be proper C++ (can't take pointer to rvalue ref).
 *     For now, only X11 is supported.
 *
 * MIT License
 * Copyright (c) 2023 Elias Daler
 * Copyright (c) 2022-2023 Elie Michel and the wgpu-native authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "util/SDLWebGPU.h"

#include <webgpu/webgpu_cpp.h>

#if defined(SDL_VIDEO_DRIVER_COCOA)
#include <Cocoa/Cocoa.h>
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

wgpu::Surface SDL_GetWGPUSurface(const wgpu::Instance& instance, SDL_Window* window)
{
    SDL_SysWMinfo windowWMInfo;
    SDL_VERSION(&windowWMInfo.version);
    SDL_GetWindowWMInfo(window, &windowWMInfo);

#if defined(SDL_VIDEO_DRIVER_COCOA)
    static_assert(false, "untested");
    /* {
        id metal_layer = NULL;
        NSWindow* ns_window = windowWMInfo.info.cocoa.window;
        [ns_window.contentView setWantsLayer:YES];
        metal_layer = [CAMetalLayer layer];
        [ns_window.contentView setLayer:metal_layer];
        return wgpuInstanceCreateSurface(
            instance,
            &(WGPUSurfaceDescriptor){
                .label = NULL,
                .nextInChain =
                    (const WGPUChainedStruct*)&(WGPUSurfaceDescriptorFromMetalLayer){
                        .chain =
                            (WGPUChainedStruct){
                                .next = NULL,
                                .sType = WGPUSType_SurfaceDescriptorFromMetalLayer,
                            },
                        .layer = metal_layer,
                    },
            });
    } */
#elif defined(SDL_VIDEO_DRIVER_X11)
    {
        Display* x11_display = windowWMInfo.info.x11.display;
        Window x11_window = windowWMInfo.info.x11.window;

        wgpu::SurfaceDescriptorFromXlibWindow x11SurfDesc;
        x11SurfDesc.display = x11_display;
        x11SurfDesc.window = x11_window;

        wgpu::SurfaceDescriptor surfaceDesc;
        surfaceDesc.nextInChain = &x11SurfDesc;
        return instance.CreateSurface(&surfaceDesc);
    }
#elif defined(SDL_VIDEO_DRIVER_WAYLAND)
    static_assert(false, "untested");
    /* {
        struct wl_display* wayland_display = windowWMInfo.info.wl.display;
        struct wl_surface* wayland_surface = windowWMInfo.info.wl.display;
        return wgpuInstanceCreateSurface(
            instance,
            &(WGPUSurfaceDescriptor){
                .label = NULL,
                .nextInChain =
                    (const WGPUChainedStruct*)&(WGPUSurfaceDescriptorFromWaylandSurface){
                        .chain =
                            (WGPUChainedStruct){
                                .next = NULL,
                                .sType = WGPUSType_SurfaceDescriptorFromWaylandSurface,
                            },
                        .display = wayland_display,
                        .surface = wayland_surface,
                    },
            });
    } */
#elif defined(SDL_VIDEO_DRIVER_WINDOWS)
    static_assert(false, "untested");
    /* {
        HWND hwnd = windowWMInfo.info.win.window;
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUSurfaceDescriptorFromWindowsHWND windowDesc{
            .chain =
                WGPUChainedStruct{
                    .next = NULL,
                    .sType = WGPUSType_SurfaceDescriptorFromWindowsHWND,
                },
            .hinstance = hinstance,
            .hwnd = hwnd,
        };
        WGPUSurfaceDescriptor surfaceDesc{
            .nextInChain = (const WGPUChainedStruct*)&windowDesc,
            .label = NULL,
        };
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    } */
#else
    // TODO: See SDL_syswm.h for other possible enum values!
#error "Unsupported WGPU_TARGET"
#endif
}
