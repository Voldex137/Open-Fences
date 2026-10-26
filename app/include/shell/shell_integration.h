#pragma once

#include <string>

// Entries in Explorer's own right-click menus (classic menu, "Show more options"
// on Windows 11), written under HKCU so no admin rights are needed:
//   * desktop background  -> "Create panel"
//   * any file or folder  -> "Move to panel" with the list of existing panels
// They start the exe with a command line (--new-fence / --move-to <id> <path>);
// a second process forwards the command to the running instance and exits.
namespace ShellIntegration {

// The user's switch (config.json, tray menu). On by default for the installed
// build, off for the portable one (it must not leave anything in the registry).
bool Enabled();
void SetEnabled(bool on);

// Rewrites the registry entries to match Enabled() and the current panels
// (titles, ids) and language; removes them when disabled. Cheap, call freely.
void Refresh();

// Removes every entry (uninstall: "--unregister-shell").
void Unregister();

// Command line of this process: handles the registry-entry commands. Returns
// true when the command line carried an instruction (the caller forwards it to
// the running instance, or runs it itself once started).
struct Command { std::wstring text; };
bool ParseCommandLine(Command& out);   // false: a normal start
bool ForwardToRunningInstance(const Command& cmd);
void Execute(const std::wstring& text); // inside the running instance

}  // namespace ShellIntegration
