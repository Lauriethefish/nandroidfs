$ErrorActionPreference = "Stop"
$ExeName = "nandroid-daemon"
$PushDestination = "/data/local/tmp/$ExeName"

Write-Output "Building"
./build
Write-Output "Copying and invoking"
adb push "./libs/arm64-v8a/$ExeName" $PushDestination
adb forward --remove-all
adb forward tcp:25565 tcp:25565
adb shell chmod +x $PushDestination

try     {
    $DaemonProcess = Start-Process adb -PassThru -ArgumentList "shell .$PushDestination" -NoNewWindow
    Wait-Process $DaemonProcess.Id
}   finally     {
    # Ctrl + C occurs here, alongside any other error.
    #
    # Ensure we actually pkill the daemon because sometimes ADB leaves it running
    # This leads to flaky connections with the daemon as the windows client ends up connecting to an
    # old daemon that isn't working properly.
    Write-Host "Pkilling"
    adb shell pkill nandroid-daemon
    Stop-Process $DaemonProcess.Id -Force
}
