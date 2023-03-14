Wallet 300K
===========

Manage your stock and finance

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

