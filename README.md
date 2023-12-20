# Learning webgpu

Following this: https://eliemichel.github.io/LearnWebGPU/

![triangle](screenshots/1_triangle.png)

Expect a cute animated cat to appear if successful.

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
