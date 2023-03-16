Wallet 300K
===========

Manage your stock and finance

## Requirements

- A C++20 compiler
- Git <https://git-scm.com/downloads>
- CMake 3.15 or later <https://cmake.org/download/>
- Python 3.8 or later <https://www.python.org/downloads/>

I also recommend using [Visual Studio Code]<https://code.visualstudio.com/download> as your IDE to edit CMakelists.txt files, bash scripts, etc.

### Windows

- Microsoft Visual Studio 2022 with C++ support <https://visualstudio.microsoft.com/downloads/>
    - I use the community edition which is free
    - I also recommend installing Entrian Source Search <https://entrian.com/source-search/> to search for anything in the project root folder.
- I personnally recomment using **Git Bash** on Windows since all the scripts are written for it (i.e. `./run`)

### MacOS (OSX)

- A bash shell (i.e. iTerm2 <https://iterm2.com/>) to run the scripts (i.e. `./run`)
- XCode 12.5 or later <https://developer.apple.com/xcode/>
- Apple Command Line Tools <https://developer.apple.com/download/more/>
- Homebrew <https://brew.sh/>

## Using

- imgui <https://github.com/ocornut/imgui> for the UI.
- glfw <https://github.com/glfw/glfw> to manage platform windows.
- bgfx <https://github.com/bkaradzic/bgfx> for the 2D/3D rendering backends.
- foundation_lib <https://github.com/mjansson/foundation_lib> for the common platform code and basic structures.
- libcurl <https://github.com/curl/curl> to execute various https requests.
- doctest <https://github.com/doctest/doctest> to run unit and integration tests for the various apps and framework modules.
- stb_image <https://github.com/nothings/stb> to decode various stock logos downloaded from the web.
- mnyfmt.c from <http://www.di-mare.com/adolfo/p/mnyfmt/mnyfmt.c> to format stock dollar prices.

## Documentation

You can find more documentation about the application framework under [docs](docs/README.md)

## Run Command

### Build Solution (In Release)

```
./run build
```

### Build Solution In Debug

```
./run build debug
```

### Build Solution and Run Tests

```
./run build tests
```

#### You can also simply run test when the solution is already built

```
./run tests --verbose --bgfx-ignore-logs
```

### Or simply run the solution (if already built)

```
./run --console --log-debug
```

### Open Solution Workspace

```
./run generate open workspace
```

