#include "Game.h"

int main()
{
    Game game;
    game.start({
        .screenWidth = 640,
        .screenHeight = 480,
        .windowTitle = "WebGPU test",
    });
}
