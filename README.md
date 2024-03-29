# Learning WebGPU

https://github.com/eliasdaler/webgpu-learning/assets/1285136/e2855490-6f60-46e4-be9c-5b3e61cf51ff

## Building

I'm using [SDL](https://github.com/libsdl-org/SDL) for windows/input and [Google Dawn](https://github.com/google/dawn) as a WebGPU backend. Everything is built from source.

### Install system deps (Linux)

(TODO: probably incomplete list)

```sh
sudo apt install libx11-xcb-dev
```

### Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Status of WebGPU support in browsers on Linux

* Firefox Nightly (123.0) - kinda works, but WGSL support seems incomplete (e.g. `override` doesn't work)
* Chrome Unstable (122.0.6182.0) - on Linux WebGPU only works when launched with `--enable-features=Vulkan,UseSkiaRenderer --enable-unsafe-webgpu`

## Progress

19.12.23 - first triangle

![triangle](screenshots/1_triangle.png)

20.12.23 - textured quad

![textured_quad](screenshots/2_textured_quad.png)

20.01.24 - model

![model](screenshots/3_model.png)

22.01.24 - simple Blinn-Phong lighting

![blinn_phong](screenshots/4_blinn_phong.png)

25.01.24 - loading complex glTF scenes

![gltf_scene_loading](screenshots/5_gltf_scene_loading.png)

02.02.24 - skeletal animation

https://github.com/eliasdaler/webgpu-learning/assets/1285136/e2855490-6f60-46e4-be9c-5b3e61cf51ff


