Build
=====

This document describes how to build the project and its dependencies. You can also find a more information about the various Cmake options.

## `./run build`

We encourage you to use the `./run build` script to build the project. This script will automatically download and build all the dependencies for you.

In example, to build the project in release mode, you can run the following command:

```bash
./run generate build release
```

This will generate the project files and build the project in release mode.

## Options

If you want to pass Cmake build options to the `./run generate` command, you can run the following command:

```bash
./run generate -DBUILD_ENABLE_LOCALIZATION=OFF
```

This will generate the project files with the `BUILD_ENABLE_LOCALIZATION` option set to `OFF`.

Then you can run the following command to build and open the project workspace:

```bash
./run build debug open workspace
```

This will build the project in debug mode and open the project workspace (i.e. your platform IDE).

Finally, here is a list of all the Cmake options you can pass to the `./run generate` command:

### `-DBUILD_INFO=ON|OFF`

**Default: `OFF`**

If enabled, the project will compile with additional compiler an linker verbosity. This is useful for debugging the build process.

### `-DBUILD_ENABLE_LOCALIZATION=ON|OFF`

**Default: `ON`**

If enabled, the project will be built with localization support. If turned off, the project will be built without localization support.

If the localization support is disabled, the `tr(...)` function will be a no-op and return the passed constant string.

If enabled, the `tr(...)` function will return the localized string for the passed constant string. The localized strings are stored in the `framework/localization.h` file.

You can find the `locales.sjson` which contains the localized strings at `config/locales.sjson` directory. If you use the command line argument `--build-locales` when using the build flags `BUILD_DEVELOPMENT` any non localized strings will be added to the `locales.sjson` file with a `@TODO` value so you can easily find them and add the localized string.

When build with `BUILD_DEPLOY`, the `locales.sjson` file will be copied to the `build` directory.

### `-DBUILD_ENALE_DEVELOPMENT=ON|OFF`

**Default: `ON`**

If enabled, the project will be built with development support. If turned off, the project will be built without development support.

### `-DBUILD_MAX_QUERY_THREADS=N`

**Default: `4`**

The maximum number of query threads that can be used by the project. If your project heavily uses the query system, you can increase this value to improve performance, otherwise you can decrease it to reduce memory usage.

### `-DBUILD_MAX_JOB_THREADS=N`

**Default: `4`**

The maximum number of job threads that can be used by the project. If your project heavily uses the job system, you can increase this value to improve performance, otherwise you can decrease it to reduce memory usage.

### `-DBUILD_SERVICE_EXE=ON|OFF`

**Default: `OFF`**

If enabled, the project will be built with the service executable. If turned off, the project will be built without the service executable.

The service executable is used to run the project as a service on Windows or on the command line on Windows.

In example, you can run the following command to build and generate the service executable:

```bash
./run build release generate -DBUILD_SERVICE_EXE=ON
```

Then you can run the following command to run the service executable and get the version as an output:

```bash
./build/service.exe --version
```

**Note:** The build options are cached. If you want to change the build options, you will need to run the following command:

```bash
./run build release generate -DBUILD_SERVICE_EXE=OFF
```

### `-DBUILD_ENABLE_TESTS=ON|OFF`

**Default: `ON`**

If enabled, the project will be built with tests. If turned off, the project will be built without tests. This is useful if you want to build the project without tests for distribution purposes.

**Note:** When building in deploy mode, the tests will be disabled automatically.

```bash	
./run build release generate -DBUILD_ENABLE_TESTS=OFF
```
