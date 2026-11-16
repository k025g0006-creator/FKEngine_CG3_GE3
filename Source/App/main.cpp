#include <Windows.h>
#include "Game.h"

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    Game game;
    return game.Run();
}
