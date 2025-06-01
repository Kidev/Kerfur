# Kerfur  
[Try it in your browser](https://demo.kidev.org/Kerfur)

[![meow](assets/kerfur.gif)](https://demo.kidev.org/Kerfur)  

## How to install ?
You can use an installer with easy updates:
- [Windows x64](https://github.com/Kidev/Kerfur/releases/latest/download/KerfurInstaller-Windows-x64.exe)  
- [Linux x64](https://github.com/Kidev/Kerfur/releases/latest/download/KerfurInstaller-Linux-x64)  
- [Linux arm64 (RaspiOS Bookworm for RPI5)](https://github.com/Kidev/Kerfur/releases/latest/download/KerfurInstaller-Linux-arm64-rpi)  

You can download and install a .app on macOS
- [macOS 15 arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-macos-15-arm64.tar.gz)  

You can download and run an AppImage:
- [Linux x64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-x64.AppImage)  
- [Linux arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-arm64.AppImage)  
- [Linux arm64 (RaspiOS Bookworm for RPI5)](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-bookworm-arm64-rpi.AppImage)  

You can download an archive and extract the files:
- [Windows x64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-windows-x64.zip)  
- [Windows arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-windows-arm64.zip)  
- [Linux x64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-x64.tar.gz)  
- [Linux arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-arm64.tar.gz)  
- [Linux arm64 (RaspiOS Bookworm for RPI5)](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-bookworm-arm64-rpi.tar.gz)  

## How to build ?
You can also chose to build the project. It is very straightforward.  

### Requirements
- Qt 6.7.0+ installed and its install path known  
- Standard C++ build packages (`make`, `cmake`, `gcc` or `clang` or `msvc`...).  
- An internet connection, it will download `vcpkg` and if required `emsdk`

### Build
Run the following, replacing the value of `QT_ROOT_DIR` with yours.  You must include the version and the architecture:  
```console
kidev:~$ make QT_ROOT_DIR=/opt/Qt/6.8.3/gcc_64
````

### Advanced
- You must include the version and the architecture like I did.  
- You can also simply run `make` if you have the env var `QT_ROOT_DIR` set, or if you have a `.env` file at the root of the repository that exports `QT_ROOT_DIR`.  
- You can use `make` rules included to:  
    - `make installer` to create an online installer using a FTP as CDN. You will need to provide credentials (`CDN_UPLOAD_SOCKET` `CDN_UPLOAD_USERNAME` `CDN_UPLOAD_PASSWORD`). The repository for the OS you built on will be created inside a folder named after your OS, in the folder you are in upon connection.  
    - `make web` to create a WASM build  
    - `make run-web` to start a localserver running the application (requires to have a WASM build first with `make web`)  
    - `make upload-web` to upload the WASM application to a web server.  You will need to provide credentials (`WASM_UPLOAD_SOCKET` `WASM_UPLOAD_USERNAME` `WASM_UPLOAD_PASSWORD`). The application will be uploaded inside a folder `Kerfur` in the folder you are in upon connection.  
