# Learning webgpu

Following this: https://eliemichel.github.io/LearnWebGPU/

![triangle](screenshots/1_triangle.png)

Expect a cute animated cat to appear if successful.

## Building

### Install deps (Linux)

(TODO: probably incomplete list)

```sh
sudo apt install libx11-xcb-dev
```

### Build

```sh
git submodule update --init --recursive
mkdir build && cd build
cmake ..
cmake --build .
```
