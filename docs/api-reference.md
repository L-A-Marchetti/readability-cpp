# API reference

All public declarations live under `include/readability/` and use the
`readability` namespace. The library requires C++23.

## Headers

| Header | Purpose |
|---|---|
| `<readability/dom.hpp>` | Parser-independent DOM contracts |
| `<readability/readability.hpp>` | Readability algorithm, result, and options |
| `<readability/lightweight_dom.hpp>` | Optional lightweight reference DOM/parser |

## DOM adapter API

### Ownership and lifetime

A `dom::Document` owns every node it exposes or creates. `dom::Node*` values
are non-owning observers and must remain valid until the document is destroyed,
even after the node is detached. Readability never deletes nodes.

`Readability` stores a reference to the document. The document must outlive the
reader and the call to `parse()`. Parsing mutates the DOM, as Mozilla
Readability does, so pass a disposable document or clone it in the adapter when
the original must be preserved.

Strings crossing the interface are UTF-8. Element `tag_name()` values must be
uppercase ASCII (`"DIV"`, `"P"`, and so on). Attribute names are expected in
their normalized DOM form, normally lowercase for HTML.

Collections returned by `child_nodes()`, `children()`, `attributes()`, and
`elements_by_tag_name()` are snapshots. They do not need live-NodeList
semantics.

### `dom::NodeType`

```cpp
enum class NodeType {
  element = 1,
  text = 3,
  comment = 8,
  document = 9,
  document_fragment = 11
};
```

The numeric values mirror the browser DOM constants used by Mozilla.

### `dom::Attribute`

```cpp
struct Attribute {
  std::string name;
  std::string value;
};
```

### `dom::Node`

An adapter implements these primitive virtual operations:

```cpp
class Node {
public:
  virtual NodeType type() const noexcept = 0;
  virtual std::string_view tag_name() const noexcept = 0;
  virtual void rename(std::string_view tag) = 0;

  virtual Node* parent() const noexcept = 0;
  virtual std::vector<Node*> child_nodes() const = 0;
  virtual std::vector<Node*> children() const = 0;

  virtual std::vector<Attribute> attributes() const = 0;
  virtual std::optional<std::string> attribute(std::string_view name) const = 0;
  virtual void set_attribute(std::string_view name, std::string_view value) = 0;
  virtual void remove_attribute(std::string_view name) = 0;

  virtual std::string text_content() const = 0;
  virtual void set_text_content(std::string_view text) = 0;
  virtual std::string inner_html() const = 0;
  virtual void set_inner_html(std::string_view html) = 0;

  virtual Node& append_child(Node& child) = 0;
  virtual Node& insert_before(Node& child, Node* reference) = 0;
  virtual Node& replace_child(Node& replacement, Node& old_child) = 0;
  virtual Node& remove() = 0;
};
```

Mutation operations must support moving an already attached node. Appending a
document fragment appends its children and empties the fragment. A null
`reference` in `insert_before` means append. `replace_child` and
`insert_before(node, &node)` must behave as DOM no-ops when applicable.

The base class already implements the following conveniences from the
primitives:

- `first_child()` / `last_child()`;
- `first_element_child()` / `last_element_child()`;
- previous/next node and element siblings;
- `has_attribute()`, `class_name()`, and `id()`;
- recursive `elements_by_tag_name()`, including `"*"`.

### `dom::Document`

`Document` derives from `Node` and additionally requires:

```cpp
virtual Node* document_element() const noexcept = 0;
virtual Node* head() const noexcept = 0;
virtual Node* body() const noexcept = 0;
virtual std::string title() const = 0;
virtual std::string document_uri() const = 0;
virtual std::string base_uri() const = 0;
virtual Node& create_element(std::string_view tag) = 0;
virtual Node& create_text_node(std::string_view text) = 0;
virtual Node& create_document_fragment() = 0;
```

`base_uri()` should include any document `<base href>` resolution. Created
nodes are owned by the document and follow the same stable-address rule.

### Minimal adapter shape

```cpp
class MyNode final : public readability::dom::Node {
  // Store or reference the native DOM node and implement every virtual member.
};

class MyDocument final : public readability::dom::Document {
  // Own wrappers strongly enough to keep returned Node pointers stable.
};

MyDocument document(native_document);
readability::Readability reader(document);
auto article = reader.parse();
```

For DOMs whose native node handles are stable, an adapter commonly keeps one
wrapper per native handle in an arena or map. For ref-counted DOMs, wrappers can
retain the corresponding native node. Raw pointers in this API never convey
ownership.

## Readability API

### `readability::Article`

`parse()` returns `std::optional<Article>`. An empty optional means that no
article could be extracted.

| Field | Meaning |
|---|---|
| `title` | Extracted article title |
| `byline` | Author/byline, when available |
| `dir` | Text direction inherited from the article container |
| `lang` | Document/article language |
| `content` | Serialized extracted HTML |
| `text_content` | Plain text content of the extracted subtree |
| `length` | JavaScript-compatible UTF-16 code-unit length of `text_content` |
| `excerpt` | Metadata description or first suitable paragraph |
| `site_name` | Publisher/site name |
| `published_time` | Publication time string from upstream metadata |

Optional upstream values use `std::optional<std::string>` so missing data is
distinct from an empty string.

### `readability::Options`

| Option | Default | Effect |
|---|---:|---|
| `debug` | `false` | Reserved parity option for diagnostic behavior |
| `max_elements_to_parse` | `0` | Abort above this element count; zero disables the limit |
| `top_candidates` | `5` | Number of highest-scoring candidates retained |
| `character_threshold` | `500` | Minimum article length before fallback passes |
| `classes_to_preserve` | empty | Classes retained when cleaning output |
| `keep_classes` | `false` | Preserve every class instead of cleaning classes |
| `disable_json_ld` | `false` | Ignore JSON-LD article metadata |
| `link_density_modifier` | `0.0` | Adjust conditional link-density thresholds |
| `serializer` | empty | Custom final subtree serializer |
| `allowed_video_regex` | empty | Override the allowed embedded-video expression |

The custom serializer receives the extracted root as `const dom::Node&` and
must return the desired content string.

### `readability::Readability`

```cpp
explicit Readability(dom::Document& document, Options options = {});
std::optional<Article> parse();
```

The reader is movable but not copyable. Create a new document and reader for a
fresh parse because `parse()` consumes/mutates relevant parts of the DOM.

## Readerability heuristic

```cpp
bool is_probably_readerable(
    const dom::Document& document,
    ReaderableOptions options = {});
```

This is the fast, non-extracting heuristic corresponding to Mozilla's
`isProbablyReaderable`.

| `ReaderableOptions` field | Default | Effect |
|---|---:|---|
| `minimum_score` | `20.0` | Score required to return true |
| `minimum_content_length` | `140` | Minimum text length of a candidate |
| `visibility_checker` | built in | Optional callback deciding node visibility |

Unlike `Readability::parse()`, the heuristic accepts a const document and does
not mutate it.

## Lightweight DOM parser

```cpp
readability::LightweightDomParser parser;
std::unique_ptr<dom::Document> document = parser.parse(html, page_url);
if (!parser.error_state().empty()) {
  // Input was not accepted as properly formed HTML/XML.
}
```

`parse()` transfers ownership through `std::unique_ptr<dom::Document>`.
`error_state()` describes recoverable or fatal parser errors from the most
recent call. See the limitations in the project README before using this parser
with arbitrary web HTML.
