Wallet App
==========

Manage your stock and finance

If you are only interested in the application framework used to build the Wallet app, please visit <https://github.com/wiimag/framework>.

If you are interested in the Wallet app, please visit <https://wallet.wiimag.com>.

If you find this application useful and want to support its developement, please consider making a [donation](https://www.paypal.com/donate/?business=9BP5HBC9GFYYA&amount=10&no_recurring=0&item_name=I+make+applications+for+the+love+of+programming.+You+can+support+me+to+continue+the+developement.+Thanks+for+your+support.+%F0%9F%9A%80&currency_code=CAD) or a [feature request](https://www.paypal.com/donate/?business=9BP5HBC9GFYYA&amount=200&no_recurring=0&item_name=Wallet+App+Feature+Request&currency_code=CAD).

## [EODHD API Key Required](https://eodhistoricaldata.com/r/?ref=PF9TZC2T)

The application uses exclusively the [EODHD](https://eodhistoricaldata.com/r/?ref=PF9TZC2T) data APIs. You can get your API key [here](https://eodhistoricaldata.com/r/?ref=PF9TZC2T). Currently the application only works with the __ALL-IN-ONE Package__. As of now I cannot guarantee that the application will work with the other packages. I strongly recommend that you get an API key with all the features. It will cost you around 80 US$ per month. But it is worth it. In most country you can deduct that cost from your taxes.

Make sure you understand the [EODHD Terms and Conditions](https://eodhistoricaldata.com/financial-apis/terms-conditions/) before using the application.

## Introduction

Tired of losing money in the stock market like a forgetful gambler? Say hello to the Wallet app! It's not a tool for managing your bank account or credit card - that's what your banker is for (or your piggy bank if you're old school). Instead, it's a desktop app designed to help you manage your stocks and finance, minus the stress and confusion.

Created out of frustration with the lack of a user-friendly and cheap stock management tool, the Wallet app was born from the ashes of a Google Spreadsheet that was slower than a snail on a lazy day (I was abusing the `GOOGLEFINANCE` function!). It's portable, fast, and easy to use.

And the best part? You don't have to be an expert to use the Wallet app - it's perfect for beginners and inexperienced investors too! It's designed to help you keep track of your stocks and finances, learn how to invest wisely, and make better decisions without feeling like you need a degree in finance. So why not give it a try and start your journey to becoming a stock market pro?

![Wallet App](https://wallet.wiimag.com/manual/en/img/wallet.png)

## [Download](https://wallet.wiimag.com/releases/latest)

You can download the installation and portable packages at <https://wallet.wiimag.com>.

## Getting Started

* [English](https://wallet.wiimag.com/manual/en/)
* [Français](https://wallet.wiimag.com/manual/fr/)
* [Expressions](https://wallet.wiimag.com/manual/en/expressions.md)
* [Documentation](./docs/README.md#documentation)

_Otherwise if you feel like it, you can also build the application from source. Instructions are below._

## Requirements

- A C++20 compiler
- Git <https://git-scm.com/downloads>
- CMake 3.15 or later <https://cmake.org/download/>
- Python 3.8 or later <https://www.python.org/downloads/>

I also recommend using [Visual Studio Code](https://code.visualstudio.com/download) as an editor to modify CMakelists.txt files, bash scripts, etc.

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

## Run Command (`./run --help`)

### Generate Solution

```bash
./run generate
```

The solution will be generated in the `projects/.build` folder.

Note that currently the `./run` scripts only supports the `Visual Studio 2022` generator on Windows and the `Xcode` generator on MacOS. If you want to use another generator, you will have to use `cmake` directly. Here's a few examples for older versions of Visual Studio:

#### Visual Studio 2019

```bash
cmake --no-warn-unused-cli -DBUILD_MAX_JOB_THREADS=4 -DBUILD_MAX_QUERY_THREADS=8 -S./ -B./projects/.build -G "Visual Studio 16 2019" -A x64
```

#### Xcode

```bash
cmake --no-warn-unused-cli -DBUILD_MAX_JOB_THREADS=4 -DBUILD_MAX_QUERY_THREADS=4 -S./ -B./projects/.build -G "Xcode"
```

### Build Solution (In Release)

```
./run build
```

In case the `./run` script doesn't work, you can also use `cmake` directly:

```bash
cmake --build ./projects/.build --config Release --target wallet -j 10
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

### Build Distribution Package

You can build an installation package on Windows using the `./scripts/build-package.sh` scripts or simply by running the following command:

```bash
./run package
```

### Batch it all!

```bash
./run generate build release tests open workspace start --verbose --console
```

This will generate the solution, build it, run the tests, open the workspace, and start the application (or course if everything went well!).
