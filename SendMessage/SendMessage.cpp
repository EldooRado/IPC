// SendMessage.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <psapi.h>

#define WM_MYMSG	WM_USER + 1

constexpr size_t BUFF_SIZE = 256;

HWND g_hMainWnd = NULL;
std::vector<HWND> g_vecHwnd;
size_t g_hwndIndex = 0;

std::string GetLastErrorAsString()
{
	//Get the error message, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return std::string(); //No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}


BOOL createFileMappingNameFromHash(CHAR* hash, size_t sizeOfHash, CHAR* sharedFileNameBuffer)
{
	CHAR prefix[] = "Global\\";
	if (sizeof(prefix) - 1 + sizeof(hash) > BUFF_SIZE)
	{
		std::cout << "Invalid prefix" << std::endl;
		return false;
	}

	strncpy_s(sharedFileNameBuffer, BUFF_SIZE, prefix, sizeof(prefix) - 1);
	strncpy_s(sharedFileNameBuffer + sizeof(prefix) - 1, BUFF_SIZE - sizeof(prefix) + 1, hash, sizeOfHash);

	return true;
}

VOID unmapSharedFile(HANDLE hMapFile, LPWSTR pBuf)
{
	::UnmapViewOfFile(pBuf);

	::CloseHandle(hMapFile);
}
BOOL mapSharedFile(HANDLE &hMapFile, LPWSTR &pBuf, LPCSTR sharedObjectName)
{	
	hMapFile = ::CreateFileMappingA(
		INVALID_HANDLE_VALUE,    // use paging file
		NULL,                    // default security
		PAGE_READWRITE,          // read/write access
		0,                       // maximum object size (high-order DWORD)
		BUFF_SIZE,                // maximum object size (low-order DWORD)
		sharedObjectName);                 // name of mapping object

	if (hMapFile == NULL) 
		return false;

	pBuf = (LPWSTR) ::MapViewOfFile(hMapFile,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		BUFF_SIZE);

	if (pBuf == NULL)
	{
		::CloseHandle(hMapFile);
		return false;
	}

	return true;
}

HMODULE getModuleHandle(HANDLE processHandle, const wchar_t* moduleFileName)
{
	HMODULE hMods[1024];
	DWORD cbNeeded;
	if (::EnumProcessModules(processHandle, hMods, sizeof(hMods), &cbNeeded))
	{
		for (size_t i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
		{
			WCHAR szModName[MAX_PATH];

			// Get the full path to the module's file.
			DWORD length = ::GetModuleFileNameExW(processHandle, hMods[i], szModName, sizeof(szModName) / sizeof(WCHAR));
			if (length > 0)
			{
				if (wcscmp(moduleFileName, szModName) == 0)
				{
					return hMods[i];
				}
			}
		}
	}
	return NULL;
}

BOOL isProcessX32(HANDLE processHandle, bool& isProcessX32)
{
	SYSTEM_INFO sysInfo;
	::GetNativeSystemInfo(&sysInfo);

	if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_UNKNOWN)
	{
		std::cout << "Unknown processor" << std::endl;
		return false;
	}

	BOOL isWin64 = sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
		sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ||
		sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64;

	if (!isWin64)
	{
		isProcessX32 = true;
		return true;
	}

	//If IsWow64Process() reports true, the process is 32-bit running on a 64-bit OS.
	BOOL isWow64Proc = false;
	if (!::IsWow64Process(processHandle, &isWow64Proc))
	{
		std::cout << "Failed to check if is 64 Error: " << GetLastErrorAsString() << std::endl;
		return false;
	}

	if (isWow64Proc)
	{
		isProcessX32 = true;
	}
	else
	{
		isProcessX32 = false;
	}
	return true;
}

BOOL is_main_window(HWND handle)
{
	return GetWindow(handle, GW_OWNER) == (HWND)0 && IsWindowVisible(handle);
}

VOID ejectDll(HANDLE hProcess, HMODULE hLibModule)
{
	HMODULE hKernel32 = ::GetModuleHandle(L"Kernel32");
	
	LPTHREAD_START_ROUTINE freeLibraryProcAddress =
		(LPTHREAD_START_ROUTINE) ::GetProcAddress(hKernel32, "FreeLibrary");

	HANDLE hThread = ::CreateRemoteThread(hProcess, NULL, 0,
		freeLibraryProcAddress, (void*)hLibModule, 0, NULL);

	::WaitForSingleObject(hThread, INFINITE);

	// Clean up
	::CloseHandle(hThread);
}

DWORD injectDll(HANDLE hProcess, LPWSTR lpLibPath, size_t libPathSize)
{
	// Base address of loaded module (==HMODULE);
	DWORD hLibModule;
	HMODULE hKernel32 = ::GetModuleHandleW(L"Kernel32");

	size_t libPathLength = libPathSize / sizeof(WCHAR);

	if (libPathLength > MAX_PATH)
	{
		std::cout << "Incorrect dll path" << std::endl;
		return NULL;
	}
	WCHAR libPathBuffer[MAX_PATH];
	wcsncpy_s(libPathBuffer, lpLibPath, libPathLength);

	// 1. Allocate memory in the remote process for szLibPath
	// 2. Write szLibPath to the allocated memory
	VOID* pRemoteLibPathAddress = ::VirtualAllocEx(hProcess, NULL, libPathSize,
		MEM_COMMIT, PAGE_READWRITE);

	::WriteProcessMemory(hProcess, pRemoteLibPathAddress, (VOID*)lpLibPath,
		libPathSize, NULL);

	// Load DLL into the remote process
	// (via CreateRemoteThread & LoadLibrary)

	LPTHREAD_START_ROUTINE loadLibraryWProcAddress = 
		(LPTHREAD_START_ROUTINE) ::GetProcAddress(hKernel32,"LoadLibraryW");

	HANDLE hThread = ::CreateRemoteThread(hProcess, NULL, 0, 
		loadLibraryWProcAddress, pRemoteLibPathAddress, 0, NULL);

	if (hThread == NULL)
	{
		std::cerr << "Error. Can't createRemoteThread. Code: " << ::GetLastErrorAsString() << std::endl;
		return NULL;
	}
	
	::WaitForSingleObject(hThread, INFINITE);

	// Get handle of the loaded module
	::GetExitCodeThread(hThread, &hLibModule);

	// Clean up
	::CloseHandle(hThread);
	::VirtualFreeEx(hProcess, pRemoteLibPathAddress, libPathSize, MEM_RELEASE);
	return hLibModule;
}

int findProcessHwnd(LPWSTR processName)
{
	int pid = 0;
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (::Process32FirstW(snapshot, &entry) == TRUE)
	{
		while (::Process32NextW(snapshot, &entry) == TRUE)
		{
			if (_wcsicmp(entry.szExeFile, processName) == 0)
			{
				HANDLE hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);

				pid = entry.th32ProcessID;

				::CloseHandle(hProcess);
			}
		}
	}

	::CloseHandle(snapshot);
	return pid;
}

VOID iterByEachChildren(HWND parentHwnd)
{
	HWND hCurrWnd = ::GetWindow(parentHwnd, GW_CHILD);
	if (hCurrWnd != NULL)
	{
		do {
			WCHAR classNameBuffer[MAX_CLASS_NAME + 1];
			if (::GetClassNameW(hCurrWnd, classNameBuffer, MAX_CLASS_NAME) <= 0)
			{
				std::cout << "Failed to get class Name from: " << hCurrWnd << std::endl;
				return;
			}
			g_vecHwnd.push_back(hCurrWnd);
			std::wcout << 1+g_hwndIndex++ << ": HWND: " << hCurrWnd << "\tClassName: " << classNameBuffer << std::endl;

			hCurrWnd = ::GetWindow(hCurrWnd, GW_HWNDNEXT);

		} while (hCurrWnd != NULL);
	}
}

BOOL CALLBACK enumWindowsProcMy(HWND hwnd, LPARAM lParam)
{
	DWORD lpdwProcessId;
	::GetWindowThreadProcessId(hwnd, &lpdwProcessId);
	if (lpdwProcessId == lParam && is_main_window(hwnd))
	{
		g_hMainWnd = hwnd;
		std::cout << "windows found" << std::endl;
		return FALSE;
	}
	return TRUE;
}

std::wstring s2ws(const std::string& str)
{
	int cchWideChar = ::MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(cchWideChar, 0);
	::MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], cchWideChar);
	return wstrTo;
}

int main(int argc, char* argv[])
{
	std::string szProcessName;
	if (argc > 2)
	{
		std::cout << "SendMessage.exe <process_to_invade> or SendMessage.exe" << std::endl;
		return -2;
	}
	else if (argc == 2)
	{
		szProcessName = std::string(argv[1]);
	}
	else
	{
		std::cout << "Hello. Type a process name, where you want inject and send messages" << std::endl;
		std::cin >> szProcessName;
	}

	std::wstring wzProcessName = s2ws(szProcessName);
	
	int pid = ::findProcessHwnd((LPWSTR)wzProcessName.c_str());
	if (pid == 0)
	{
		std::cout << "getting processhandle failed";
		return -1;
	}

	::EnumWindows(enumWindowsProcMy, pid);

	std::cout << "SUCCESS!" << std::endl;

	
	WCHAR classNameBuffer[MAX_CLASS_NAME + 1];
	if (::GetClassNameW(g_hMainWnd, classNameBuffer, MAX_CLASS_NAME) <= 0)
	{
		std::cout << "Failed to get class Name from: " << g_hMainWnd << std::endl;
		return -1;
	}

	
	g_vecHwnd.push_back(g_hMainWnd);
	std::wcout << 1 + g_hwndIndex++ <<": HWND: " << g_hMainWnd << "\tClassName: " << classNameBuffer<<std::endl;

	iterByEachChildren(g_hMainWnd);
	HWND currHwnd = ::GetWindow(g_hMainWnd, GW_CHILD);
	
	HANDLE hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, TRUE, pid);

	if (hProcess == NULL)
	{
		std::cout << "Failed to open process:" << pid <<" Error: " << ::GetLastErrorAsString() << std::endl;
		return -1;
	}

	//check if current proc isX32
	bool isCurrentProcX32 = false;
	if (!isProcessX32(::GetCurrentProcess(), isCurrentProcX32))
	{
		std::cout << "isProcessX32 problem:" << pid << " Error: " << ::GetLastErrorAsString() << std::endl;
		return -1;
	}

	//check if process is x32
	bool isInjectedProcX32 = false;
	if (!isProcessX32(hProcess, isInjectedProcX32))
	{
		std::cout << "isProcessX32 problem:" << pid << " Error: " << ::GetLastErrorAsString() << std::endl;
		return -1;
	}

	if ((isCurrentProcX32 && !isInjectedProcX32) || (!isCurrentProcX32 && isInjectedProcX32))
	{
		std::cout << "Incompatible processes!" << std::endl;
		return -1;
	}

	DWORD hInjectedDllDword;
	WCHAR * currModuleHandle;
	//if injected dll is in another sollution, the you need specify paths exlicitly
	/*WCHAR libPathBuffer[] = L"C:\\Users\\admin\\source\\repos\\IPC\\Debug\\AddWndProc.dll";
	WCHAR libPathBuffer64[] = L"C:\\Users\\admin\\source\\repos\\IPC\\x64\\Debug\\AddWndProc.dll";
	if (isInjectedProcX32)
	{
		currModuleHandle = libPathBuffer;
		hInjectedDllDword = injectDll(hProcess, (LPWSTR)libPathBuffer, sizeof(libPathBuffer));
	}
	else
	{
		currModuleHandle = libPathBuffer64;
		hInjectedDllDword = injectDll(hProcess, (LPWSTR)libPathBuffer64, sizeof(libPathBuffer64));
	}*/

	const WCHAR* libName = L"AddWndProc.dll";
	WCHAR libPathBuffer[MAX_PATH];
	WCHAR* lastPathPart;

	if (!::GetFullPathNameW(libName, MAX_PATH, libPathBuffer, &lastPathPart))
	{
		std::cout << "Failed to get moldule full path. Error:" << ::GetLastErrorAsString() << std::endl;
		return -1;
	}

	currModuleHandle = libPathBuffer;
	hInjectedDllDword = injectDll(hProcess, (LPWSTR)libPathBuffer, sizeof(libPathBuffer));

	if (hInjectedDllDword == NULL)
	{
		std::cerr << "Error. hModule is null. Code: " << ::GetLastErrorAsString() << std::endl;
		return NULL;
	}

	HMODULE hModule;

#ifndef _WIN64
	hModule = (HMODULE)hInjectedDllDword;
#else
	//we have only low dword for qword HMODULE. Try to get whole QWORD
	hModule = getModuleHandle(hProcess, libPathBuffer);

	if (hModule == NULL)
	{
		std::cout << "Failed to get module handle. Error:" << ::GetLastErrorAsString() << std::endl;
		return -1;
	}

	DWORD hModuleDword = *(DWORD*) &hModule;
	if (hInjectedDllDword != NULL && hInjectedDllDword != hModuleDword)
	{
		std::cout << "Module handles are incompatible. Error:" << ::GetLastErrorAsString() << std::endl;
		return -1;
	}
#endif
	std::cout << "\nPick the index of HWDN to send message. To exit type \"exit\"" << std::endl;

	HANDLE hMapFile;
	LPWSTR pBuf;

	//create a name of sharedFile
	CHAR someHashToSend[] = "!@#$%^&";
	if (sizeof(someHashToSend) != 2*sizeof(DWORD))
	{
		std::cout << "Invalid hash size" << std::endl;
		return -1;
	}

	CHAR sharedFileNameBuffer[BUFF_SIZE];
	if (!createFileMappingNameFromHash(someHashToSend, sizeof(someHashToSend), sharedFileNameBuffer)) 
	{
		std::cout << "createFileMappingNameFromHash problem" << std::endl;
		return -1;
	}

	DWORD wparam = *(DWORD*)someHashToSend;
	DWORD lparam = *(DWORD*)(someHashToSend +sizeof(DWORD));
	//end of: create a name of sharedFile

	if (!mapSharedFile(hMapFile, pBuf, sharedFileNameBuffer))
	{
		std::cout << "Failed to map shared file. Error:" << ::GetLastErrorAsString() << std::endl;
		return -1;
	}

	while (true)
	{
		std::string inputString;
		std::cout << "Choose a window:" << std::endl;
		std::cin >> inputString;
		if (inputString.compare("exit") == 0)
		{
			break;
		}

		size_t index = std::atoi(inputString.c_str());
		if (index == 0 || index > g_vecHwnd.size())
		{
			std::cout << "There is no such a window" << std::endl;
			continue;
		}
		std::cout << "Type a EditBox message:" << std::endl;
		
		std::cin.ignore();
		std::getline(std::cin, inputString);

		std::wstring wMsg(inputString.begin(), inputString.end());

		RtlCopyMemory((PVOID)pBuf, wMsg.data(), sizeof(wchar_t)*wMsg.size());
		::SendMessageW(g_vecHwnd[index-1], WM_MYMSG, (WPARAM)wparam, (LPARAM)lparam);
	}

	unmapSharedFile(hMapFile, pBuf);
	ejectDll(hProcess, hModule);

	return 0;
}