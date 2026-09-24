param(
    [Parameter(Mandatory=$true)][string]$CudaRoot,
    [int]$SmArchitecture=75,
    [string]$BinaryName='genesis-physx-editor',
    [string]$PhysxCheckout='',
    [string]$BuildRoot=''
)

$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(-not $PhysxCheckout){$PhysxCheckout=Join-Path $repo 'build/.deps/PhysX'}
if(-not $BuildRoot){$BuildRoot=Join-Path $repo 'build/.deps'}
$CudaRoot=[IO.Path]::GetFullPath($CudaRoot)
$PhysxCheckout=[IO.Path]::GetFullPath($PhysxCheckout)
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$nvcc=Join-Path $CudaRoot 'bin/nvcc.exe'
if(-not (Test-Path -LiteralPath $nvcc)){throw "CUDA nvcc.exe is missing from $CudaRoot"}
$version=& $nvcc --version | Out-String
if($version -notmatch 'release 13\.4'){throw 'This PhysX compatibility build requires CUDA 13.4.'}
if((& $nvcc --list-gpu-code) -notcontains "sm_$SmArchitecture"){
    throw "CUDA 13.4 does not support SM $SmArchitecture"
}

$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if(-not (Test-Path -LiteralPath $vswhere)){throw 'Visual Studio Installer vswhere.exe is required.'}
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'A Visual Studio C++ toolchain is required.'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
foreach($file in @($vcvars,$cmake,$ninja)){
    if(-not (Test-Path -LiteralPath $file)){throw "Missing build tool: $file"}
}

$tag='110.1-omni-and-physx-5.9.0'
$commit='517a0073715120e114ee055b63b26c95e00d9039'
if(-not (Test-Path -LiteralPath (Join-Path $PhysxCheckout '.git'))){
    git clone --filter=blob:none --sparse --branch $tag https://github.com/NVIDIA-Omniverse/PhysX.git $PhysxCheckout
    if($LASTEXITCODE -ne 0){throw 'PhysX clone failed'}
    git -C $PhysxCheckout sparse-checkout set physx
    if($LASTEXITCODE -ne 0){throw 'PhysX sparse checkout failed'}
}
$current=(git -C $PhysxCheckout rev-parse HEAD).Trim()
if($current -ne $commit){throw "PhysX checkout is $current; expected $commit"}
$patch=Join-Path $repo 'cmake/physx-cuda13.patch'
git -C $PhysxCheckout apply --ignore-whitespace --reverse --check $patch 2>$null
if($LASTEXITCODE -ne 0){
    git -C $PhysxCheckout apply --ignore-whitespace --check $patch
    if($LASTEXITCODE -ne 0){throw 'PhysX CUDA 13 patch cannot be applied cleanly'}
    git -C $PhysxCheckout apply --ignore-whitespace $patch
    if($LASTEXITCODE -ne 0){throw 'PhysX CUDA 13 patch failed'}
}

$physxSource=(Join-Path $PhysxCheckout 'physx').Replace('\','/')
$physxBuild=Join-Path $BuildRoot 'physx-build13'
$physxOutput=(Join-Path $BuildRoot 'physx-install').Replace('\','/')
$genesisBuild=Join-Path $BuildRoot 'genesis-physx-build'
$physxLib=Join-Path $physxOutput 'bin/win.x86_64.vc143.mt/release'
$env:CUDA_PATH=$CudaRoot
$env:PATH="$(Join-Path $CudaRoot 'bin');$(Split-Path $ninja);$env:PATH"
$env:NVCC_PREPEND_FLAGS='-diag-suppress=20011'

function Invoke-MsvcCmake([string[]]$Arguments){
    $quoted=($Arguments | ForEach-Object { '"'+($_ -replace '"','\"')+'"' }) -join ' '
    $command='call "'+$vcvars+'" >nul && "'+$cmake+'" '+$quoted
    & cmd.exe /d /c $command
    if($LASTEXITCODE -ne 0){throw "CMake failed (exit $LASTEXITCODE)"}
}

Invoke-MsvcCmake @(
    '-S',(Join-Path $physxSource 'compiler/public'),'-B',$physxBuild,'-G','Ninja',
    '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_CUDA_COMPILER=$nvcc",
    "-DCUDAToolkit_ROOT=$CudaRoot","-DCMAKE_CUDA_ARCHITECTURES=$SmArchitecture",
    "-DPX_CUDA_SM_ARCH=$SmArchitecture","-DPHYSX_ROOT_DIR=$physxSource",
    '-DTARGET_BUILD_PLATFORM=windows','-DPLATFORM=Windows','-DPX_GENERATE_GPU_PROJECTS=ON',
    '-DPX_GENERATE_STATIC_LIBRARIES=ON','-DPX_BUILDSNIPPETS=OFF','-DPX_BUILDPVDRUNTIME=OFF',
    '-DNV_USE_STATIC_WINCRT=ON',"-DPX_OUTPUT_LIB_DIR=$physxOutput","-DPX_OUTPUT_BIN_DIR=$physxOutput"
)
Invoke-MsvcCmake @('--build',$physxBuild,'--target','PhysXGpu','PhysX','PhysXExtensions','PhysXCooking','-j','8')
Invoke-MsvcCmake @(
    '-S',$repo,'-B',$genesisBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$ninja",'-DGENESIS_WITH_PHYSX_PBD=ON',
    "-DGENESIS_RELEASE_BINARY_NAME=$BinaryName","-DGENESIS_PHYSX_ROOT=$physxSource",
    "-DGENESIS_PHYSX_LIB_DIR=$physxLib","-DGENESIS_CUDA_ROOT=$CudaRoot"
)
Invoke-MsvcCmake @('--build',$genesisBuild,'--target','Genesis','PhysxParticlesTests','GameEditorTests','-j','8')
$particleTest=Join-Path $repo 'build/release/microsoft/PhysxParticlesTests.exe'
$editorTest=Join-Path $repo 'build/release/microsoft/GameEditorTests.exe'
& $particleTest
if($LASTEXITCODE -ne 0){throw 'PhysX GPU test failed'}
& $editorTest
if($LASTEXITCODE -ne 0){throw 'Editor authoring test failed'}
Write-Output "Built $BinaryName.exe with PhysX for SM $SmArchitecture"
