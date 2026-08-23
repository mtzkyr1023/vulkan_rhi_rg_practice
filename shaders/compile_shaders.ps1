# Compiles every .hlsl in this directory to both SPIR-V (.spv, for Vulkan) and DXIL
# (.cso, for D3D12).
#
# Two different dxc builds are used on purpose:
#   - the Vulkan SDK's dxc is the one that can emit SPIR-V (-spirv)
#   - the Windows SDK's dxc ships dxil.dll, so it can *sign* DXIL; unsigned DXIL is
#     rejected by D3D12 unless the machine is in developer mode
#
# Entry points are fixed: VSMain / PSMain.

$ErrorActionPreference = "Stop"

$shaderDir = $PSScriptRoot

$spirvDxc = Join-Path $env:VULKAN_SDK "Bin\dxc.exe"

$dxilDxc = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter dxc.exe -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\x64\\" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (-not (Test-Path $spirvDxc)) { throw "SPIR-V dxc not found at $spirvDxc (is VULKAN_SDK set?)" }
if (-not $dxilDxc) { throw "DXIL dxc not found under the Windows Kits bin directory" }

Write-Host "spirv dxc: $spirvDxc"
Write-Host "dxil  dxc: $dxilDxc"

# 6.6 for the resource indexing used by the bindless texture array.
$stages = @(
    @{ Entry = "VSMain"; Profile = "vs_6_6"; Suffix = "vs" },
    @{ Entry = "PSMain"; Profile = "ps_6_6"; Suffix = "ps" }
)

foreach ($hlsl in Get-ChildItem $shaderDir -Filter *.hlsl) {
    foreach ($stage in $stages) {
        $base = Join-Path $shaderDir "$($hlsl.BaseName).$($stage.Suffix)"

        # MV_TARGET_VULKAN selects the [[vk::push_constant]] declaration; the DXIL build
        # gets the root constant cbuffer instead.
        #
        # -fvk-use-dx-layout makes SPIR-V use D3D's packing rules. Without it DXC applies
        # std430-style alignment, where a float3 member is padded to 16 bytes, so a struct
        # like ModelVertex (float3, float3, float2) becomes 48 bytes in the shader against
        # 32 on the CPU and every structured buffer read lands on the wrong field.
        & $spirvDxc -spirv -fvk-use-dx-layout -D MV_TARGET_VULKAN=1 -T $stage.Profile -E $stage.Entry -Fo "$base.spv" $hlsl.FullName
        if ($LASTEXITCODE -ne 0) { throw "SPIR-V compile failed: $($hlsl.Name) $($stage.Entry)" }

        & $dxilDxc -T $stage.Profile -E $stage.Entry -Fo "$base.cso" $hlsl.FullName
        if ($LASTEXITCODE -ne 0) { throw "DXIL compile failed: $($hlsl.Name) $($stage.Entry)" }

        Write-Host "  $($hlsl.BaseName).$($stage.Suffix)  ->  .spv / .cso"
    }
}

Write-Host "done."
