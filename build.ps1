# Build and run the uctx coroutine library
# Usage: .\build.ps1                (build & run with -O2)
#        .\build.ps1 -Clean         (remove build artifacts)

param([switch]$Clean)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

if ($Clean) {
    Remove-Item -LiteralPath "test.exe", "*.o" -ErrorAction SilentlyContinue
    Write-Host "Cleaned."
    exit 0
}

$sources = @("test.c", "coro.c", "coro_switch.S")
$target  = "test.exe"
$cflags  = @("-Wall", "-Wextra", "-O2")

Write-Host "Compiling..." -ForegroundColor Cyan
$args = $cflags + @("-o", $target) + $sources
& gcc $args
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "Running..." -ForegroundColor Cyan
Write-Host ""
& .\$target
$exit = $LASTEXITCODE
Write-Host ""
if ($exit -eq 0) {
    Write-Host "All tests passed." -ForegroundColor Green
} else {
    Write-Host "Tests failed (exit code $exit)." -ForegroundColor Red
}
exit $exit
