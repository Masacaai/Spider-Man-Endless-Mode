#pragma once

#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
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

// Where the mod keeps its settings and records: <Documents>\Marvel's Spider-Man Remastered\EndlessMode\.
// NOT scripts\: Overstrike wipes and recreates scripts\ every time "Install mods" is pressed (that reset the ini, the dock
// positions and every record, 2026-10-16). Falls back to scripts\ if Documents can't be found.
inline std::string DataDir() {
    static std::string dir = [] {
        char docs[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs)) && docs[0]) {
            std::string d = std::string(docs) + "\\Marvel's Spider-Man Remastered\\EndlessMode";
            std::error_code ec;
            std::filesystem::create_directories(d, ec);
            if (!ec) return d;
        }
        return ScriptsDir();
    }();
    return dir;
}

// One-time move of data a previous version kept in scripts\ (copies only what is missing in the data folder).
inline void MigrateDataFromScripts() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (DataDir() == ScriptsDir()) return;
    for (const char* name : { "EndlessMode.ini", "EndlessMode_horde_best.txt", "EndlessMode_horde_scores.csv",
                              "EndlessMode_campaign_best.txt", "EndlessMode_loadout.txt" })
        if (fs::exists(fs::path(ScriptsDir()) / name, ec) && !fs::exists(fs::path(DataDir()) / name, ec))
            fs::copy_file(fs::path(ScriptsDir()) / name, fs::path(DataDir()) / name, ec);
    fs::path runs = fs::path(ScriptsDir()) / "EndlessMode_runs";
    if (fs::exists(runs, ec) && !fs::exists(fs::path(DataDir()) / "EndlessMode_runs", ec))
        fs::copy(runs, fs::path(DataDir()) / "EndlessMode_runs", fs::copy_options::recursive, ec);
}
