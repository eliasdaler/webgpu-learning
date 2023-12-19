#include "Game.h"

#include "OSUtil.h"

#include <SDL.h>

#include <cassert>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
std::filesystem::path getExecutableDir() {
#ifdef _WIN32
  std::wstring buf;
  buf.resize(MAX_PATH);
  do {
    unsigned int len = GetModuleFileNameW(
        NULL, &buf[0], static_cast<unsigned int>(buf.size()));
    if (len < buf.size()) {
      buf.resize(len);
      break;
    }

    buf.resize(buf.size() * 2);
  } while (buf.size() < 65536);

  return std::filesystem::path(buf).parent_path();
#else
  if (std::filesystem::exists("/proc/self/exe")) {
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
  }
  return std::filesystem::path();
#endif
}
} // end of anonymous namespace

void Game::Params::validate() {
  assert(screenWidth > 0);
  assert(screenHeight > 0);
}

void Game::start(Params params) {
  params.validate();
  this->params = params;

  init();
  loop();
  cleanup();
}

void Game::init() {
  util::setCurrentDirToExeDir();
  std::filesystem::current_path(getExecutableDir());

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
    printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
    std::exit(1);
  }

  window = SDL_CreateWindow(params.windowTitle.c_str(),
                            // pos
                            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            // size
                            params.screenWidth, params.screenHeight, 0);

  if (!window) {
    printf("Couldn't create window! SDL Error: %s\n", SDL_GetError());
    std::exit(1);
  }
}

void Game::loop() {
  // Fix your timestep! game loop
  const float FPS = 60.f;
  const float dt = 1.f / FPS;

  uint32_t prev_time = SDL_GetTicks();
  float accumulator = dt; // so that we get at least 1 update before render

  isRunning = true;
  while (isRunning) {
    uint32_t new_time = SDL_GetTicks();
    const auto frame_time = (new_time - prev_time) / 1000.f;
    accumulator += frame_time;
    prev_time = new_time;

    if (accumulator > 10 * dt) { // game stopped for debug
      accumulator = dt;
    }

    while (accumulator >= dt) {
      { // event processing
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
          if (event.type == SDL_QUIT) {
            quit();
          }
        }
      }

      // update
      update(dt);

      accumulator -= dt;
    }

    render();

    // Delay to not overload the CPU
    const auto frameTime = (SDL_GetTicks() - prev_time) / 1000.f;
    if (dt > frameTime) {
      SDL_Delay(dt - frameTime);
    }
  }
}

void Game::update(float dt) {}

void Game::render() {}

void Game::quit() { isRunning = false; }

void Game::cleanup() {
  SDL_DestroyWindow(window);

  SDL_Quit();
}
