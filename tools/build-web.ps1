param(
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [string]$Sdk='',
    [string]$ShaderCompiler='',
    [string]$CMake='',
    [string]$Ninja='',
    [int]$Jobs=8
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(!$Sdk){$Sdk=Join-Path $projectRoot 'build/release/web/.tools/emsdk'}
if(!$ShaderCompiler){$ShaderCompiler=Join-Path $projectRoot 'build/release/microsoft/.cmake/thirdparty/bgfx.cmake/cmake/bgfx/Release/shaderc.exe'}
if(!$CMake){
    $command=Get-Command cmake -ErrorAction SilentlyContinue
    if($command){$CMake=$command.Source}else{$CMake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'}
}
if(!$Ninja){
    $command=Get-Command ninja -ErrorAction SilentlyContinue
    if($command){$Ninja=$command.Source}else{$Ninja='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'}
}
foreach($required in @($CMake,$Ninja,$ShaderCompiler,(Join-Path $Sdk 'emsdk_env.bat'))){
    if(!(Test-Path -LiteralPath $required)){throw "Missing prerequisite: $required (see docs/web-build.md)"}
}
# Read the SDK's environment into this process only; never change user/system PATH.
$previousEnvironment=@{}
$sdkEnvironment=& cmd /d /c "`"$Sdk\emsdk_env.bat`" >nul 2>nul && set"
foreach($line in $sdkEnvironment){
    if($line -match '^([^=]+)=(.*)$'){
        $name=$Matches[1];$value=$Matches[2]
        $previousEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
        [Environment]::SetEnvironmentVariable($name,$value,'Process')
    }
}
try{
    $buildDirectory=Join-Path $projectRoot "build/$($Configuration.ToLowerInvariant())/web/.cmake"
    & $CMake -S $projectRoot -B $buildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" `
        "-DCMAKE_TOOLCHAIN_FILE=$Sdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" `
        "-DCMAKE_BUILD_TYPE=$Configuration" "-DGENESIS_HOST_SHADERC=$ShaderCompiler" -DBUILD_TESTING=OFF
    if($LASTEXITCODE){throw 'Web configure failed'}
    & $CMake --build $buildDirectory --target Genesis --parallel $Jobs
    if($LASTEXITCODE){throw 'Web build failed'}
}finally{
    foreach($name in $previousEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$previousEnvironment[$name],'Process')}
}
