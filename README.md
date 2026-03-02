# EmbedR

`EmbedR` is a small C++20 library that embeds the R interpreter into your process and provides:

- R code evaluation from `std::string`
- conversion of base R objects to C++ scalar/container types
- conversion of R lists to `nlohmann::json`
- import of `nlohmann::json` into the R global environment as vectors/lists
- plot rendering to in-memory PNG or PDF bytes

## Requirements

- CMake >= 3.20
- C++20 compiler
- R installation with headers and library (`Rembedded.h`, `libR`)

`nlohmann::json` is fetched automatically with CMake `FetchContent`.

## Build

```bash
cmake -B build -G Ninja
cmake --build build
```

If R is not in a standard location, set `R_HOME`:

```bash
cmake -B build -G Ninja -DR_HOME=/path/to/R/home
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Documentation

If Doxygen is installed:

```bash
cmake --build build --target docs
```

Generated HTML is under `doc/html`.

## Use In Your CMake Project (FetchContent)

```cmake
include(FetchContent)

FetchContent_Declare(
  EmbedR
  GIT_REPOSITORY <your-embedr-repo-url>
  GIT_TAG <tag-or-commit>
)

# Optional: disable EmbedR tests/docs in consumer builds
set(EMBEDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EMBEDR_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(EmbedR)

target_link_libraries(your_target PRIVATE EmbedR::EmbedR)
```

## Minimal C++ Example

```cpp
#include <EmbedR.hpp>

#include <nlohmann/json.hpp>

int main() {
  EmbedR::RInterpreter r;

  auto value = r.eval("1 + 1");

  nlohmann::json payload = {
    {"x", 10},
    {"labels", {"a", "b", "c"}}
  };
  r.assign_json_as_list("data", payload);

  auto result = r.eval_json("list(sum = data$x + 5L, labels = data$labels)");

  auto png = r.render_plot(
    "plot(1:5, 1:5, type='l')",
    EmbedR::RInterpreter::GraphicsFormat::Png,
    800,
    600,
    96
  );

  return png.empty() ? 1 : 0;
}
```

## Public API

Main header: `include/EmbedR.hpp`

- `EmbedR::RInterpreter`
- `EmbedR::RInterpreter::Options`
- `EmbedR::RInterpreter::RValue`
- `EmbedR::RInterpreter::eval(...)`
- `EmbedR::RInterpreter::eval_json(...)`
- `EmbedR::RInterpreter::source_script(...)`
- `EmbedR::RInterpreter::get_stdout_buffer(...)`
- `EmbedR::RInterpreter::get_stderr_buffer(...)`
- `EmbedR::RInterpreter::clear_output_buffers(...)`
- `EmbedR::RInterpreter::assign_json_as_list(...)`
- `EmbedR::RInterpreter::render_plot(...)`

## Notes

- The interpreter is embedded (`Rf_initEmbeddedR`), not run through pipe-based command execution.
- `Options` can load `.Renviron`/`.Renv` from a configured working directory.
- The library target exported by this project is `EmbedR::EmbedR`.
