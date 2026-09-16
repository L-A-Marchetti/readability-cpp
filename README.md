# Readability++

Readability++ is a C++23 port of Mozilla Readability focused on behavioral
parity. It has no JavaScript or Node.js dependency, and the algorithm is
independent of any concrete HTML parser or DOM implementation.

The current reference is Mozilla Readability commit
`ab4027a8b37669745016869a37a504727992b2ba`. Its complete 130-page fixture
corpus passes for content, metadata, and readerability.

## Public API

```cpp
#include <readability/lightweight_dom.hpp>
#include <readability/readability.hpp>

readability::LightweightDomParser parser;
auto document = parser.parse(html, page_url);

readability::Readability reader(*document);
if (auto article = reader.parse()) {
  // article->title, byline, dir, lang, content, text_content, length,
  // excerpt, site_name, and published_time
}

bool likely = readability::is_probably_readerable(*document);
```

Applications with an existing DOM implement `readability::dom::Document` and
`readability::dom::Node`, then pass that adapter directly to the algorithm. In
particular, a Lexbor-backed application can supply its own adapter without
Readability++ linking Lexbor. See [the architecture](docs/architecture.md) and
[the audited DOM surface](docs/dom-audit.md). The complete contracts, options,
result fields, and adapter requirements are listed in the
[API reference](docs/api-reference.md).

`Readability::parse()` mutates the supplied document, matching Mozilla's API.
The document must therefore outlive the reader and parse call. Nodes are owned
by the document; pointers exposed by the abstraction are non-owning and stable
for the document lifetime.

## Lightweight parser

`LightweightDomParser` is the MPL-2.0 port of Mozilla's `JSDOMParser.js`. It is
a reference DOM adapter and allows standalone use for suitable input. Like the
upstream parser, it is deliberately not a general HTML5 error-recovering
parser:

- input must be properly formed HTML/XML (serialized DOM input is ideal);
- node-list results are snapshots, not live collections;
- it does not execute scripts, load resources, apply CSS, or implement a full
  browser DOM.

Use a production HTML parser through an adapter for arbitrary web input.

## Build and test

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Standalone tests compile with strict warnings and, by default, AddressSanitizer
and UndefinedBehaviorSanitizer. The project-specific options are:

```text
READABILITY_BUILD_TESTS        ON when built standalone, OFF as a dependency
READABILITY_BUILD_EXAMPLES     ON when built standalone, OFF as a dependency
READABILITY_ENABLE_SANITIZERS  follows READABILITY_BUILD_TESTS by default
```

This keeps `FetchContent` integration lightweight while retaining strict
standalone development defaults.

To consume the tagged release directly:

```cmake
include(FetchContent)
FetchContent_Declare(
  readability
  SYSTEM
  GIT_REPOSITORY https://github.com/L-A-Marchetti/readability-cpp.git
  GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(readability)

target_link_libraries(your_target PRIVATE readability::readability)
```

## Example

The [`readability_extract_example`](examples/extract.cpp) executable parses a
complete HTML document, stores it behind `std::unique_ptr<dom::Document>`, runs
the parser-independent API, and prints the extracted metadata, readable HTML,
and readable text:

```sh
cmake -B build
cmake --build build --target readability_extract_example
./build/readability_extract_example
```

For installation:

```sh
cmake -B build-release \
  -DREADABILITY_BUILD_TESTS=OFF \
  -DREADABILITY_BUILD_EXAMPLES=OFF \
  -DREADABILITY_ENABLE_SANITIZERS=OFF
cmake --build build-release
cmake --install build-release --prefix /your/prefix
```

Consumers may then use `find_package(readability CONFIG REQUIRED)` and link
`readability::readability`.

Parity details and the current result are in [docs/parity.md](docs/parity.md).

## Licensing

The algorithm and most of the project are Apache-2.0; see [LICENSE.md](LICENSE.md)
and [NOTICE](NOTICE). The lightweight DOM port is MPL-2.0 and retains its
file-level notice. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
