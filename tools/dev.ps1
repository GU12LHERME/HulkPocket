<#
.SYNOPSIS
  HulkPocket Windows developer entry point.

.EXAMPLE
  .\tools\dev.ps1 sdk            # fetch + verify pinned x86_context.h
  .\tools\dev.ps1 apk            # debug APK (GLES only, like main CI)
  .\tools\dev.ps1 apk-vk         # debug APK with Vulkan preview (experiment-vulkan-runtime CI)
  .\tools\dev.ps1 compile-db     # compile_commands.json for clangd (after an APK build)
  .\tools\dev.ps1 ci android-smoke xbe-startup-layout   # run workflows in WSL2
  .\tools\dev.ps1 test           # Python unit tests that run on Windows
  .\tools\dev.ps1 doctor         # print tool versions
#>
param(
    [Parameter(Position = 0)][string]$Command = 'help',
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest = @()
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Distro = if ($env:HULK_WSL_DISTRO) { $env:HULK_WSL_DISTRO } else { 'Ubuntu-24.04' }
$Ndk = '28.2.13676358'

function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe exited with $LASTEXITCODE" }
}

function Set-AndroidEnv {
    if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    $env:ANDROID_NDK_HOME = Join-Path $env:ANDROID_HOME "ndk\$Ndk"
    # JDK 17 on Windows opens an AF_UNIX socket under the temp dir for NIO;
    # some temp paths make Gradle fail with "Unable to establish loopback connection".
    $tmp = Join-Path $env:USERPROFILE '.hulk-tmp'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    if ($env:JAVA_TOOL_OPTIONS -notmatch 'jdk.net.unixdomain.tmpdir') {
        $env:JAVA_TOOL_OPTIONS = "$env:JAVA_TOOL_OPTIONS -Djdk.net.unixdomain.tmpdir=$tmp".Trim()
    }
}

function Get-WslPath([string]$WindowsPath) {
    $drive = $WindowsPath.Substring(0, 1).ToLower()
    return "/mnt/$drive" + ($WindowsPath.Substring(2) -replace '\\', '/')
}

Push-Location $Root
try {
    switch ($Command) {
        'sdk' { Invoke-Checked python @('-B', 'tools/fetch_sdk_context.py') }
        'apk' {
            Set-AndroidEnv
            Invoke-Checked python @('-B', 'tools/fetch_sdk_context.py')
            Invoke-Checked gradle @('-p', 'android', '--no-daemon', 'assembleDebug')
        }
        'apk-vk' {
            Set-AndroidEnv
            Invoke-Checked python @('-B', 'tools/fetch_sdk_context.py')
            Invoke-Checked gradle @('-p', 'android', '--no-daemon', '-PhulkVulkanPreview=true', 'assembleDebug')
        }
        'compile-db' { Invoke-Checked python @('-B', 'tools/gen_compile_commands.py') }
        'test' {
            $env:PYTHONDONTWRITEBYTECODE = '1'
            Get-ChildItem tools -Filter 'test_*.py' |
                Where-Object Name -ne 'test_install_hulk_diagnostic.py' |  # POSIX-only (Termux)
                ForEach-Object { Invoke-Checked python @('-B', '-m', 'unittest', 'discover', '-s', 'tools', '-p', $_.Name) }
            Invoke-Checked python @('-B', 'tools/check_publish_safety.py', '--tree')
        }
        'ci' {
            $repo = Get-WslPath $Root
            $workflowArgs = ($Rest | ForEach-Object { "'$_'" }) -join ' '
            Invoke-Checked wsl @('-d', $Distro, '--cd', $repo, '--', 'bash', '-lc', "python3 -B tools/run_ci_local.py $workflowArgs")
        }
        'setup-wsl' {
            $repo = Get-WslPath $Root
            Invoke-Checked wsl @('-d', $Distro, '-u', 'root', '--cd', $repo, '--', 'bash', 'tools/setup_wsl.sh', ($Rest | Select-Object -First 1))
        }
        'doctor' {
            foreach ($tool in 'git', 'gh', 'python', 'cmake', 'ninja', 'clang', 'clangd', 'java', 'gradle', 'adb', 'pyright-langserver') {
                $found = Get-Command $tool -ErrorAction SilentlyContinue
                '{0,-20} {1}' -f $tool, $(if ($found) { $found.Source } else { 'MISSING' })
            }
            '{0,-20} {1}' -f 'ANDROID_HOME', $env:ANDROID_HOME
            '{0,-20} {1}' -f 'ANDROID_NDK_HOME', $env:ANDROID_NDK_HOME
            '{0,-20} {1}' -f 'core.hooksPath', (git config core.hooksPath)
            '{0,-20} {1}' -f 'x86_context.h', (Test-Path 'android/app/src/main/cpp/sdk/cpu/x86_context.h')
            wsl -l -v
        }
        default { Get-Help $PSCommandPath -Detailed }
    }
} finally {
    Pop-Location
}
