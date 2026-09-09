# teez-core

Orchestrator library for [teez](https://github.com/Coditary/teez): plugin discovery, Lua plugins, async process streaming.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Dependencies

Fetched automatically via CMake FetchContent: Lua 5.4, sol2, nlohmann/json, Catch2 (tests).

## Integration tests

When `teez-worker` is present as a sibling directory, worker integration tests link `teez_worker_lib` directly — no separate `teez-worker` binary or `-DTEEZ_WORKER_BIN` is required.

## Run tests via teez CLI

`teez.config.lua` in this repo configures CTest via `runners.ctest` (`build_dir = "build"`, skips `end-to-end` tests).

```bash
# from teez-core/
../teez-cli/build/teez run .
../teez-cli/build/teez run build
```

Override filters in `teez.config.lua`:

```lua
runners = {
    ctest = {
        build_dir = "build",
        regex = "discovery",
        exclude = "integration",
        parallel = 4,
    },
}
```

