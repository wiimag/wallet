Command Line
============

The following section provide the various command line arguments you can pass 
to the application to alter the application runtime.

## Switches

| Query Flags | Description |
|:------------|-------------|
| ```--help``` | Prints a help message listing all these flags/options |
| ```--version``` | Prints the version of the application framework and exit right away |
| **User Options** | <hr> |
| ```--session=<name>``` | Override the default application session name in order to run the application like a new user. |
| **Cloud Options** | <hr> |
| ```--host=<string>``` | Override the default `https://cloud.<domain>.com` URL to execute all cloud queries. |
| ```--apikey=<string>``` | Force the application to use the developer API key to run all cloud queries. |
| **Localization Options** | <hr> |
| ```--lang=<string>``` | Override the default locale to use for the application. (i.e. `--lang=en` or `--lang=fr`) |
| ```--build-locales``` | Build and update the localization file for the application at `config/locales.sjson`. |
| **Developer Options** | <hr> |
| ```--console``` | Open the developer console on startup. |
| ```--bgfx-ignore-logs``` | Omit BGFX traces from the application log. |
| ```--render-thread``` | Create a render thread for BGFX (Currently only works on Windows) |
| **Testing Options** | <hr> |
| ```--run-tests``` | Run application tests and exit the application afterward. See [**doctests**](https://github.com/doctest/doctest/blob/master/doc/markdown/commandline.md#command-line) for additional command line arguments |
| ```--build-machine``` | This flags is used on build machine to process special build functions. |
| ```--verbose``` &nbsp; ```--debug``` | Put the application in verbose mode, spewing additional log messages. This can be useful to get additional debuggin informations. |
| ```--exit``` | Run a single application frame and exit right away. This can be useful to report one-time performance trackers. |
| **Profiling Options** | <hr> |
| ```--profile``` | Enable the application profiler to report additional runtime profiling information. |
| ```--profile-log=<path>``` | Output all profiling blocks to a file stream that can be used later to investigation performance issues offline. |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;| |