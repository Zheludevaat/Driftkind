param()
# One-click build and install script for Windows

if ($args -contains '--help') {
    Write-Host "Usage: install.ps1"
    Write-Host "Builds the plugin using CMake and installs it to the user's VST3 folder."
    exit 0
}

$Root = Resolve-Path (Join-Path $PSScriptRoot '..')
$JuceDir = Join-Path $Root 'deps/juce'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error 'cmake is required'
    exit 1
}

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Write-Error 'Visual Studio Build Tools (cl) are required'
    exit 1
}

if (-not (Test-Path $JuceDir)) {
    Write-Host "JUCE not found, cloning to $JuceDir"
    New-Item -ItemType Directory -Force -Path (Join-Path $Root 'deps') | Out-Null
    git clone --depth 1 https://github.com/juce-framework/JUCE.git $JuceDir | Out-Null
}

$buildDir = Join-Path $Root 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
cmake -B $buildDir -S $Root -DJUCE_DIR=$JuceDir
cmake --build $buildDir --config Release
cpack --config "$buildDir/CPackConfig.cmake" | Out-Null

$plugin = Get-ChildItem $buildDir -Filter *.vst3 -Recurse | Select-Object -First 1
if ($plugin) {
    $dest = Join-Path $Env:COMMONPROGRAMFILES 'VST3'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item $plugin.FullName $dest -Recurse -Force
    Write-Host "Installed $($plugin.Name) to $dest"
} else {
    Write-Warning 'Plugin artifact not found after build'
}
