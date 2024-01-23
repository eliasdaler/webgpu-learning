This is a single file example of drawing a triangle using Dawn and WebGPU.

It's useful for reproducing bugs / errors / problems that you might encounter along the way. Having it all in a single file also helps you to see how it all fits together.

I use glfw which Dawn brings along because it has a useful `wgpu::glfw::CreateSurfaceForWindow` function so that the surface creation is simplified compared to what you have to do when using SDL.
