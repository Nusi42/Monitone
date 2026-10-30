#pragma once

#include <string>

namespace monitone {

// Opens a file, folder or URL with its default handler. Runs on a short-lived
// STA thread because ShellExecute must not be called from an MTA thread.
void OpenInShell(std::wstring target);

}  // namespace monitone
