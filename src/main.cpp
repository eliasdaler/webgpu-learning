#include "Game.h"

int main()
{
    Game game;
    game.start({
        .screenWidth = 1280,
        .screenHeight = 960,
        .windowTitle = "WebGPU test",
    });
}
