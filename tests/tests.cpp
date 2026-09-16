// SPDX-License-Identifier: Apache-2.0
#include "readability/lightweight_dom.hpp"
#include "readability/readability.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using readability::dom::Node;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

Node* by_id(readability::dom::Document& document, std::string_view id) {
  for (Node* node : document.elements_by_tag_name("*")) if (node->id() == id) return node;
  return nullptr;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read " + path.string());
  std::ostringstream data; data << input.rdbuf();
  std::string result = data.str();
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) result.pop_back();
  return result;
}

std::string collapse(std::string value) {
  std::string out; bool space = false;
  for (char raw : value) {
    const auto c = static_cast<unsigned char>(raw);
    if (std::isspace(c) != 0) { if (!space) out.push_back(' '); space = true; }
    else { out.push_back(raw); space = false; }
  }
  return out;
}

std::string json_unescape(std::string value) {
  std::string out;
  for (std::size_t i = 0U; i < value.size(); ++i) {
    if (value[i] != '\\' || i + 1U >= value.size()) { out.push_back(value[i]); continue; }
    const char next = value[++i];
    if (next == 'n') out.push_back('\n'); else if (next == 'r') out.push_back('\r'); else if (next == 't') out.push_back('\t');
    else if (next == '"') out.push_back('"'); else if (next == '\\') out.push_back('\\'); else if (next == '/') out.push_back('/');
    else { out.push_back('\\'); out.push_back(next); }
  }
  return out;
}

std::optional<std::string> json_string(const std::string& json, std::string_view key) {
  const std::string marker = "\"" + std::string(key) + "\"";
  std::size_t pos = json.find(marker); if (pos == std::string::npos) return std::nullopt;
  pos = json.find(':', pos + marker.size()); if (pos == std::string::npos) return std::nullopt;
  pos = json.find_first_not_of(" \t\r\n", pos + 1U); if (pos == std::string::npos || json.compare(pos, 4U, "null") == 0) return std::nullopt;
  if (json[pos] != '"') return std::nullopt;
  std::string value; bool escaped = false;
  for (++pos; pos < json.size(); ++pos) {
    const char c = json[pos];
    if (!escaped && c == '"') break;
    value.push_back(c);
    if (escaped) escaped = false; else if (c == '\\') escaped = true;
  }
  return json_unescape(value);
}

bool json_bool(const std::string& json, std::string_view key) {
  const std::string marker = "\"" + std::string(key) + "\"";
  const std::size_t key_pos = json.find(marker); if (key_pos == std::string::npos) return false;
  const std::size_t colon = json.find(':', key_pos + marker.size());
  const std::size_t value = json.find_first_not_of(" \t\r\n", colon + 1U);
  return value != std::string::npos && json.compare(value, 4U, "true") == 0;
}

void collect(const Node& node, std::vector<const Node*>& nodes) {
  if (!(node.type() == readability::dom::NodeType::text && collapse(node.text_content()) == " ")) nodes.push_back(&node);
  for (const Node* child : node.child_nodes()) collect(*child, nodes);
}

std::optional<std::string> compare_dom(const readability::dom::Document& actual, const readability::dom::Document& expected) {
  std::vector<const Node*> aa, ee;
  const Node* ar = actual.document_element() ? actual.document_element() : actual.first_child();
  const Node* er = expected.document_element() ? expected.document_element() : expected.first_child();
  if (ar) collect(*ar, aa);
  if (er) collect(*er, ee);
  for (std::size_t i = 0U; i < std::min(aa.size(), ee.size()); ++i) {
    if (aa[i]->type() != ee[i]->type() || aa[i]->tag_name() != ee[i]->tag_name()) {
      return "node mismatch at " + std::to_string(i) + " actual <" + std::string(aa[i]->tag_name()) + "> [" +
             collapse(aa[i]->text_content()).substr(0U, 100U) + "] expected <" + std::string(ee[i]->tag_name()) + "> [" +
             collapse(ee[i]->text_content()).substr(0U, 100U) + "]";
    }
    if (aa[i]->type() == readability::dom::NodeType::text) {
      std::string actual_text = collapse(aa[i]->text_content());
      std::string expected_text = collapse(ee[i]->text_content());
      if (aa[i]->next_sibling() == nullptr && ee[i]->next_sibling() == nullptr) {
        while (!actual_text.empty() && actual_text.back() == ' ') actual_text.pop_back();
        while (!expected_text.empty() && expected_text.back() == ' ') expected_text.pop_back();
      }
      if (actual_text != expected_text) return "text mismatch at " + std::to_string(i) + " actual=[" + actual_text + "] expected=[" + expected_text + "]";
    }
    if (aa[i]->type() == readability::dom::NodeType::element) {
      std::map<std::string, std::string> actual_attrs, expected_attrs;
      for (const auto& attr : aa[i]->attributes()) actual_attrs[attr.name] = attr.value;
      for (const auto& attr : ee[i]->attributes()) expected_attrs[attr.name] = attr.value;
      if (actual_attrs != expected_attrs) {
        std::string detail;
        for (const auto& [key, value] : actual_attrs) detail += " actual " + key + "=[" + value + "]";
        for (const auto& [key, value] : expected_attrs) detail += " expected " + key + "=[" + value + "]";
        return "attribute mismatch at " + std::to_string(i) + " <" + std::string(aa[i]->tag_name()) + ">" + detail;
      }
    }
  }
  if (aa.size() != ee.size()) return "node count " + std::to_string(aa.size()) + " != " + std::to_string(ee.size());
  return std::nullopt;
}

void parser_api_tests() {
  readability::LightweightDomParser parser;
  constexpr std::string_view base_html =
      R"(<html><body><p>Some text and <a class="someclass" href="#">a link</a></p><div id="foo">With a <script>With &lt; fancy " characters in it because</script> that is fun.<span>And another node to make it harder</span></div><form><input type="text"/><input type="number"/>Here's a form</form></body></html>)";
  auto base = parser.parse(base_html, "http://fakehost/");
  require(parser.error_state().empty(), parser.error_state());
  require(base->child_nodes().size() == 1U, "document child count");
  require(base->elements_by_tag_name("*").size() == 10U, "element count");
  Node* foo = by_id(*base, "foo");
  require(foo != nullptr && foo->parent() == base->body(), "body/parent hierarchy");
  require(base->body() != nullptr && base->body()->parent() == base->document_element(), "document hierarchy");
  require(base->elements_by_tag_name("p").front()->inner_html() == R"(Some text and <a class="someclass" href="#">a link</a>)", "inner HTML");
  Node* script = base->elements_by_tag_name("script").front();
  require(script->inner_html() == R"(With &lt; fancy " characters in it because)", "script inner HTML");
  require(script->text_content() == R"(With < fancy " characters in it because)", "script text");
  require(base->document_uri() == "http://fakehost/" && base->base_uri() == "http://fakehost/", "document URI");

  Node* before = foo->previous_sibling();
  Node* after = foo->next_sibling();
  require(before != nullptr && after != nullptr && before->next_sibling() == foo && after->previous_sibling() == foo, "sibling links");
  foo->remove();
  require(foo->parent() == nullptr && foo->previous_sibling() == nullptr && foo->next_sibling() == nullptr, "remove");
  require(before->next_sibling() == after && after->previous_sibling() == before, "remove sibling repair");
  base->body()->append_child(*foo);
  require(foo->previous_sibling() == after, "append detached child");
  base->body()->append_child(*after);
  require(foo->next_sibling() == after && after->previous_sibling() == foo, "append existing child");

  auto doc = parser.parse("<html><body><div><p>A</p>Some text<p>B</p></div></body></html>", "http://fakehost/");
  if (!parser.error_state().empty()) throw std::runtime_error(parser.error_state());
  Node* div = doc->elements_by_tag_name("div").front(); Node* a = div->children().front(); Node* b = div->children().back();
  Node& hr = doc->create_element("hr"); div->insert_before(hr, b);
  if (a->next_element_sibling() != &hr || hr.next_element_sibling() != b || b->previous_element_sibling() != &hr) throw std::runtime_error("element sibling operations");
  Node& replacement = doc->create_element("em");
  Node* old_text = div->child_nodes().at(1U);
  div->replace_child(replacement, *old_text);
  require(replacement.parent() == div && old_text->parent() == nullptr, "replace text with element");
  div->replace_child(*old_text, replacement);
  require(old_text->parent() == div && replacement.parent() == nullptr, "restore replaced child");
  Node& fragment = doc->create_document_fragment(); fragment.append_child(*a); fragment.append_child(*b); div->append_child(fragment);
  if (fragment.child_nodes().size() != 0U || a->parent() != div || b->parent() != div) throw std::runtime_error("document fragment insertion");
  auto moving = parser.parse("<div><p>A</p><p>B</p><p>C</p></div>");
  Node* moving_div = moving->elements_by_tag_name("div").front();
  Node* moving_a = moving_div->children().at(0U); Node* moving_b = moving_div->children().at(1U); Node* moving_c = moving_div->children().at(2U);
  moving_div->insert_before(*moving_c, moving_b);
  require(moving_a->next_sibling() == moving_c && moving_c->next_sibling() == moving_b && moving_b->next_sibling() == nullptr, "move existing child");
  moving_div->insert_before(*moving_b, moving_b);
  moving_div->replace_child(*moving_b, *moving_b);
  require(moving_c->next_sibling() == moving_b, "self insertion/replacement no-op");
  Node& unconnected = doc->create_element("span");
  bool insert_threw = false;
  try { div->insert_before(doc->create_element("i"), &unconnected); } catch (const std::runtime_error&) { insert_threw = true; }
  require(insert_threw, "insertBefore non-child must throw");

  auto escaped = parser.parse("<p>&#32;&#x20;&amp;&lt;&gt;&quot;&apos;</p>");
  if (escaped->elements_by_tag_name("p").front()->text_content() != "  &<>\"'") throw std::runtime_error("entity decoding");
  auto encoded = parser.parse(R"(<p>Hello &amp; &lt;this&gt; &quot;x&quot; &apos;y&apos;.</p>)");
  Node* encoded_text = encoded->elements_by_tag_name("p").front()->first_child();
  require(encoded_text->inner_html() == R"(Hello &amp; &lt;this&gt; &quot;x&quot; &apos;y&apos;.)", "source entity serialization");
  encoded_text->set_text_content(encoded_text->text_content());
  require(encoded_text->inner_html() == R"(Hello &amp; &lt;this&gt; "x" 'y'.)", "mutated text serialization");

  for (const auto& [href, expected] : std::array<std::pair<std::string_view, std::string_view>, 4>{
           std::pair{"relative/path", "http://fakehost/some/dir/relative/path"},
           std::pair{"/path", "http://fakehost/path"},
           std::pair{"http://absolute/", "http://absolute/"},
           std::pair{"//absolute/path", "http://absolute/path"}}) {
    auto based = parser.parse("<html><head><base href=\"" + std::string(href) + "\"></base></head><body/></html>", "http://fakehost/some/dir/");
    require(based->base_uri() == expected, "base URI resolution");
  }

  for (const auto& [source, expected] : std::array<std::pair<std::string_view, std::string_view>, 5>{
           std::pair{R"(<script><?Silly test <img src="test"></script>)", ""},
           std::pair{R"(<script><!--Silly test > <script src="foo.js"></script>--></script>)", ""},
           std::pair{R"(<script>&lt;div>Hello, I'm not really in a &lt;/div></script>)", "<div>Hello, I'm not really in a </div>"},
           std::pair{R"(<script>&lt;script src="foo.js">&lt;/script></script>)", R"(<script src="foo.js"></script>)"},
           std::pair{R"(<script>var x = '&lt;script>Hi&lt;' + '/script>';</script>)", R"(var x = '<script>Hi<' + '/script>';)"}}) {
    auto scripted = parser.parse(source);
    require(parser.error_state().empty(), "script parse error");
    require(scripted->first_child() != nullptr && scripted->first_child()->tag_name() == "SCRIPT", "script root");
    require(scripted->first_child()->text_content() == expected, "script text parsing");
    require(scripted->first_child()->children().empty(), "script element children");
  }

  auto cased = parser.parse("<DIV><svG><clippath/></svG></DIV>");
  require(cased->first_child()->tag_name() == "DIV" && cased->first_child()->first_child()->tag_name() == "SVG" && cased->first_child()->first_child()->first_child()->tag_name() == "CLIPPATH", "tag name casing");
  auto namespaced = parser.parse("<a0:html><a0:body><a0:DIV><a0:svG><a0:clippath/></a0:svG></a0:DIV></a0:body></a0:html>");
  require(parser.error_state().empty() && namespaced->document_element() == namespaced->first_child(), "namespace document root");
  require(namespaced->body() != nullptr && namespaced->elements_by_tag_name("div").front()->first_child()->tag_name() == "SVG", "namespace stripping");
  auto delayed = parser.parse("<div><input><p>I'm in an input</p></input></div>");
  require(delayed->first_child()->first_child()->first_child()->tag_name() == "P", "delayed explicit close");
}

void readerable_api_tests() {
  readability::LightweightDomParser parser;
  const auto make = [&](std::size_t repeats) { return parser.parse("<html><p id=\"main\">" + std::string(repeats, 'x') + "</p></html>"); };
  auto very_small = make(11U), small = make(132U), large = make(144U), very_large = make(600U);
  require(!readability::is_probably_readerable(*very_small) && !readability::is_probably_readerable(*small) && !readability::is_probably_readerable(*large) && readability::is_probably_readerable(*very_large), "readerable defaults");
  readability::ReaderableOptions lower_length; lower_length.minimum_content_length = 120U; lower_length.minimum_score = 0.0;
  require(!readability::is_probably_readerable(*very_small, lower_length) && readability::is_probably_readerable(*small, lower_length) && readability::is_probably_readerable(*large, lower_length), "readerable minimum content length");
  readability::ReaderableOptions higher_length; higher_length.minimum_content_length = 200U; higher_length.minimum_score = 0.0;
  require(!readability::is_probably_readerable(*large, higher_length) && readability::is_probably_readerable(*very_large, higher_length), "readerable high content length");
  readability::ReaderableOptions score; score.minimum_content_length = 0U; score.minimum_score = 11.5;
  require(!readability::is_probably_readerable(*small, score) && readability::is_probably_readerable(*large, score), "readerable minimum score");
  bool called = false; readability::ReaderableOptions hidden; hidden.visibility_checker = [&](const Node&) { called = true; return false; };
  require(!readability::is_probably_readerable(*very_large, hidden) && called, "readerable visibility callback false");
  called = false; hidden.visibility_checker = [&](const Node&) { called = true; return true; };
  require(readability::is_probably_readerable(*very_large, hidden) && called, "readerable visibility callback true");
}

void readability_api_tests() {
  readability::LightweightDomParser parser;
  auto oversized = parser.parse("<html><div>yo</div></html>");
  readability::Options limited; limited.max_elements_to_parse = 1U;
  bool threw = false;
  try { static_cast<void>(readability::Readability(*oversized, limited).parse()); } catch (const std::runtime_error& error) { threw = std::string_view(error.what()).find("2 elements found") != std::string_view::npos; }
  require(threw, "maximum element limit");

  const std::string paragraph = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nunc mollis leo lacus, vitae semper nisl ullamcorper ut.";
  auto serialized_doc = parser.parse("<html><body><p>" + paragraph + "</p></body></html>");
  readability::Options serialized_options; serialized_options.character_threshold = 20U; serialized_options.serializer = [](const Node&) { return std::string("custom serialization"); };
  const auto serialized = readability::Readability(*serialized_doc, serialized_options).parse();
  require(serialized && serialized->content == "custom serialization", "custom serializer");

  auto video_doc = parser.parse("<html><body><p>" + paragraph + "</p><iframe src=\"https://mycustomdomain.com/some-embeds\"></iframe></body></html>");
  readability::Options video_options; video_options.character_threshold = 20U; video_options.allowed_video_regex = std::regex(".*mycustomdomain\\.com.*");
  const auto video = readability::Readability(*video_doc, video_options).parse();
  require(video && video->content.find("mycustomdomain.com/some-embeds") != std::string::npos, "custom video regular expression");

  const std::string class_source = "<html><body><div class=\"article retained\"><p>" + paragraph + paragraph + paragraph + "</p><p>" + paragraph + paragraph + paragraph + "</p></div></body></html>";
  auto clean_doc = parser.parse(class_source);
  readability::Options clean_options; clean_options.classes_to_preserve = {"retained"};
  const auto cleaned = readability::Readability(*clean_doc, clean_options).parse();
  require(cleaned.has_value(), "class cleaning returned no article");
  if (cleaned->content.find("class=\"retained\"") == std::string::npos || cleaned->content.find("article retained") != std::string::npos) throw std::runtime_error("class cleaning/preservation: " + cleaned->content.substr(0U, 240U));
  auto keep_doc = parser.parse(class_source);
  readability::Options keep_options; keep_options.keep_classes = true;
  const auto kept = readability::Readability(*keep_doc, keep_options).parse();
  require(kept && kept->content.find("article retained") != std::string::npos, "keep classes option");
}
} // namespace

int main(int argc, char** argv) {
  std::size_t parser_pass = 0U, readerable_pass = 0U, content_pass = 0U, metadata_pass = 0U, api_pass = 0U;
  std::vector<std::string> failures;
  try { parser_api_tests(); parser_pass = 1U; } catch (const std::exception& error) { failures.push_back(std::string("parser API: ") + error.what()); }
  try { readerable_api_tests(); ++api_pass; } catch (const std::exception& error) { failures.push_back(std::string("readerable API: ") + error.what()); }
  try { readability_api_tests(); ++api_pass; } catch (const std::exception& error) { failures.push_back(std::string("readability API: ") + error.what()); }

  const std::filesystem::path root = READABILITY_FIXTURE_DIR;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_directory()) continue;
    const std::string name = entry.path().filename().string();
    if (argc > 1 && name != argv[1]) continue;
    try {
      const std::string source = read_file(entry.path() / "source.html");
      const std::string expected_html = read_file(entry.path() / "expected.html");
      const std::string expected_meta = read_file(entry.path() / "expected-metadata.json");
      readability::LightweightDomParser parser;
      auto doc = parser.parse(source, "http://fakehost/test/page.html");
      if (!parser.error_state().empty()) failures.push_back(name + " parser: " + parser.error_state()); else ++parser_pass;
      const bool readerable = readability::is_probably_readerable(*doc);
      if (readerable == json_bool(expected_meta, "readerable")) ++readerable_pass; else failures.push_back(name + " readerable mismatch");
      readability::Options options; options.classes_to_preserve = {"caption"};
      readability::Readability readability(*doc, std::move(options)); const auto article = readability.parse();
      if (!article) { failures.push_back(name + " returned no article"); continue; }
      auto actual_doc = parser.parse(article->content); auto expected_doc = parser.parse(expected_html);
      if (const auto mismatch = compare_dom(*actual_doc, *expected_doc)) { failures.push_back(name + " content: " + *mismatch); if (argc > 1) { std::cerr << "ACTUAL:\n" << article->content << "\nEXPECTED:\n" << expected_html << '\n'; } } else ++content_pass;
      bool metadata_ok = article->title == json_string(expected_meta, "title").value_or("") && article->byline == json_string(expected_meta, "byline") && article->excerpt == json_string(expected_meta, "excerpt") && article->site_name == json_string(expected_meta, "siteName") && article->published_time == json_string(expected_meta, "publishedTime");
      if (json_string(expected_meta, "dir") && article->dir != json_string(expected_meta, "dir")) metadata_ok = false;
      if (json_string(expected_meta, "lang") && article->lang != json_string(expected_meta, "lang")) metadata_ok = false;
      if (metadata_ok) ++metadata_pass; else {
        const auto show = [](const std::optional<std::string>& value) { return value.value_or("<null>"); };
        failures.push_back(name + " metadata mismatch title=[" + article->title + "] expected=[" + json_string(expected_meta, "title").value_or("<null>") +
                           "] byline=[" + show(article->byline) + "] expected=[" + show(json_string(expected_meta, "byline")) +
                           "] excerpt=[" + show(article->excerpt) + "] expected=[" + show(json_string(expected_meta, "excerpt")) +
                           "] site=[" + show(article->site_name) + "] expected=[" + show(json_string(expected_meta, "siteName")) +
                           "] published=[" + show(article->published_time) + "] expected=[" + show(json_string(expected_meta, "publishedTime")) + "]");
      }
    } catch (const std::exception& error) { failures.push_back(name + " exception: " + error.what()); }
  }
  std::ranges::sort(failures);
  const std::size_t fixture_total = argc > 1 ? 1U : 130U;
  std::cout << "API " << api_pass << "/2, parser " << parser_pass << '/' << fixture_total + 1U << ", readerable " << readerable_pass << '/' << fixture_total << ", content " << content_pass << '/' << fixture_total << ", metadata " << metadata_pass << '/' << fixture_total << '\n';
  for (const auto& failure : failures) std::cerr << failure << '\n';
  return failures.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}
