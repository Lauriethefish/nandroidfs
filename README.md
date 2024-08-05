# NandroidFS

## What is NandroidFS?
NandroidFS is a filesystem for Windows which allows Android devices connected to your PC via the [Android Debug Bridge](https://developer.android.com/tools/adb) to be mounted as though they were a regular drive connected to your PC. (with a drive letter for each device)

NandroidFS is not a kernel mode driver, but instead uses [Dokan](https://github.com/dokan-dev/dokany). This allows NandroidFS, which runs as a regular user-mode application, to respond to filesystem requests.

## Why use NandroidFS?
Android devices can already be accessed in Windows Explorer using the [Media Transfer Protocol](https://en.wikipedia.org/wiki/Media_Transfer_Protocol), however this does NOT mean that they are mounted as drives. So you can't use the device in the terminal/shell scripts. Explorer's MTP integration is slow and limited, and this is somewhat inevitable given the limitations of the protocol. 

MTP only allows downloading/uploading a whole file, and has no support for editing files in place/streaming data.

## How to use
1. Install [dokan library v2.1.0](https://github.com/dokan-dev/dokany/releases/tag/v2.1.0.1000)
2. Download and run the latest NandroidFS installer from [github actions](https://github.com/Lauriethefish/nandroidfs/actions)
3. NandroidFS will run once the installer exits and on startup. 
4. Plug in an Android device and allow USB debugging. NandroidFS will mount it to the next available drive letter.

## Licensing
NandroidFS is available under the terms of the GNU General Public License, version 3. The full text can be found [here](./LICENSE).

## Development
### Project structure
NandroidFS has two parts:
- A client, `nandroidfs.exe`, which runs on the Windows computer and connects to...
- A daemon/agent, `nandroid-daemon`, which runs on any connected Android devices and communicates with the client with sockets, using `adb forward` to forward ports from the Android device to the Windows computer.

### Compilation Instructions
#### Requirements
To manually install:
- Visual Studio 2022 with the `Desktop development with C++` package installed.
- Dokan driver v2.1.0 installed as is required to run the app.

Installed by script, run `./install_deps.ps1` to install any of these dependencies that are missing:
- Dokan headers and libs in `nandroidfs/dependencies`
- Android NDK r27 with path set on `ANDROID_NDK_HOME`.
- Inno setup (for building the installer if desired.) 

#### Instructions
1. - Navigate to `./nandroid_daemon` and run `./build.ps1`
2. - Open the `NandroidFS.sln` file in Visual Studio 2022 and change the configuration to `Release`. Press `Ctrl + Shift + B` to build or use the button to build and run the app.