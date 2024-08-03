# Installs dependencies needed for NandroidFS to build (i.e. dokan)
# This includes the Android NDK if the envvar ANDROID_NDK_HOME is not set.

# Dependency temp paths
$DependenciesTemp = "./dependencies"
$DokanSourceTemp = "$DependenciesTemp/dokan_source.zip"
$DokanBinariesTemp = "$DependenciesTemp/dokan_binaries.zip"
$DokanSourceExtract = "$DependenciesTemp/dokan_src"
$DokanBinariesExtract = "$DependenciesTemp/dokan_binaries"
$NdkTemp = "$DependenciesTemp/android_ndk.zip"
$NdkExtractTemp = "$DependenciesTemp/android_ndk"

$NandroidDepsPath = "./nandroidfs/dependencies"
$NandroidIncludePath = "$NandroidDepsPath/include"
$NandroidLibPath = "$NandroidDepsPath/lib"

# Dokan source and binaries for v2.1.0.1000
$DokanSourceUrl = "https://github.com/dokan-dev/dokany/archive/refs/tags/v2.1.0.1000.zip"
$DokanBinariesUrl = "https://github.com/dokan-dev/dokany/releases/download/v2.1.0.1000/dokan.zip"

# Android NDK r27
$AndroidNdkUrl = "https://dl.google.com/android/repository/android-ndk-r27-windows.zip"


Write-Output "Downloading dokan source and binaries"
Invoke-WebRequest $DokanSourceUrl -OutFile $DokanSourceTemp
Invoke-WebRequest $DokanBinariesUrl -OutFile $DokanBinariesTemp
Write-Output "Extracting archives"
Expand-Archive $DokanSourceTemp -DestinationPath $DokanSourceExtract
Expand-Archive $DokanBinariesTemp -DestinationPath $DokanBinariesExtract
Remove-Item $DokanSourceTemp
Remove-Item $DokanBinariesTemp

Write-Output "Copying dokan headers"
$DokanSourceSubFolder = (Get-ChildItem -Path $DokanSourceExtract -Directory)[0].FullName
# Locate the dokan headers and the driver header that the dokan user library calls.
$Headers = Get-ChildItem -Path ($DokanSourceSubFolder + "\dokan") -Filter "*.h" -File
$Headers += (Get-ChildItem -Path ($DokanSourceSubFolder + "\sys") -Filter "public.h" -File)[0]
if(Test-Path -Path $NandroidDepsPath) {
    Remove-Item -Recurse $NandroidDepsPath
}
# Create the dependency directories for dokan include/lib
New-Item -Path $NandroidDepsPath -ItemType Directory | Out-Null
New-Item -Path $NandroidIncludePath -ItemType Directory | Out-Null
New-Item -Path $NandroidLibPath -ItemType Directory | Out-Null
# Copy the headers into the include path
ForEach($Header in $Headers)
{
    Copy-Item $Header.FullName ($NandroidIncludePath + "/" + $Header.Name)
}

Write-Output "Copying dokan lib"
Copy-Item "$DokanBinariesExtract/x64/Release/dokan2.lib" "$NandroidLibPath/dokan2.lib"

Remove-Item -Recurse $DokanSourceExtract
Remove-Item -Recurse $DokanBinariesExtract

# If no android NDK is set, we will download this and set ANDROID_NDK_HOME
if($null -eq $env:ANDROID_NDK_HOME) {
    Write-Output "Downloading android NDK"
    Invoke-WebRequest $AndroidNdkUrl -OutFile $NdkTemp
    Write-Output "Extracting android NDK"
    Expand-Archive $NdkTemp -DestinationPath $NdkExtractTemp

    Remove-Item -Recurse $NdkTemp

    # Locate the android-ndk-rXXX folder within the extracted ZIP archive.
    # Set ANDROID_NDK_HOME so that build.ps1 will work correctly.
    $env:ANDROID_NDK_HOME = (Get-ChildItem -Path $NdkExtractTemp -Directory)[0].FullName
}
