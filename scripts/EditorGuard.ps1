# Dot-sourced by the test and editor scripts: Unreal Editor or game processes running this project's file.
# Editors on other projects (copies elsewhere, other games) do not count.
function Get-ProjectEditors([string]$ProjectFile) {
    $wanted = [IO.Path]::GetFullPath($ProjectFile).Replace('\', '/').ToLowerInvariant()
    Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe' OR Name='UnrealEditor-Cmd.exe'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.Replace('\', '/').ToLowerInvariant().Contains($wanted) }
}
