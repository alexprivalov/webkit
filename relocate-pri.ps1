# Make the generated qmake module files relocatable.
#
# ECMGeneratePriFile writes CMAKE_INSTALL_PREFIX into them as an absolute path. The bundle is
# then unpacked somewhere else - a CI runner's temp directory, another machine - and qmake hands
# jom a dependency on a library that is not there:
#
#   Error: dependent 'C:\qt5_static_webkit_gl\lib\Qt5WebKitWidgets.lib' does not exist.
#
# Qt's own module files use the QT_MODULE_*_BASE variables, which qmake resolves from where it
# finds itself. Rewrite ours the same way; the previously published bundle was relocatable in
# exactly this form, which is why it survived being unpacked on a different drive.
#
# String replacement is deliberately literal (.Replace, not -replace): the replacement text
# contains '$$', which regex substitution would collapse to a single '$'.
param(
    [Parameter(Mandatory=$true)][string]$BundleDir
)

$prefix = ($BundleDir -replace '\\', '/').TrimEnd('/')
$files = Get-ChildItem (Join-Path $BundleDir 'mkspecs\modules\qt_lib_webkit*.pri') -ErrorAction Stop
if (-not $files) { Write-Error "no generated module files under $BundleDir"; exit 1 }

foreach ($f in $files) {
    $text = Get-Content $f.FullName -Raw
    $text = $text.Replace("$prefix/include", '$$QT_MODULE_INCLUDE_BASE')
    $text = $text.Replace("$prefix/lib",     '$$QT_MODULE_LIB_BASE')
    $text = $text.Replace("$prefix/bin",     '$$QT_MODULE_BIN_BASE')
    Set-Content -Path $f.FullName -Value $text -NoNewline
}

# The point of the exercise is that the prefix is gone. Fail loudly rather than ship a bundle
# that only works on the machine that built it.
$left = Select-String -Path $files.FullName -SimpleMatch $prefix
if ($left) {
    Write-Error "prefix still present after rewrite:`n$($left | ForEach-Object { $_.Line })"
    exit 1
}
Write-Output "RELOCATE_PRI_OK $($files.Count) files"
