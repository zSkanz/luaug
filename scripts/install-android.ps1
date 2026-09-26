# Installs what building for Android needs on a Windows machine, the same set
# the nightly `Triangle APK` job installs on its runner:
#
#   * the Android command-line tools (sdkmanager) under %LOCALAPPDATA%\Android\Sdk
#   * the NDK this repository pins (cmake/toolchains/android.cmake, R5)
#   * platforms;android-35, build-tools;35.0.0 and platform-tools (adb)
#   * a JDK 17 and the Gradle the triangle sample's wrapper pins, both under
#     the build root's toolchains folder -- never on PATH and never JAVA_HOME,
#     so a machine's own Java is left alone (Gradle 8.12 does not run on 24+)
#
# and sets ANDROID_HOME and ANDROID_SDK_ROOT for the user, adding adb to the
# user's PATH. Nothing needs administrator rights. Running it a second time
# installs only what is missing.
#
# Installing the SDK packages accepts their licences on your behalf; read them
# with `sdkmanager --licenses` first if you want to.
#
#     scripts/install-android.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = Split-Path -Parent $PSScriptRoot

$ndk = (Select-String -Path (Join-Path $repo 'cmake/toolchains/android.cmake') `
        -Pattern '^set\(LUAUG_ANDROID_NDK_VERSION "([^"]+)"').Matches[0].Groups[1].Value
$gradleUrl = (Select-String -Path (Join-Path $repo 'samples/triangle/android/gradle/wrapper/gradle-wrapper.properties') `
        -Pattern '^distributionUrl=(.+)$').Matches[0].Groups[1].Value -replace '\\', ''
$gradleName = [IO.Path]::GetFileNameWithoutExtension($gradleUrl) -replace '-bin$', ''

$buildRoot = if ($env:LUAUG_BUILD_ROOT) { $env:LUAUG_BUILD_ROOT } else { Join-Path $env:LOCALAPPDATA 'LuauG\build' }
$tools = Join-Path $buildRoot 'toolchains'
$downloads = Join-Path $tools 'dl'
$sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
New-Item -ItemType Directory -Force $downloads | Out-Null

function Get-Once([string]$Url, [string]$File) {
    $path = Join-Path $downloads $File
    if (-not (Test-Path $path)) {
        Write-Host "[android] downloading $Url"
        Invoke-WebRequest $Url -OutFile $path
    }
    return $path
}

# JDK 17, portable.
$jdkRoot = Join-Path $tools 'jdk17'
if (-not (Get-ChildItem $jdkRoot -Directory -ErrorAction SilentlyContinue)) {
    $zip = Get-Once 'https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse' 'jdk17.zip'
    Expand-Archive $zip $jdkRoot -Force
}
$jdk = (Get-ChildItem $jdkRoot -Directory | Select-Object -First 1).FullName

# The Gradle the sample pins.
if (-not (Test-Path (Join-Path $tools "$gradleName\bin\gradle.bat"))) {
    $zip = Get-Once $gradleUrl "$gradleName.zip"
    Expand-Archive $zip $tools -Force
}

# The command-line tools, as `cmdline-tools\latest`, which is where sdkmanager
# expects to find itself.
$sdkmanager = Join-Path $sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
if (-not (Test-Path $sdkmanager)) {
    $zip = Get-Once 'https://dl.google.com/android/repository/commandlinetools-win-13114758_latest.zip' 'cmdline-tools.zip'
    $staging = Join-Path $sdk 'cmdline-tools\staging'
    Expand-Archive $zip $staging -Force
    Move-Item (Join-Path $staging 'cmdline-tools') (Join-Path $sdk 'cmdline-tools\latest')
    Remove-Item -Recurse -Force $staging
}

# **Through cmd, with the answers in a file.** sdkmanager is a batch file that
# reads its licence prompts from stdin, and PowerShell's pipe into a batch file
# does not reach it -- every package is then skipped "as the license is not
# accepted", and the command still exits zero.
$answers = Join-Path $downloads 'yes.txt'
Set-Content -Encoding ascii $answers (("y`r`n") * 40)
$packages = "`"ndk;$ndk`" `"platforms;android-35`" `"build-tools;35.0.0`" `"platform-tools`""
cmd /c "set JAVA_HOME=$jdk&& `"$sdkmanager`" --sdk_root=`"$sdk`" --licenses < `"$answers`" > nul 2>&1"
cmd /c "set JAVA_HOME=$jdk&& `"$sdkmanager`" --sdk_root=`"$sdk`" --install $packages"
if ($LASTEXITCODE -ne 0) { throw "sdkmanager failed" }
if (-not (Test-Path (Join-Path $sdk "ndk\$ndk"))) { throw "NDK $ndk is not installed -- were the licences refused?" }

[Environment]::SetEnvironmentVariable('ANDROID_HOME', $sdk, 'User')
[Environment]::SetEnvironmentVariable('ANDROID_SDK_ROOT', $sdk, 'User')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$adb = Join-Path $sdk 'platform-tools'
if ($userPath -notlike "*$adb*") {
    [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ";$adb"), 'User')
}

Write-Host ""
Write-Host "[android] NDK $ndk, platform 35, build-tools 35.0.0 and adb under $sdk" -ForegroundColor Green
Write-Host "[android] JDK 17 at $jdk and $gradleName under $tools"
Write-Host "[android] ANDROID_HOME is set for new shells; scripts/localgate.ps1 -Only android builds for arm64"
