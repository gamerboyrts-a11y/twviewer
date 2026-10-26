# TwViewer CIA build environment

This folder contains the metadata and assets to package TwViewer as an installable CIA.

Place the official Windows builds of these tools in `tools\`:

- `makerom.exe`
- `bannertool.exe`

The upstream projects are [Project_CTR](https://github.com/3DSGuy/Project_CTR) for `makerom` and [bannertool](https://github.com/Steveice10/bannertool) for `bannertool`.

Then run this from the project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\cia-build\build_cia.ps1
```

After the tools are installed, `make cia` runs the same script.

The script compiles the current C sources, links an ELF under `cia-build\work`, and creates `twviewer.cia` in the project root. It does not replace `twviewer.3dsx`.

Copy the CIA to the 3DS SD card and install it with FBI. It will appear on the Home Menu as TwViewer. Installation requires custom firmware.

The package uses New 3DS settings from `app.rsf` (804 MHz, L2 cache, core 2, and MVD access) and title unique ID `0xECA11`.
