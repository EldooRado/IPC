#include <windows.h>
#include <cassert>
#include <iostream>

#define ID_SELF_DESTROY_BUTTON 100
#define ID_LISTBOX 101

LRESULT CALLBACK DefaultCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_CLOSE:
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow) {
	
	WNDCLASS wc;
	MSG msg;

	wc = {};
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = DefaultCallback;
	wc.hInstance = hInstance;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszClassName = L"WINAPITest";

	assert(RegisterClass(&wc));

	HWND hWnd = CreateWindow(L"WINAPITest", L"WinAPI Tutorial", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, hInstance, 0);

	HWND hButton = CreateWindow(L"button", L"MyButton", WS_TABSTOP | WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 10, 70, 300, 50, hWnd, (HMENU)ID_SELF_DESTROY_BUTTON, hInstance, 0);
	
	HWND hList = CreateWindowEx(WS_EX_CLIENTEDGE, L"listbox", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | LBS_EXTENDEDSEL, 400, 40, 150, 200, hWnd, (HMENU)ID_LISTBOX, 0, 0);
	SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)L"first");
	SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)L"second");
	SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)L"last");

	while (true) {
		BOOL result = GetMessage(&msg, 0, 0, 0);
		if (result > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else 
		{
			return result;
		}
	}
}