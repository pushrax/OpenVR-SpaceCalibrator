#pragma once

#if  !defined(_WIN32) && !defined(_WIN64)

#include <cstdint>

#define WINAPI
typedef void* LPVOID;


typedef struct _OVERLAPPED {
    void * nothing;
} OVERLAPPED;

typedef void* HANDLE;
typedef bool BOOL;
typedef uint32_t DWORD;
typedef OVERLAPPED* LPOVERLAPPED;
typedef unsigned char BYTE;
typedef int LSTATUS;
typedef wchar_t * LPWSTR;

#define MAX_PATH 1024

#define MH_OK 0
#define FALSE false
#define TRUE true

#define ERROR_BROKEN_PIPE 1
#define ERROR_SUCCESS 0
#define INVALID_HANDLE_VALUE nullptr

void MessageBox(void *, wchar_t const * msg, wchar_t const *  title, int trash);

int MH_Initialize();
char const * MH_StatusToString(int err);
void MH_Uninitialize();
void MH_RemoveHook(void* thing);
int MH_EnableHook(void* thing);
int MH_CreateHook(void* a, void* b, LPVOID* fun);

#else
//NOP not needed in windows
#endif
