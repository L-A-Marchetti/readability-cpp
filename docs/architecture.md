# Architecture

## DOM boundary

The stable public boundary is the runtime-polymorphic `dom::Node` and
`dom::Document` interface in `include/readability/dom.hpp`. A document owns its
nodes. Returned pointers are non-owning observers whose addresses remain valid
until the document is destroyed, including after a node is detached.

Runtime polymorphism was selected after considering concepts/templates and
type erasure. Templates would make the complete algorithm implementation part
of the public headers, increase compile times, and make ABI-stable distribution
difficult. A custom type-erased value would reproduce a virtual dispatch table
and complicate mutation and identity. The explicit virtual interface is small,
easy for a browser DOM adapter to implement, and its dispatch cost is minor
beside DOM traversal, regular expressions, text processing, and serialization.

The core targets only this interface. It does not include or link Lexbor,
Gumbo, libxml2, Qt, WebKit, or the lightweight parser.

## Components

- `readability::Readability` owns algorithm state through a private
  implementation and mutates the caller-supplied document like upstream.
- `readability::is_probably_readerable` is the independent fast heuristic.
- `readability::LightweightDomParser` is the MPL-2.0 reference adapter and a
  test/input convenience. It is in the same library for distribution, but no
  core algorithm path constructs or downcasts to it.
- `Article` preserves all upstream result fields. Its `length` uses JavaScript
  UTF-16 code-unit semantics even though public strings are UTF-8.

The interface audit is recorded in [dom-audit.md](dom-audit.md).
