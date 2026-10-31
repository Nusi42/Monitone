# Security policy

## Supported versions

Only the latest release receives fixes.

## Reporting a problem

Please do not open a public issue for security problems. Report them
privately through GitHub instead:
[Security > Report a vulnerability](https://github.com/Nusi42/Monitone/security/advisories/new).

Describe what an attacker could do, the Monitone and Windows versions, and how
to reproduce it. You will get an answer within a week. Once a fix is released,
the advisory is published, with credit to you if you want.

## Scope

Monitone runs with the rights of the signed-in user and changes per-program
audio output settings in Windows. Relevant reports include, for example:

- making Monitone load code from an unexpected location (DLL search order,
  the settings DLL, WebView2),
- content in window titles or program names that escapes the settings
  window's Content Security Policy,
- the installers writing outside the per-user install folder.
