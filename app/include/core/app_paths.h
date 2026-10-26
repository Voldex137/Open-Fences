#pragma once

#include <string>

// Where the app keeps its data. The installed build uses %APPDATA%\Open Fences.
// The portable build is recognised by a file named "portable.flag" next to the
// exe: everything then lives in "data" beside it, so the folder can be moved
// between PCs or run from a USB stick without touching the system.
namespace AppPaths {

bool IsPortable();

// Folder of the running exe (no trailing backslash).
std::wstring ExeDir();

// Settings folder: "<exe dir>\data" (portable) or "%APPDATA%\Open Fences".
std::wstring DataDir();

}  // namespace AppPaths
