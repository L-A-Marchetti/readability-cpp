# DOM operation audit

This audit is based on Mozilla Readability commit
`ab4027a8b37669745016869a37a504727992b2ba` (`0.6.0-15-gab4027a`). It was
performed before fixing the public abstraction. JavaScript conveniences are
grouped by the primitive operation they require rather than copied one for one.

## Read operations

`Readability.js` and `Readability-readerable.js` require:

- document element, head, body, title, document URI and base URI;
- node type and element tag/local name;
- parent, child nodes, element children, first/last child, and adjacent node or
  element siblings;
- text content, inner HTML, ID and class;
- attribute enumeration, lookup and presence;
- descendant lookup by tag, including the `*` wildcard;
- stable node identity while score and data-table state are kept in maps.

Style visibility is intentionally represented through the serialized `style`,
`hidden`, and `aria-hidden` attributes. The algorithm does not require a CSS
engine or computed style.

## Mutation and creation operations

The algorithm requires:

- create element, text node, and document fragment;
- append and insert before;
- replace and remove;
- change an element's tag;
- set/remove attributes and set text/inner HTML;
- move existing nodes and splice document-fragment children;
- serialize the final subtree (or invoke a caller-provided serializer).

Cloning is not used by the core algorithm and is therefore not in the public
interface. The lightweight reference implementation has a private cloning
operation solely to implement `set_inner_html` across its temporary parser
document.

## Deliberately excluded APIs

Selectors, XPath, events, layout, computed CSS, networking, script execution,
live node lists, and ownership of a concrete parser are not required. URL
resolution remains part of Readability because upstream reads browser-resolved
URI properties; an adapter may expose its DOM's resolved values through the
ordinary attributes and document base URI.
