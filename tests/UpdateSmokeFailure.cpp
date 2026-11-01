#include <Windows.h>

// A valid executable that fails startup, without triggering Windows loader dialogs.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 23; }
