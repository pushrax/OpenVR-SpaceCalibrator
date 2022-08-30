#include <iostream>
#include "compat.h"
#include <unordered_map>

std::unordered_map<void*, void*> functionMap;


void MessageBox(void *, wchar_t const * msg, wchar_t const *  title, int /* trash */ ){
    std::wcerr << "Messagebox: " << title << ": " << msg << std::endl;
}

/*
void MessageBox(void *, char* msg, char * title, int ){
    std::cerr << "Messagebox: " << title << ": " << msg << std::endl;
} */


int MH_Initialize(){
    return 0;
}

char const * MH_StatusToString(int err){
    switch(err){
    case 0:
        return "No Error";
    default:
        return "Unkonwn";
    }
}

void MH_Uninitialize()
{
}

void MH_RemoveHook(void* /* thing */){
}
int MH_EnableHook(void* /* thing */){
    return 1;
}
int MH_CreateHook(void* /* a */, void* /* b */, LPVOID* /* fun */){
    return 1;
}

