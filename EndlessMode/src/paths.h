#pragma once

#include <Windows.h>
#include <string>

// Folder containing this DLL (the game's scripts\ folder).
inline std::string ScriptsDir() {
    static std::string dir = [] {
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&ScriptsDir, &self);
        char path[MAX_PATH];
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string s(path);
        return s.substr(0, s.find_last_of("\\/"));
    }();
    return dir;
}
