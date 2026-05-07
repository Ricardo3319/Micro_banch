---
description: "Use when writing or running commands, scripts, tooling setup, or file operations for Windows 11 environments. Covers PowerShell 5.1 syntax, Windows path rules, and safe command behavior. Also useful for Win11, PowerShell, path, terminal, and script preferences."
name: "Windows 11 Host Rules"
---
# Windows 11 Host Rules

- Default assumption: host OS is Windows 11 unless the user explicitly says otherwise.
- Preferred terminal style: use PowerShell 5.1 command syntax and cmdlets before Bash-style commands.
- In PowerShell examples, prefer `;` for sequential commands instead of `&&`.
- Prefer PowerShell-native commands when practical, such as `Get-ChildItem`, `Get-Content`, `Set-Location`, and `Test-Path`.
- Use Windows path conventions in examples and commands (`C:\path\to\file`), and quote paths that contain spaces.
- For cross-platform guidance, provide Windows command examples first, then optional Linux/macOS variants.
- For script snippets, default to `.ps1` unless the user requests another shell.
- For process management, prefer PowerShell patterns (`Get-Process`, `Stop-Process`) and include alternatives if Unix-only tools are mentioned.
- Prefer safe operations by default. For risky or destructive commands, explain impact and request confirmation.
- Keep command snippets copy-paste ready for Windows Terminal and VS Code integrated terminal.

## Win11 Chinese Notes / Win11 中文说明

- 默认认为主机为 Windows 11，除非用户明确说明其他系统。
- 终端命令优先使用 PowerShell 5.1 语法与 cmdlet。
- 在 PowerShell 示例中，顺序执行命令优先使用 `;`，不使用 `&&`。
- 优先给出 Windows 路径写法（如 `C:\path\to\file`），包含空格的路径要加引号。
- 涉及多平台时，先给 Windows 命令，再补充 Linux/macOS 可选写法。
- 脚本默认提供 `.ps1` 示例，除非用户要求其他 shell。
- 涉及风险操作时先说明影响，再请求确认。

## Quick Patterns

- Directory listing: `Get-ChildItem -Force`
- Search text: `Select-String -Path .\* -Pattern "keyword" -Recurse`
- Environment variable read: `$env:PATH`
- Set variable for current session: `$env:MY_VAR = "value"`
- Check command exists: `Get-Command git`
