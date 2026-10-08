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

# The module files also describe a shared build: what the libraries need privately (multimedia,
# multimediawidgets for the fullscreen video widget) is in run_depends, which a static app never
# links, so an app without QT += multimediawidgets failed on QVideoWidget. Move it into depends
# and mark the modules staticlib, as Qt's own static modules are.
foreach ($f in $files) {
    $lines = Get-Content $f.FullName
    $run = (($lines | Where-Object { $_ -match '\.run_depends = ' }) -replace '^.*\.run_depends = ', '').Trim()
    $lines = foreach ($line in $lines) {
        if ($line -match '\.run_depends = ') { $line -replace '=.*$', '=' }
        elseif ($line -match '\.depends = ') { "$line $run".TrimEnd() }
        elseif ($line -match '\.module_config = ' -and $line -notmatch 'staticlib') { $line.TrimEnd() + ' staticlib' }
        else { $line }
    }
    Set-Content -Path $f.FullName -Value $lines
}

# The engine's static libraries, through QT.webkit.uses, so only apps that use QtWebKit link them,
# and with nothing to list in the app. WebGL and clang's builtins only when the bundle has them.
$lib = Join-Path $BundleDir 'lib'
$deps = '-L$$QT_MODULE_LIB_BASE -lWebCore -lPAL -lJavaScriptCore -lWTF -lwoff2dec -lwoff2common ' +
        '-lbrotlidec -lbrotlicommon -licuuc -licuin -licudt -lharfbuzz-icu -lsqlite3 -llibxml2 -lwebp'
if (Test-Path (Join-Path $lib 'libGLESv2.lib')) { $deps += ' -llibGLESv2 -llibEGL -lANGLE -ld3d9 -ldxgi -ldxguid' }
if (Test-Path (Join-Path $lib 'clang_rt.builtins-x86_64.lib')) { $deps += ' -lclang_rt.builtins-x86_64' }
$webkitPri = Join-Path $BundleDir 'mkspecs\modules\qt_lib_webkit.pri'
if (-not (Select-String -Path $webkitPri -SimpleMatch 'QT.webkit.uses' -Quiet)) {
    Add-Content -Path $webkitPri -Value @('QT.webkit.uses = webkit_static_deps', "QMAKE_LIBS_WEBKIT_STATIC_DEPS = $deps")
}
Write-Output "RELOCATE_PRI_OK $($files.Count) files"
