#define QD_IMPLEMENTATION
#include "libs/vkh/quickdata.h"

#include <iostream>
#include "game.hpp"

#include <Windows.h>

int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd
)
{
	GameState* state;
	if (!game_init(&state))
		return -1;

	game_main_loop(state);
	game_quit(state);

	return 0;
}
