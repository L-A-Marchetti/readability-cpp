#include "readability/dom.hpp"
#include "readability/lightweight_dom.hpp"
#include "readability/readability.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
constexpr std::string_view html = R"html(
<html lang="en">
  <head>
    <title>Building a quiet and useful reader mode</title>
    <meta name="author" content="Readability++ Example"/>
    <meta name="description" content="A complete extraction example using the generic DOM API."/>
  </head>
  <body>
    <nav><a href="/">Home</a><a href="/archive">Archive</a></nav>
    <main>
      <article class="story">
        <h1>Building a quiet and useful reader mode</h1>
        <p>A reader mode starts with a document, but it should not be tied to the parser that produced that document. Readability++ receives a generic DOM document and performs the same scoring, cleanup, metadata extraction, and serialization steps regardless of the concrete DOM implementation behind the interface.</p>
        <p>In this example the lightweight parser supplies the nodes. A browser can instead provide an adapter backed by Lexbor, WebKit, Qt, or another native tree. The extraction algorithm only sees readability::dom::Document and readability::dom::Node, so the ownership and parser choices remain entirely with the application.</p>
        <p>The resulting article contains a title, author, excerpt, cleaned HTML, plain text, language, direction when available, publication metadata, and a JavaScript-compatible text length. Navigation and other unlikely page furniture are removed while the prose and its useful structure remain.</p>
      </article>
    </main>
    <footer>Copyright and unrelated footer links</footer>
  </body>
</html>
)html";
}

int main() {
  readability::LightweightDomParser parser;

  // The parser is only one possible provider. From this point onward the code
  // uses the parser-independent public DOM interface.
  std::unique_ptr<readability::dom::Document> document =
      parser.parse(html, "https://example.test/articles/reader-mode");
  if (!parser.error_state().empty()) {
    std::cerr << "DOM parse error: " << parser.error_state();
    return EXIT_FAILURE;
  }

  readability::Readability reader(*document);
  const std::optional<readability::Article> article = reader.parse();
  if (!article) {
    std::cerr << "No readable article was found.\n";
    return EXIT_FAILURE;
  }

  std::cout << "Title: " << article->title << '\n';
  std::cout << "Byline: " << article->byline.value_or("(none)") << '\n';
  std::cout << "Language: " << article->lang.value_or("(none)") << '\n';
  std::cout << "Length: " << article->length << " UTF-16 code units\n\n";
  std::cout << "Readable HTML:\n" << article->content << "\n\n";
  std::cout << "Readable text:\n" << article->text_content << '\n';
}
