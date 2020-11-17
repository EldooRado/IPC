#include "stdafx.h"
#include <map>

#define WM_MYMSG	WM_USER + 1

constexpr size_t BUFF_SIZE = 256;

std::map<HWND, WNDPROC> mapCallbacks;
HWND hMainWnd;
int createdWindowsCount = 0;

//declarations
LRESULT CALLBACK cbSelfDefinedMsgSwitch(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK cbAttachWndProcToAppropriateWindow(HWND hCurrWnd, LPARAM lParam);

//local functions
BOOL createFileMappingNameFromHash(CHAR* hash, size_t sizeOfHash, CHAR* sharedFileNameBuffer)
{
	CHAR prefix[] = "Global\\";


	if (sizeof(prefix) - 1 + sizeof(hash) > BUFF_SIZE)
	{
		return false;
	}

	strncpy_s(sharedFileNameBuffer, BUFF_SIZE, prefix, sizeof(prefix) - 1);
	strncpy_s(sharedFileNameBuffer + sizeof(prefix) - 1, BUFF_SIZE - sizeof(prefix) + 1, hash, sizeOfHash);

	return true;
}

BOOL getMsgFromSharedMemory(CHAR* sharedMemoryName, WCHAR* lpMessage)
{
	HANDLE hMapFile = ::OpenFileMappingA(
		FILE_MAP_ALL_ACCESS,   // read/write access
		FALSE,                 // do not inherit the name
		sharedMemoryName);               // name of mapping object

	if (hMapFile == NULL)
		return false;

	LPWSTR pBuf = (LPWSTR) ::MapViewOfFile(hMapFile, // handle to map object
		FILE_MAP_ALL_ACCESS,  // read/write permission	
		0,
		0,
		BUFF_SIZE);

	if (pBuf == NULL)
	{
		::CloseHandle(hMapFile);
		return false;
	}

	wcscpy_s(lpMessage, BUFF_SIZE, pBuf);

	::UnmapViewOfFile(pBuf);
	::CloseHandle(hMapFile);

	return true;
}

VOID attachWndProcToEachChildren(HWND hParentWnd)
{
	HWND hCurrWnd = GetWindow(hParentWnd, GW_CHILD);
	if (hCurrWnd != NULL)
	{
		do {
			mapCallbacks[hCurrWnd] = (WNDPROC) ::SetWindowLongPtrW(hCurrWnd, GWLP_WNDPROC, (LONG_PTR)cbSelfDefinedMsgSwitch);

			attachWndProcToEachChildren(hCurrWnd);
			hCurrWnd = ::GetWindow(hCurrWnd, GW_HWNDNEXT);

		} while (hCurrWnd != NULL);
	}
}

VOID initialize()
{
	::EnumWindows(cbAttachWndProcToAppropriateWindow, ::GetCurrentProcessId());
}

VOID deinitialize()
{
	for (std::pair<HWND, WNDPROC> callback : mapCallbacks) {
		::SetWindowLongPtrW(callback.first, GWLP_WNDPROC, (LONG_PTR)callback.second);
	}
}

//callbacks
BOOL CALLBACK cbAttachWndProcToAppropriateWindow(HWND hCurrWnd, LPARAM lParam)
{
	DWORD lpdwProcessId;
	::GetWindowThreadProcessId(hCurrWnd, &lpdwProcessId);
	if (lpdwProcessId == lParam && ::IsWindowVisible(hCurrWnd))
	{
		hMainWnd = hCurrWnd;
		mapCallbacks[hMainWnd] = (WNDPROC)SetWindowLongPtrW(hMainWnd, GWLP_WNDPROC, (LONG_PTR)cbSelfDefinedMsgSwitch);
		attachWndProcToEachChildren(hMainWnd);
		return FALSE;
	}
	return TRUE;
}

LRESULT CALLBACK cbSelfDefinedMsgSwitch(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_LBUTTONDOWN:
	{
		::SetWindowPos(hMainWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
		::SetWindowPos(hMainWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
		::MessageBoxW(hMainWnd, L"WM_COMMAND", L"Window Info", MB_OK);
		break;
	}
	case WM_MYMSG:
	{
		::SetWindowPos(hMainWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
		::SetWindowPos(hMainWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);

		WCHAR* caption = (WCHAR*)L"Error";
		WCHAR* message = (WCHAR*)L"Failed to get className";

		//buffers
		WCHAR classNameBuffer[MAX_CLASS_NAME + 1];
		CHAR sharedFileNameBuffer[BUFF_SIZE];
		WCHAR messageBuffer[BUFF_SIZE];
		
		if (::GetClassNameW(hWnd, classNameBuffer, MAX_CLASS_NAME) > 0)
		{
			caption = classNameBuffer;
		}

		DWORD wparamLparam[] = { static_cast<DWORD>(wParam), static_cast<DWORD>(lParam) };
		CHAR* hash = (CHAR*)wparamLparam;

		if (createFileMappingNameFromHash(hash, sizeof(wparamLparam), sharedFileNameBuffer))
		{
			if (getMsgFromSharedMemory(sharedFileNameBuffer, messageBuffer))
			{
				message = messageBuffer;
			}
		}

		if (::_wcsicmp(caption, L"edit") == 0)
		{
			//if there is edit box, then insert message to edit filed
			//::MessageBoxW(hWnd, message, L"to je edit", MB_OK);
			SetWindowTextW(hWnd, message);
		}
		else
		{
			//::MessageBoxW(hWnd, message, caption, MB_OK);
			HWND hButton = CreateWindowW(L"edit", message, WS_BORDER | WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL, 10, 170 + 100*createdWindowsCount++, 300, 50, hMainWnd, (HMENU)102, 0, 0);
		}
		
		break;
	}
	case WM_CLOSE:
	case WM_DESTROY:
		::PostQuitMessage(0);
		break;
	}
	return ::CallWindowProcW(mapCallbacks[hWnd], hWnd, uMsg, wParam, lParam);
}

//exported functions
extern "C" __declspec(dllexport)
VOID __cdecl invokeMessageBox(HWND hWnd)
{
	::MessageBoxW(hWnd, L"Message from dll", L"Elo", MB_OK);
}