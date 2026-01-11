# SAMURAI WARRIORS 4 DX Fix
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/PolarWizard/SamuraiWarriors4DXFix/total)

Adds support for ultrawide resolutions and additional features.

***This project is designed exclusively for Windows due to its reliance on Windows-specific APIs. The build process requires the use of PowerShell.***

## Fixes
- Support for ultrawide resolutions
- Unstretches, centers, and constrains entire UI to 16:9

## Build and Install
### Using CMake
1. Build and install:
```ps1
git clone https://github.com/PolarWizard/SamuraiWarriors4DXFix.git
cd SamuraiWarriors4DXFix; mkdir build; cd build
# If install is not needed you may omit -DCMAKE_INSTALL_PREFIX and cmake install step.
cmake -DCMAKE_INSTALL_PREFIX=<FULL-PATH-TO-GAME-FOLDER> ..
cmake --build . --config Release
cmake --install .
```
2. Download [d3d11.dll](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) Win64 version
3. Extract to game folder: `SAMURAI WARRIORS 4 DX`

### Using Release
Download and follow instructions in [latest release](https://github.com/PolarWizard/SamuraiWarriors4DXFix/releases)

## Configuration
- Adjust settings in `SAMURAI WARRIORS 4 DX/scripts/SamuraiWarriors4DXFix.yml`

## Screenshots
| ![Demo1](images/SamuraiWarriors4DXFix_1.gif) |
| --- |
| <p align='center'> Fix disabled → Fix enabled </p> |

## License
Distributed under the MIT License. See [LICENSES](LICENSES) for more information.

## External Tools
- [safetyhook](https://github.com/cursey/safetyhook)
- [spdlog](https://github.com/gabime/spdlog)
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)
- [zydis](https://github.com/zyantific/zydis)
