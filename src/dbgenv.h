#pragma once
// Debug switches (FFL3_FORCEFX etc.). Read from the environment, but the tester's F10 commands
// and test mode set them at run time through setDbgEnv. On Windows a runtime SetEnvironment-
// Variable (what SDL_setenv does) never reaches the C runtime's getenv, so everything reads
// through here instead.
#include <cstdlib>
#include <map>
#include <string>

inline std::map<std::string, std::string>& dbgEnvTable() { static std::map<std::string, std::string> t; return t; }
inline void setDbgEnv(const char* name, const std::string& v) { dbgEnvTable()[name] = v; }
inline const char* dbgEnv(const char* name) {
    auto& t = dbgEnvTable();
    auto it = t.find(name);
    if (it != t.end()) return it->second.c_str();
    return std::getenv(name);
}
