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

Tests compile with strict warnings and, by default when `BUILD_TESTING` is on,
AddressSanitizer and UndefinedBehaviorSanitizer. Disable this only when needed
with `-DREADABILITY_ENABLE_SANITIZERS=OFF`.

For installation:

```sh
cmake --install build --prefix /your/prefix
```

Consumers may then use `find_package(readability CONFIG REQUIRED)` and link
`readability::readability`.

Parity details and the current result are in [docs/parity.md](docs/parity.md).

## Licensing

The algorithm and most of the project are Apache-2.0; see [LICENSE.md](LICENSE.md)
and [NOTICE](NOTICE). The lightweight DOM port is MPL-2.0 and retains its
file-level notice. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
