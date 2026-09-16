/*
 * Copyright (c) 2010 Arc90 Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Ported and substantially modified from Mozilla Readability.js.
#include "readability/readability.hpp"
#include "text_length.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <charconv>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace readability {
namespace {
using dom::Node;
const std::regex unlikely("-ad-|ai2html|banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|extra|footer|gdpr|header|legends|menu|related|remark|replies|rss|shoutbox|sidebar|skyscraper|social|sponsor|supplemental|ad-break|agegate|pagination|pager|popup|yom-remote", std::regex::icase);
const std::regex maybe("and|article|body|column|content|main|mathjax|shadow", std::regex::icase);
const std::regex positive("article|body|content|entry|hentry|h-entry|main|page|pagination|post|text|blog|story", std::regex::icase);
const std::regex negative("-ad-|hidden|^hid$| hid$| hid |^hid |banner|combx|comment|com-|contact|footer|gdpr|masthead|media|meta|outbrain|promo|related|scroll|share|shoutbox|sidebar|skyscraper|sponsor|shopping|tags|widget", std::regex::icase);
const std::regex byline_re("byline|author|dateline|writtenby|p-author", std::regex::icase);
const std::regex share_re(R"((\b|_)(share|sharedaddy)(\b|_))", std::regex::icase);
const std::regex video_re(R"(//(www\.)?((dailymotion|youtube|youtube-nocookie|player\.vimeo|v\.qq|bilibili|live.bilibili)\.com|(archive|upload\.wikimedia)\.org|player\.twitch\.tv))", std::regex::icase);
const std::regex image_re(R"(\.(jpg|jpeg|png|webp))", std::regex::icase);
const std::regex ad_words(R"(^(ad(vertising|vertisement)?|pub(licité)?|werb(ung)?|广告|Реклама|Anuncio)$)", std::regex::icase);
const std::regex loading_words(R"(^((loading|正在加载|Загрузка|chargement|cargando)(…|\.\.\.)?)$)", std::regex::icase);
const std::unordered_set<std::string> score_tags{"SECTION","H2","H3","H4","H5","H6","P","TD","PRE"};
const std::unordered_set<std::string> block_tags{"BLOCKQUOTE","DL","DIV","IMG","OL","P","PRE","TABLE","UL"};
const std::unordered_set<std::string> phrasing_tags{"ABBR","AUDIO","B","BDO","BR","BUTTON","CITE","CODE","DATA","DATALIST","DFN","EM","EMBED","I","IMG","INPUT","KBD","LABEL","MARK","MATH","METER","NOSCRIPT","OBJECT","OUTPUT","PROGRESS","Q","RUBY","SAMP","SCRIPT","SELECT","SMALL","SPAN","STRONG","SUB","SUP","TEXTAREA","TIME","VAR","WBR"};
const std::unordered_set<std::string> unlikely_roles{"menu","menubar","complementary","navigation","alert","alertdialog","dialog"};
const std::unordered_set<std::string> alter_exceptions{"DIV","ARTICLE","SECTION","P","OL","UL"};
const std::array<std::string_view,12> presentation_attrs{"align","background","bgcolor","border","cellpadding","cellspacing","frame","hspace","rules","style","valign","vspace"};

std::size_t whitespace_width(std::string_view value,std::size_t index){
  const auto byte=[&](std::size_t offset){return static_cast<unsigned char>(value[index+offset]);};
  if(std::isspace(byte(0))!=0)return 1U;
  if(index+1U<value.size()&&byte(0)==0xc2U&&byte(1)==0xa0U)return 2U;
  if(index+2U>=value.size())return 0U;
  if(byte(0)==0xe1U&&byte(1)==0x9aU&&byte(2)==0x80U)return 3U;
  if(byte(0)==0xe2U&&byte(1)==0x80U&&((byte(2)>=0x80U&&byte(2)<=0x8aU)||byte(2)==0xa8U||byte(2)==0xa9U||byte(2)==0xafU))return 3U;
  if(byte(0)==0xe2U&&byte(1)==0x81U&&byte(2)==0x9fU)return 3U;
  if(byte(0)==0xe3U&&byte(1)==0x80U&&byte(2)==0x80U)return 3U;
  if(byte(0)==0xefU&&byte(1)==0xbbU&&byte(2)==0xbfU)return 3U;
  return 0U;
}
std::string trim(std::string value) {
  std::size_t first=0U,last_nonspace=0U;
  while(first<value.size()){
    const auto width=whitespace_width(value,first);
    if(width==0U)break;
    first+=width;
  }
  if(first==value.size())return{};
  for(std::size_t index=first;index<value.size();){
    const auto width=whitespace_width(value,index);
    if(width==0U){++index;last_nonspace=index;}else index+=width;
  }
  return value.substr(first,last_nonspace-first);
}
std::string collapse(std::string value) {
  std::string result; bool space=false;
  for(std::size_t index=0U;index<value.size();){const auto width=whitespace_width(value,index);if(width>0U){if(!space)result.push_back(' ');space=true;index+=width;}else{result.push_back(value[index]);space=false;++index;}}
  return result;
}
std::string lower(std::string value){std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
bool contains(std::string_view value,std::string_view needle){return value.find(needle)!=std::string_view::npos;}
std::size_t word_count(const std::string& value){std::size_t count=0;bool word=false;for(char raw:value){const auto c=static_cast<unsigned char>(raw);if(std::isspace(c)!=0)word=false;else if(!word){++count;word=true;}}return count;}
std::size_t comma_parts(const std::string& value){
  std::size_t count=1U;
  for(const std::string_view comma:{",","،","﹐","︐","︑","⹁","⸴","⸲","，"}){
    std::size_t position=0U;
    while((position=value.find(comma,position))!=std::string::npos){++count;position+=comma.size();}
  }
  return count;
}
bool style_hidden(const Node& node){const auto style=lower(node.attribute("style").value_or(""));return contains(style,"display:none")||contains(style,"display: none")||contains(style,"visibility:hidden")||contains(style,"visibility: hidden");}
std::optional<std::string> first_nonempty(std::initializer_list<std::optional<std::string>> values){for(const auto& value:values)if(value&&!value->empty())return value;return std::nullopt;}
void append_code_point(std::string& out,unsigned int cp){if(cp==0U||cp>0x10ffffU||(cp>=0xd800U&&cp<=0xdfffU))cp=0xfffdU;if(cp<=0x7fU)out.push_back(static_cast<char>(cp));else if(cp<=0x7ffU){out.push_back(static_cast<char>(0xc0U|(cp>>6U)));out.push_back(static_cast<char>(0x80U|(cp&0x3fU)));}else if(cp<=0xffffU){out.push_back(static_cast<char>(0xe0U|(cp>>12U)));out.push_back(static_cast<char>(0x80U|((cp>>6U)&0x3fU)));out.push_back(static_cast<char>(0x80U|(cp&0x3fU)));}else{out.push_back(static_cast<char>(0xf0U|(cp>>18U)));out.push_back(static_cast<char>(0x80U|((cp>>12U)&0x3fU)));out.push_back(static_cast<char>(0x80U|((cp>>6U)&0x3fU)));out.push_back(static_cast<char>(0x80U|(cp&0x3fU)));}}
std::string unescape_html(std::string value){std::string out;for(std::size_t i=0;i<value.size();){if(value[i]!='&'){out.push_back(value[i++]);continue;}const auto end=value.find(';',i+1U);if(end==std::string::npos){out.push_back(value[i++]);continue;}const auto entity=std::string_view(value).substr(i+1U,end-i-1U);bool handled=true;if(entity=="quot")out.push_back('"');else if(entity=="amp")out.push_back('&');else if(entity=="apos")out.push_back('\'');else if(entity=="lt")out.push_back('<');else if(entity=="gt")out.push_back('>');else if(entity.starts_with('#')){auto digits=entity.substr(1U);int base=10;if(!digits.empty()&&(digits.front()=='x'||digits.front()=='X')){base=16;digits.remove_prefix(1U);}unsigned int cp=0;const auto [ptr,error]=std::from_chars(digits.data(),digits.data()+digits.size(),cp,base);if(error==std::errc{}&&ptr==digits.data()+digits.size())append_code_point(out,cp);else handled=false;}else handled=false;if(!handled){out.append(value.substr(i,end-i+1U));}i=end+1U;}return out;}
std::string json_string(const std::string& json,std::string_view key){const std::regex expression("\\\""+std::string(key)+"\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"",std::regex::icase);std::smatch match;if(!std::regex_search(json,match,expression))return{};const std::string encoded=match[1].str();std::string out;for(std::size_t i=0;i<encoded.size();++i){if(encoded[i]!='\\'||i+1U>=encoded.size()){out.push_back(encoded[i]);continue;}const char escape=encoded[++i];if(escape=='n')out.push_back('\n');else if(escape=='r')out.push_back('\r');else if(escape=='t')out.push_back('\t');else if(escape=='"'||escape=='\\'||escape=='/')out.push_back(escape);else if(escape=='u'&&i+4U<encoded.size()){unsigned int cp=0;const auto* begin=encoded.data()+i+1U;const auto [ptr,error]=std::from_chars(begin,begin+4,cp,16);if(error==std::errc{}&&ptr==begin+4){append_code_point(out,cp);i+=4U;}}else out.push_back(escape);}return trim(out);}
std::string top_json_string(const std::string& json,std::string_view wanted){int depth=0;bool in_string=false,escaped=false;for(std::size_t i=0;i<json.size();++i){const char c=json[i];if(in_string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')in_string=false;continue;}if(c=='"'){if(depth==1){const std::size_t end=json.find('"',i+1U);if(end==std::string::npos)return{};const auto key=std::string_view(json).substr(i+1U,end-i-1U);std::size_t colon=json.find(':',end+1U);if(key==wanted&&colon!=std::string::npos){const auto value=json.find_first_not_of(" \t\r\n",colon+1U);if(value!=std::string::npos&&json[value]=='"')return json_string(json.substr(i),wanted);}i=end;}else in_string=true;}else if(c=='{'||c=='[')++depth;else if(c=='}'||c==']')--depth;}return{};}
std::string article_json_object(const std::string& json){const std::regex type_pattern(R"rx("@type"\s*:\s*"(?:Article|AdvertiserContentArticle|NewsArticle|AnalysisNewsArticle|AskPublicNewsArticle|BackgroundNewsArticle|OpinionNewsArticle|ReportageNewsArticle|ReviewNewsArticle|Report|SatiricalArticle|ScholarlyArticle|MedicalScholarlyArticle|SocialMediaPosting|BlogPosting|LiveBlogPosting|DiscussionForumPosting|TechArticle|APIReference)")rx");std::smatch match;if(!std::regex_search(json,match,type_pattern))return{};const auto type_pos=static_cast<std::size_t>(match.position());std::vector<std::size_t> stack;bool in_string=false,escaped=false;for(std::size_t i=0;i<type_pos;++i){const char c=json[i];if(in_string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')in_string=false;continue;}if(c=='"')in_string=true;else if(c=='{')stack.push_back(i);else if(c=='}'&&!stack.empty())stack.pop_back();}if(stack.empty())return{};const auto begin=stack.back();int depth=0;in_string=false;escaped=false;for(std::size_t i=begin;i<json.size();++i){const char c=json[i];if(in_string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')in_string=false;continue;}if(c=='"')in_string=true;else if(c=='{')++depth;else if(c=='}'&&--depth==0)return json.substr(begin,i-begin+1U);}return{};}
std::string json_author_names(const std::string& json){const auto author=json.find("\"author\"");if(author==std::string::npos)return{};const auto value=json.find_first_not_of(" \t\r\n",json.find(':',author)+1U);if(value==std::string::npos)return{};const auto object_end=[&](std::size_t begin){int depth=0;bool in_string=false,escaped=false;for(std::size_t i=begin;i<json.size();++i){const char c=json[i];if(in_string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')in_string=false;continue;}if(c=='"')in_string=true;else if(c=='{')++depth;else if(c=='}'&&--depth==0)return i+1U;}return json.size();};if(json[value]=='{')return top_json_string(json.substr(value,object_end(value)-value),"name");if(json[value]!='[')return{};std::string result;int array_depth=0;bool in_string=false,escaped=false;for(std::size_t i=value;i<json.size();++i){const char c=json[i];if(in_string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')in_string=false;continue;}if(c=='"')in_string=true;else if(c=='[')++array_depth;else if(c==']'&&--array_depth==0)break;else if(c=='{'&&array_depth==1){const auto end=object_end(i);const auto name=top_json_string(json.substr(i,end-i),"name");if(!name.empty()){if(!result.empty())result+=", ";result+=name;}i=end-1U;}}return result;}
std::string normalize_url(std::string value){
  if(value.starts_with("file:///")){const auto pipe=value.find('|',8U);if(pipe!=std::string::npos)value[pipe]=':';return value;}
  const auto scheme=value.find("://");
  if(scheme==std::string::npos)return value;
  auto root=value.find_first_of("/?#",scheme+3U);
  if(root==std::string::npos)return value+'/';
  if(value[root]!='/' ){value.insert(root,1U,'/');}
  const auto authority_begin=scheme+3U;
  const auto user=value.rfind('@',root);
  const auto host_begin=user!=std::string::npos&&user>=authority_begin?user+1U:authority_begin;
  const auto port=value.find(':',host_begin);
  const auto host_end=port!=std::string::npos&&port<root?port:root;
  std::ranges::transform(value.begin()+static_cast<std::ptrdiff_t>(host_begin),value.begin()+static_cast<std::ptrdiff_t>(host_end),value.begin()+static_cast<std::ptrdiff_t>(host_begin),[](unsigned char character){return static_cast<char>(std::tolower(character));});
  std::size_t pos=root;
  while((pos=value.find("/./",pos))!=std::string::npos)value.erase(pos,2U);
  while((pos=value.find("/../",root))!=std::string::npos){
    if(pos==root){value.erase(root,3U);continue;}
    const auto previous=value.rfind('/',pos-1U);
    if(previous==std::string::npos||previous<root){value.erase(root,3U);continue;}
    value.erase(previous,pos+3U-previous);
  }
  return value;
}
} // namespace

struct Metadata{std::string title;std::optional<std::string> byline,excerpt,site_name,published_time;};
struct Attempt{Node* content{};std::size_t length{};};

struct Readability::Impl {
  static constexpr unsigned strip_unlikely=1U,weight_classes=2U,conditional_cleaning_flag=4U;
  dom::Document& document; Options options; unsigned flags{7U}; std::string article_title;
  std::optional<std::string> article_byline,article_dir,article_lang; Metadata metadata;
  std::unordered_map<Node*,double> scores; std::unordered_map<Node*,bool> data_tables; std::vector<Attempt> attempts;
  Impl(dom::Document& document_value,Options options_value):document(document_value),options(std::move(options_value)){}
  bool flag(unsigned value)const{return(flags&value)!=0U;}
  std::string inner_text(const Node& node,bool normalize=true)const{auto text=trim(node.text_content());return normalize?collapse(std::move(text)):text;}
  bool visible(const Node& node)const{return!style_hidden(node)&&!node.has_attribute("hidden")&&(node.attribute("aria-hidden")!="true"||contains(node.class_name(),"fallback-image"));}
  Node* next_node(Node* node,bool ignore=false)const{if(!node)return nullptr;if(!ignore&&node->first_element_child())return node->first_element_child();if(node->next_element_sibling())return node->next_element_sibling();do{node=node->parent();}while(node&&!node->next_element_sibling());return node?node->next_element_sibling():nullptr;}
  Node* remove_and_next(Node* node)const{Node* result=next_node(node,true);node->remove();return result;}
  std::vector<Node*> all(Node& root,std::initializer_list<std::string_view> tags)const{std::vector<Node*> result;for(auto tag:tags){auto found=root.elements_by_tag_name(tag);result.insert(result.end(),found.begin(),found.end());}return result;}
  bool ancestor_tag(const Node& start,std::string_view tag,int max_depth=3)const{int depth=0;for(Node* node=start.parent();node;node=node->parent(),++depth){if(max_depth>0&&depth>max_depth)return false;if(node->tag_name()==tag)return true;}return false;}
  bool whitespace(const Node& node)const{return(node.type()==dom::NodeType::text&&trim(node.text_content()).empty())||dom::tag_is(&node,"BR");}
  bool phrasing(const Node& node)const{if(node.type()==dom::NodeType::text||phrasing_tags.contains(std::string(node.tag_name())))return true;if(node.tag_name()=="A"||node.tag_name()=="DEL"||node.tag_name()=="INS")return std::ranges::all_of(node.child_nodes(),[&](const Node* child){return phrasing(*child);});return false;}
  bool child_block(const Node& node)const{return std::ranges::any_of(node.child_nodes(),[&](const Node* child){return block_tags.contains(std::string(child->tag_name()))||child_block(*child);});}
  bool single_tag(const Node& node,std::string_view tag)const{const auto elements=node.children();return elements.size()==1U&&elements.front()->tag_name()==tag&&std::ranges::none_of(node.child_nodes(),[](const Node* child){return child->type()==dom::NodeType::text&&!trim(child->text_content()).empty();});}
  bool empty_element(const Node& node)const{if(node.type()!=dom::NodeType::element||!trim(node.text_content()).empty())return false;const auto children=node.children();return children.empty()||children.size()==node.elements_by_tag_name("br").size()+node.elements_by_tag_name("hr").size();}
  double link_density(const Node& node)const{const auto total=detail::javascript_string_length(inner_text(node));if(total==0U)return 0;double links=0;for(Node* link:node.elements_by_tag_name("a")){const auto href=link->attribute("href");links+=static_cast<double>(detail::javascript_string_length(inner_text(*link)))*((href&&href->starts_with('#')&&href->size()>1U)?0.3:1.0);}return links/static_cast<double>(total);}
  int class_weight(const Node& node)const{if(!flag(weight_classes))return 0;int weight=0;for(const auto& value:{node.class_name(),node.id()}){if(std::regex_search(value,negative))weight-=25;if(std::regex_search(value,positive))weight+=25;}return weight;}
  void initialize(Node& node){double score=0;const auto tag=node.tag_name();if(tag=="DIV")score+=5;else if(tag=="PRE"||tag=="TD"||tag=="BLOCKQUOTE")score+=3;else if(tag=="ADDRESS"||tag=="OL"||tag=="UL"||tag=="DL"||tag=="DD"||tag=="DT"||tag=="LI"||tag=="FORM")score-=3;else if(tag=="H1"||tag=="H2"||tag=="H3"||tag=="H4"||tag=="H5"||tag=="H6"||tag=="TH")score-=5;scores[&node]=score+class_weight(node);}
  double similarity(std::string a,std::string b)const{const auto tokenize=[](std::string input){std::vector<std::string> out;std::string token;for(char raw:lower(std::move(input))){const auto c=static_cast<unsigned char>(raw);if(std::isalnum(c)!=0||raw=='_')token.push_back(raw);else if(!token.empty()){out.push_back(std::move(token));token.clear();}}if(!token.empty())out.push_back(std::move(token));return out;};const auto aa=tokenize(std::move(a)),bb=tokenize(std::move(b));if(aa.empty()||bb.empty())return 0;std::size_t total=bb.size()-1U,diff=0U,unique=0U;for(const auto& token:bb){total+=token.size();if(std::ranges::find(aa,token)==aa.end()){diff+=token.size();++unique;}}if(unique>1U)diff+=unique-1U;return total?1.0-static_cast<double>(diff)/static_cast<double>(total):0;}
  bool valid_byline(const Node& node,const std::string& match)const{const auto rel=node.attribute("rel"),item=node.attribute("itemprop");const auto length=detail::javascript_string_length(trim(node.text_content()));return(rel=="author"||(item&&contains(*item,"author"))||std::regex_search(match,byline_re))&&length>0U&&length<100U;}
  std::string byline_text(Node& node)const{Node* end=next_node(&node,true);for(Node* child=next_node(&node);child&&child!=end;child=next_node(child)){const auto item=child->attribute("itemprop");if(item&&contains(*item,"name"))return trim(child->text_content());}return trim(node.text_content());}

  std::string document_title()const{
    const std::string original=trim(document.title());std::string title=original;std::size_t last=std::string::npos,separator_size=0U;bool hierarchical=false;
    for(const std::string_view separator:{" | "," - "," – "," — "," \\ "," / "," > "," » "}){const auto position=original.rfind(separator);if(position!=std::string::npos&&(last==std::string::npos||position>last)){last=position;separator_size=separator.size();}if((separator==" \\ "||separator==" / "||separator==" > "||separator==" » ")&&position!=std::string::npos)hierarchical=true;}
    if(last!=std::string::npos){title=original.substr(0,last);if(word_count(title)<3U)title=original.substr(last+separator_size);}
    else if(contains(title,": ")){bool found=false;for(Node* heading:all(const_cast<dom::Document&>(document),{"h1","h2"}))if(trim(heading->text_content())==title)found=true;if(!found){title=original.substr(original.rfind(':')+1U);if(word_count(title)<3U)title=original.substr(original.find(':')+1U);else if(word_count(original.substr(0,original.find(':')))>5U)title=original;}}
    else if(detail::javascript_string_length(title)>150U||detail::javascript_string_length(title)<15U){const auto h1=document.elements_by_tag_name("h1");if(h1.size()==1U)title=inner_text(*h1.front());}
    title=collapse(trim(title));if(word_count(title)<=4U&&!hierarchical)title=original;return title;
  }
  Metadata extract_metadata()const{
    std::unordered_map<std::string,std::string> values;
    for(Node* meta:document.elements_by_tag_name("meta")){const auto content_value=meta->attribute("content"),name=meta->attribute("name");if(!content_value||content_value->empty()||!name)continue;std::string normalized=lower(trim(*name));std::ranges::replace(normalized,'.',':');normalized.erase(std::remove_if(normalized.begin(),normalized.end(),[](unsigned char c){return std::isspace(c)!=0;}),normalized.end());if(std::regex_match(normalized,std::regex(R"(((dc|dcterm|og|twitter|parsely|weibo:(article|webpage))[-:]?)?(author|creator|pub-date|description|title|site_name))",std::regex::icase)))values[normalized]=trim(*content_value);}
    // `property` is a space-separated list. Upstream intentionally consumes
    // the first recognized property from each element.
    const std::regex property_pattern(R"((article|dc|dcterm|og|twitter)\s*:\s*(author|creator|description|published_time|title|site_name))",std::regex::icase);
    for(Node* meta:document.elements_by_tag_name("meta")){const auto property=meta->attribute("property"),content_value=meta->attribute("content");if(!property||!content_value)continue;std::smatch match;if(std::regex_search(*property,match,property_pattern)){std::string key=lower(match.str());key.erase(std::remove_if(key.begin(),key.end(),[](unsigned char c){return std::isspace(c)!=0;}),key.end());values[key]=trim(*content_value);}}
    Metadata result;const auto get=[&](std::string_view key)->std::optional<std::string>{const auto it=values.find(std::string(key));return it==values.end()?std::nullopt:std::optional<std::string>(it->second);};
    result.title=first_nonempty({get("dc:title"),get("dcterm:title"),get("og:title"),get("weibo:article:title"),get("weibo:webpage:title"),get("title"),get("twitter:title"),get("parsely-title")}).value_or("");
    auto article_author=get("article:author");if(article_author&&std::regex_search(*article_author,std::regex(R"(^[A-Za-z][A-Za-z0-9+.-]*:)")))article_author.reset();result.byline=first_nonempty({get("dc:creator"),get("dcterm:creator"),get("author"),get("parsely-author"),article_author});result.excerpt=first_nonempty({get("dc:description"),get("dcterm:description"),get("og:description"),get("weibo:article:description"),get("weibo:webpage:description"),get("description"),get("twitter:description")});result.site_name=get("og:site_name");result.published_time=first_nonempty({get("article:published_time"),get("parsely-pub-date")});
    if(!options.disable_json_ld)for(Node* script:document.elements_by_tag_name("script")){if(script->attribute("type")!="application/ld+json")continue;const std::string source=script->text_content();if(!contains(source,"schema.org"))continue;const std::string json=article_json_object(source);if(json.empty())continue;const auto name=top_json_string(json,"name"),headline=top_json_string(json,"headline");if(!name.empty()&&!headline.empty()&&name!=headline){const auto page_title=document_title();result.title=similarity(headline,page_title)>0.75&&similarity(name,page_title)<=0.75?headline:name;}else if(!name.empty())result.title=name;else if(!headline.empty())result.title=headline;const auto authors=json_author_names(json);if(!authors.empty())result.byline=authors;const auto description=top_json_string(json,"description");if(!description.empty())result.excerpt=description;const auto publisher_pos=json.find("\"publisher\"");if(publisher_pos!=std::string::npos){const auto publisher=json_string(json.substr(publisher_pos),"name");if(!publisher.empty())result.site_name=publisher;}const auto date=top_json_string(json,"datePublished");if(!date.empty())result.published_time=date;break;}
    if(result.title.empty()){result.title=document_title();}
    result.title=unescape_html(result.title);if(result.byline)result.byline=unescape_html(*result.byline);if(result.excerpt)result.excerpt=unescape_html(*result.excerpt);if(result.site_name)result.site_name=unescape_html(*result.site_name);if(result.published_time)result.published_time=unescape_html(*result.published_time);return result;
  }
  void remove_nodes(std::vector<Node*> nodes)const{for(auto it=nodes.rbegin();it!=nodes.rend();++it)if((*it)->parent())(*it)->remove();}
  void unwrap_images(){for(Node* image:document.elements_by_tag_name("img")){bool useful=false;for(const auto& attr:image->attributes())if(attr.name=="src"||attr.name=="srcset"||attr.name=="data-src"||attr.name=="data-srcset"||std::regex_search(attr.value,image_re))useful=true;if(!useful)image->remove();}}
  bool single_image(Node* node)const{while(node){if(node->tag_name()=="IMG")return true;if(node->children().size()!=1U||!trim(node->text_content()).empty())return false;node=node->first_element_child();}return false;}
  void unwrap_noscripts(){for(Node* noscript:document.elements_by_tag_name("noscript")){if(!single_image(noscript))continue;Node* previous=noscript->previous_element_sibling();if(!previous||!single_image(previous))continue;Node* old_image=previous->tag_name()=="IMG"?previous:(previous->elements_by_tag_name("img").empty()?nullptr:previous->elements_by_tag_name("img").front());if(!old_image)continue;Node& holder=document.create_element("div");holder.set_inner_html(noscript->inner_html());Node* replacement=holder.first_element_child();if(!replacement)continue;Node* new_image=replacement->tag_name()=="IMG"?replacement:(replacement->elements_by_tag_name("img").empty()?nullptr:replacement->elements_by_tag_name("img").front());if(!new_image)continue;for(const auto& attr:old_image->attributes()){if(attr.value.empty()||(attr.name!="src"&&attr.name!="srcset"&&!std::regex_search(attr.value,image_re)))continue;if(new_image->attribute(attr.name)==attr.value)continue;const std::string name=new_image->has_attribute(attr.name)?"data-old-"+attr.name:attr.name;new_image->set_attribute(name,attr.value);}if(previous->parent())previous->parent()->replace_child(*replacement,*previous);}}
  void replace_brs(Node& root){for(Node* br:root.elements_by_tag_name("br")){if(!br->parent())continue;const auto skip=[](Node* node){while(node&&node->type()!=dom::NodeType::element&&trim(node->text_content()).empty())node=node->next_sibling();return node;};Node* next=br->next_sibling();bool replaced=false;while((next=skip(next))&&dom::tag_is(next,"BR")){replaced=true;Node* sibling=next->next_sibling();next->remove();next=sibling;}if(!replaced)continue;Node& p=document.create_element("p");br->parent()->replace_child(p,*br);next=p.next_sibling();while(next){if(dom::tag_is(next,"BR")&&dom::tag_is(skip(next->next_sibling()),"BR"))break;if(!phrasing(*next))break;Node* sibling=next->next_sibling();p.append_child(*next);next=sibling;}while(p.last_child()&&whitespace(*p.last_child()))p.last_child()->remove();if(dom::tag_is(p.parent(),"P"))p.parent()->rename("DIV");}}
  void prep_document(){remove_nodes(document.elements_by_tag_name("style"));if(document.body())replace_brs(*document.body());for(Node* font:document.elements_by_tag_name("font"))font->rename("SPAN");}
  void clean_styles(Node& node)const{if(node.tag_name()=="SVG")return;for(auto attr:presentation_attrs)node.remove_attribute(attr);if(node.tag_name()=="TABLE"||node.tag_name()=="TH"||node.tag_name()=="TD"||node.tag_name()=="HR"||node.tag_name()=="PRE"){node.remove_attribute("width");node.remove_attribute("height");}for(Node* child:node.children())clean_styles(*child);}
  void clean(Node& root,std::string_view tag)const{const bool embed=tag=="object"||tag=="embed"||tag=="iframe";const std::regex& allowed=options.allowed_video_regex?*options.allowed_video_regex:video_re;auto nodes=root.elements_by_tag_name(tag);for(auto it=nodes.rbegin();it!=nodes.rend();++it){Node* node=*it;bool keep=false;if(embed){for(const auto& attr:node->attributes())if(std::regex_search(attr.value,allowed))keep=true;if(node->tag_name()=="OBJECT"&&std::regex_search(node->inner_html(),allowed))keep=true;}if(!keep&&node->parent())node->remove();}}
  void mark_tables(Node& root){
    const auto span=[](const Node& node,std::string_view name){
      const auto value=node.attribute(name);
      if(!value)return std::size_t{1};
      std::size_t parsed=0;
      const auto [end,error]=std::from_chars(value->data(),value->data()+value->size(),parsed);
      return error==std::errc{}&&end==value->data()+value->size()&&parsed>0U?parsed:std::size_t{1};
    };
    for(Node* table:root.elements_by_tag_name("table")){
      if(table->attribute("role")=="presentation"||table->attribute("datatable")=="0"){
        data_tables[table]=false;continue;
      }
      if(const auto summary=table->attribute("summary");summary&&!summary->empty()){
        data_tables[table]=true;continue;
      }
      const auto captions=table->elements_by_tag_name("caption");
      if(!captions.empty()&&!captions.front()->child_nodes().empty()){
        data_tables[table]=true;continue;
      }
      bool data=false;
      for(auto tag:{"col","colgroup","tfoot","thead","th"})
        if(!table->elements_by_tag_name(tag).empty())data=true;
      if(data){data_tables[table]=true;continue;}
      if(!table->elements_by_tag_name("table").empty()){
        data_tables[table]=false;continue;
      }
      std::size_t rows=0,columns=0;
      for(Node* row:table->elements_by_tag_name("tr")){
        rows+=span(*row,"rowspan");
        std::size_t current=0;
        for(Node* cell:row->elements_by_tag_name("td"))current+=span(*cell,"colspan");
        columns=std::max(columns,current);
      }
      data_tables[table]=!(rows==1U||columns==1U)&&(rows>=10U||columns>4U||rows*columns>10U);
    }
  }
  void prepare_lazy_source(Node& node)const{const auto src=node.attribute("src");if(!src||!src->starts_with("data:")||src->size()>=180U)return;bool replacement=false;for(const auto& attr:node.attributes())if(attr.name!="src"&&std::regex_search(attr.value,image_re))replacement=true;if(replacement)node.remove_attribute("src");}
  void fix_lazy(Node& root){for(Node* node:all(root,{"img","picture","figure"})){prepare_lazy_source(*node);const auto src=node->attribute("src"),srcset=node->attribute("srcset");if((src||(srcset&&*srcset!="null"))&&!contains(lower(node->class_name()),"lazy"))continue;for(const auto& attr:node->attributes()){if(attr.name=="src"||attr.name=="srcset"||attr.name=="alt")continue;std::string target;if(std::regex_search(attr.value,std::regex(R"(\.(jpg|jpeg|png|webp)\s+\d)",std::regex::icase)))target="srcset";else if(std::regex_search(attr.value,std::regex(R"(^\s*\S+\.(jpg|jpeg|png|webp)\S*\s*$)",std::regex::icase)))target="src";if(target.empty())continue;if(node->tag_name()=="IMG"||node->tag_name()=="PICTURE")node->set_attribute(target,attr.value);else if(all(*node,{"img","picture"}).empty()){Node& image=document.create_element("img");image.set_attribute(target,attr.value);node->append_child(image);}}}}
  double text_density(const Node& node,std::initializer_list<std::string_view> tags)const{const auto total=detail::javascript_string_length(inner_text(node));if(!total)return 0;std::size_t children=0;for(auto tag:tags)for(Node* child:node.elements_by_tag_name(tag))children+=detail::javascript_string_length(inner_text(*child));return static_cast<double>(children)/static_cast<double>(total);}
  void clean_conditionally(Node& root,std::string_view tag){
    if(!flag(conditional_cleaning_flag))return;
    auto nodes=root.elements_by_tag_name(tag);
    for(auto it=nodes.rbegin();it!=nodes.rend();++it){
      Node* node=*it;
      if(!node->parent())continue;
      const auto text=inner_text(*node);
      bool list=tag=="ul"||tag=="ol";
      if(!list&&!text.empty())list=text_density(*node,{"ul","ol"})>0.9;
      if(tag=="table"&&data_tables[node])continue;
      bool in_data=false;
      for(Node* parent=node->parent();parent;parent=parent->parent())
        if(parent->tag_name()=="TABLE"&&data_tables[parent])in_data=true;
      if(in_data||ancestor_tag(*node,"CODE"))continue;
      bool contains_data_table=false;
      for(Node* table:node->elements_by_tag_name("table"))
        if(data_tables[table])contains_data_table=true;
      if(contains_data_table)continue;

      const int weight=class_weight(*node);
      if(weight<0){node->remove();continue;}
      if(std::count(text.begin(),text.end(),',')>=10)continue;

      const auto p=node->elements_by_tag_name("p").size();
      const auto img=node->elements_by_tag_name("img").size();
      const auto input=node->elements_by_tag_name("input").size();
      const long long li=static_cast<long long>(node->elements_by_tag_name("li").size())-100LL;
      const double heading=text_density(*node,{"h1","h2","h3","h4","h5","h6"});
      const std::regex& allowed=options.allowed_video_regex?*options.allowed_video_regex:video_re;
      const auto embedded=all(*node,{"object","embed","iframe"});
      bool allowed_video=false;
      for(Node* embed:embedded){
        for(const auto& attr:embed->attributes())if(std::regex_search(attr.value,allowed))allowed_video=true;
        if(embed->tag_name()=="OBJECT"&&std::regex_search(embed->inner_html(),allowed))allowed_video=true;
      }
      if(allowed_video)continue;
      if(std::regex_match(text,ad_words)||std::regex_match(text,loading_words)){node->remove();continue;}

      const auto text_length=detail::javascript_string_length(text);
      const double density=link_density(*node);
      const bool figure=ancestor_tag(*node,"FIGURE");
      const double textish=text_density(*node,{"span","li","td","blockquote","dl","div","ol","p","pre","table","ul"});
      const auto embeds=embedded.size();
      bool remove=(!figure&&img>1U&&static_cast<double>(p)/static_cast<double>(img)<0.5)||
        (!list&&li>static_cast<long long>(p))||input>p/3U||
        (!list&&!figure&&heading<0.9&&text_length<25U&&(img==0U||img>2U)&&density>0)||
        (!list&&weight<25&&density>0.2+options.link_density_modifier)||
        (weight>=25&&density>0.5+options.link_density_modifier)||
        (embeds==1U&&text_length<75U)||embeds>1U||(img==0U&&textish==0);
      if(list&&remove){
        bool simple=true;
        for(Node* child:node->children())if(child->children().size()>1U)simple=false;
        if(simple&&img==node->elements_by_tag_name("li").size())remove=false;
      }
      if(remove)node->remove();
    }
  }
  void prep_article(Node& content){clean_styles(content);mark_tables(content);fix_lazy(content);clean_conditionally(content,"form");clean_conditionally(content,"fieldset");for(auto tag:{"object","embed","footer","link","aside","iframe","input","textarea","select","button"})clean(content,tag);for(Node* top:content.children()){Node* end=next_node(top,true);Node* node=next_node(top);while(node&&node!=end){if(std::regex_search(node->class_name()+" "+node->id(),share_re)&&detail::javascript_string_length(node->text_content())<500U)node=remove_and_next(node);else node=next_node(node);}}for(auto tag:{"h1","h2"})for(Node* heading:content.elements_by_tag_name(tag))if(class_weight(*heading)<0)heading->remove();clean_conditionally(content,"table");clean_conditionally(content,"ul");clean_conditionally(content,"div");for(Node* h:content.elements_by_tag_name("h1"))h->rename("H2");for(Node* p:content.elements_by_tag_name("p"))if(all(*p,{"img","embed","object","iframe"}).empty()&&inner_text(*p,false).empty())p->remove();for(Node* br:content.elements_by_tag_name("br")){Node* next=br->next_sibling();while(next&&next->type()!=dom::NodeType::element&&trim(next->text_content()).empty())next=next->next_sibling();if(dom::tag_is(next,"P"))br->remove();}for(Node* table:content.elements_by_tag_name("table")){Node* body=single_tag(*table,"TBODY")?table->first_element_child():table;if(body&&single_tag(*body,"TR")&&single_tag(*body->first_element_child(),"TD")){Node* cell=body->first_element_child()->first_element_child();cell->rename(std::ranges::all_of(cell->child_nodes(),[&](Node* child){return phrasing(*child);})?"P":"DIV");if(table->parent())table->parent()->replace_child(*cell,*table);}}}
  void clean_classes(Node& node)const{std::vector<std::string> preserve{"page"};preserve.insert(preserve.end(),options.classes_to_preserve.begin(),options.classes_to_preserve.end());std::string retained,current=node.class_name();std::size_t start=0;while(start<current.size()){const auto end=current.find_first_of(" \t\r\n",start);const auto value=current.substr(start,end-start);if(std::ranges::find(preserve,value)!=preserve.end()){if(!retained.empty())retained+=' ';retained+=value;}if(end==std::string::npos)break;start=end+1U;}if(retained.empty())node.remove_attribute("class");else node.set_attribute("class",retained);for(Node* child:node.children())clean_classes(*child);}
  void fix_srcsets(Node& content)const{for(Node* node:all(content,{"img","picture","figure","video","audio","source"})){const auto value=node->attribute("srcset");if(!value)continue;std::string result;std::size_t start=0U;while(start<value->size()){const auto comma=value->find(',',start);std::string part=value->substr(start,comma-start);const auto first=part.find_first_not_of(" \t\r\n");if(first!=std::string::npos){const auto space=part.find_first_of(" \t\r\n",first);const auto url=part.substr(first,space-first);part.replace(first,url.size(),absolute(url));}if(!result.empty())result+=',';result+=part;if(comma==std::string::npos)break;start=comma+1U;}node->set_attribute("srcset",result);}}
  void fix_srcsets_exact(Node& content)const{const std::regex pattern(R"((\S+)(\s+[\d.]+[xw])?(\s*(?:,|$)))");for(Node* node:all(content,{"img","picture","figure","video","audio","source"})){const auto value=node->attribute("srcset");if(!value)continue;std::string result;std::size_t copied=0U;for(std::sregex_iterator it(value->begin(),value->end(),pattern),end;it!=end;++it){const auto position=static_cast<std::size_t>(it->position());result+=value->substr(copied,position-copied);result+=absolute((*it)[1].str());result+=(*it)[2].str();result+=(*it)[3].str();copied=position+static_cast<std::size_t>(it->length());}result+=value->substr(copied);node->set_attribute("srcset",result);}}
  std::string absolute(const std::string& value)const{const auto base=document.base_uri();if(base==document.document_uri()&&value.starts_with('#'))return value;if(value.starts_with("http://")||value.starts_with("https://")||value.starts_with("ftp://"))return normalize_url(value);if(value.starts_with("file:///"))return normalize_url(value);if(std::regex_search(value,std::regex(R"(^[A-Za-z][A-Za-z0-9+.-]*:)")))return value;const auto scheme=base.find("://");if(scheme==std::string::npos)return value;const auto path=base.find('/',scheme+3U);const auto origin=path==std::string::npos?base:base.substr(0,path);if(value.starts_with("//"))return normalize_url(base.substr(0,scheme+1U)+value);if(value.starts_with('/'))return origin+value;const auto slash=base.rfind('/');return normalize_url((slash==std::string::npos?base+"/":base.substr(0,slash+1U))+value);}
  void fix_uris(Node& content)const{for(Node* link:content.elements_by_tag_name("a"))if(const auto href=link->attribute("href")){if(href->starts_with("javascript:")){if(link->child_nodes().size()==1U&&link->first_child()->type()==dom::NodeType::text){Node& text=document.create_text_node(link->text_content());if(link->parent())link->parent()->replace_child(text,*link);}else{Node& span=document.create_element("span");while(link->first_child())span.append_child(*link->first_child());if(link->parent())link->parent()->replace_child(span,*link);}}else link->set_attribute("href",absolute(trim(*href)));}for(Node* media:all(content,{"img","picture","figure","video","audio","source"}))for(auto attr:{"src","poster"})if(const auto value=media->attribute(attr))media->set_attribute(attr,absolute(trim(*value)));}
  void simplify(Node& content)const{Node* node=&content;while(node){if(node->parent()&&(node->tag_name()=="DIV"||node->tag_name()=="SECTION")&&!node->id().starts_with("readability")){if(empty_element(*node)){node=remove_and_next(node);continue;}if(single_tag(*node,"DIV")||single_tag(*node,"SECTION")){Node* child=node->first_element_child();for(const auto& attr:node->attributes())child->set_attribute(attr.name,attr.value);node->parent()->replace_child(*child,*node);node=child;continue;}}node=next_node(node);}}

  Node* grab_article(){Node* page=document.body();if(!page)return nullptr;const auto cached=page->inner_html();for(;;){std::vector<Node*> to_score;Node* node=document.document_element();bool remove_title=true;while(node){if(node->tag_name()=="HTML")article_lang=node->attribute("lang");const auto match=node->class_name()+" "+node->id();if(!visible(*node)||(node->attribute("aria-modal")=="true"&&node->attribute("role")=="dialog")){node=remove_and_next(node);continue;}if(!article_byline&&!metadata.byline&&valid_byline(*node,match)){article_byline=byline_text(*node);node=remove_and_next(node);continue;}if(remove_title&&(node->tag_name()=="H1"||node->tag_name()=="H2")&&similarity(article_title,inner_text(*node,false))>0.75){remove_title=false;node=remove_and_next(node);continue;}if(flag(strip_unlikely)&&std::regex_search(match,unlikely)&&!std::regex_search(match,maybe)&&!ancestor_tag(*node,"TABLE")&&!ancestor_tag(*node,"CODE")&&node->tag_name()!="BODY"&&node->tag_name()!="A"){node=remove_and_next(node);continue;}if(flag(strip_unlikely)&&unlikely_roles.contains(node->attribute("role").value_or(""))){node=remove_and_next(node);continue;}if((node->tag_name()=="DIV"||node->tag_name()=="SECTION"||node->tag_name()=="HEADER"||node->tag_name()=="H1"||node->tag_name()=="H2"||node->tag_name()=="H3"||node->tag_name()=="H4"||node->tag_name()=="H5"||node->tag_name()=="H6")&&empty_element(*node)){node=remove_and_next(node);continue;}if(score_tags.contains(std::string(node->tag_name())))to_score.push_back(node);if(node->tag_name()=="DIV"){Node* child=node->first_child();while(child){Node* next=child->next_sibling();if(phrasing(*child)){Node& fragment=document.create_document_fragment();do{next=child->next_sibling();fragment.append_child(*child);child=next;}while(child&&phrasing(*child));while(fragment.first_child()&&whitespace(*fragment.first_child()))fragment.first_child()->remove();while(fragment.last_child()&&whitespace(*fragment.last_child()))fragment.last_child()->remove();if(fragment.first_child()){Node& p=document.create_element("p");p.append_child(fragment);node->insert_before(p,next);}}child=next;}if(single_tag(*node,"P")&&link_density(*node)<0.25){Node* replacement=node->first_element_child();node->parent()->replace_child(*replacement,*node);node=replacement;to_score.push_back(node);}else if(!child_block(*node)){node->rename("P");to_score.push_back(node);}}node=next_node(node);}
      std::vector<Node*> candidates;for(Node* element:to_score){if(!element->parent()||element->parent()->tag_name().empty())continue;const auto text=inner_text(*element);const auto text_length=detail::javascript_string_length(text);if(text_length<25U)continue;std::vector<Node*> ancestors;for(Node* p=element->parent();p&&ancestors.size()<5U;p=p->parent())ancestors.push_back(p);const double content=1.0+static_cast<double>(comma_parts(text))+static_cast<double>(std::min(text_length/100U,std::size_t{3U}));for(std::size_t level=0;level<ancestors.size();++level){Node* ancestor=ancestors[level];if(ancestor->tag_name().empty()||!ancestor->parent()||ancestor->parent()->tag_name().empty())continue;if(!scores.contains(ancestor)){initialize(*ancestor);candidates.push_back(ancestor);}const double divider=level==0U?1.0:(level==1U?2.0:static_cast<double>(level*3U));scores[ancestor]+=content/divider;}}
      std::vector<Node*> top;for(Node* candidate:candidates){scores[candidate]*=1.0-link_density(*candidate);auto at=std::ranges::find_if(top,[&](Node* other){return scores[candidate]>scores[other];});top.insert(at,candidate);if(top.size()>options.top_candidates)top.pop_back();}Node* top_candidate=top.empty()?nullptr:top.front();bool created=false;if(!top_candidate||top_candidate->tag_name()=="BODY"){top_candidate=&document.create_element("div");created=true;while(page->first_child())top_candidate->append_child(*page->first_child());page->append_child(*top_candidate);initialize(*top_candidate);}else{std::vector<std::vector<Node*>> alternatives;for(std::size_t i=1U;i<top.size();++i)if(scores[top_candidate]!=0.0&&scores[top[i]]/scores[top_candidate]>=0.75){std::vector<Node*> ancestors;for(Node* p=top[i]->parent();p;p=p->parent())ancestors.push_back(p);alternatives.push_back(std::move(ancestors));}if(alternatives.size()>=3U){for(Node* p=top_candidate->parent();p&&p->tag_name()!="BODY";p=p->parent()){std::size_t count=0U;for(const auto& ancestors:alternatives)if(std::ranges::find(ancestors,p)!=ancestors.end())++count;if(count>=3U){top_candidate=p;break;}}}if(!scores.contains(top_candidate))initialize(*top_candidate);double last_score=scores[top_candidate];const double score_threshold=last_score/3.0;for(Node* p=top_candidate->parent();p&&p->tag_name()!="BODY";p=p->parent()){if(!scores.contains(p))continue;const double parent_score=scores[p];if(parent_score<score_threshold)break;if(parent_score>last_score){top_candidate=p;break;}last_score=parent_score;}while(top_candidate->parent()&&top_candidate->parent()->tag_name()!="BODY"&&top_candidate->parent()->children().size()==1U)top_candidate=top_candidate->parent();if(!scores.contains(top_candidate))initialize(*top_candidate);}
      Node& article=document.create_element("div");Node* parent=top_candidate->parent();const double threshold=std::max(10.0,scores[top_candidate]*0.2);if(parent){auto siblings=parent->children();std::size_t index=0;while(index<siblings.size()){Node* sibling=siblings[index];bool append=sibling==top_candidate;const double bonus=sibling->class_name()==top_candidate->class_name()&&!sibling->class_name().empty()?scores[top_candidate]*0.2:0;if(!append&&scores.contains(sibling)&&scores[sibling]+bonus>=threshold)append=true;if(!append&&sibling->tag_name()=="P"){const auto text=inner_text(*sibling);const auto text_length=detail::javascript_string_length(text);const auto density=link_density(*sibling);append=(text_length>80U&&density<0.25)||(text_length<80U&&!text.empty()&&density==0&&std::regex_search(text,std::regex(R"(\.( |$))")));}if(append){if(!alter_exceptions.contains(std::string(sibling->tag_name())))sibling->rename("DIV");article.append_child(*sibling);siblings=parent->children();}else++index;}}
      prep_article(article);if(created){top_candidate->set_attribute("id","readability-page-1");top_candidate->set_attribute("class","page");}else{Node& div=document.create_element("div");div.set_attribute("id","readability-page-1");div.set_attribute("class","page");while(article.first_child())div.append_child(*article.first_child());article.append_child(div);}const auto length=detail::javascript_string_length(inner_text(article));if(length<options.character_threshold){page->set_inner_html(cached);attempts.push_back({&article,length});if(flag(strip_unlikely))flags&=~strip_unlikely;else if(flag(weight_classes))flags&=~weight_classes;else if(flag(conditional_cleaning_flag))flags&=~conditional_cleaning_flag;else{const auto best=std::ranges::max_element(attempts,{},&Attempt::length);return best==attempts.end()||best->length==0U?nullptr:best->content;}scores.clear();data_tables.clear();continue;}for(Node* ancestor:{parent,top_candidate})if(ancestor&&!article_dir)article_dir=ancestor->attribute("dir");for(Node* ancestor=parent?parent->parent():nullptr;ancestor&&!article_dir;ancestor=ancestor->parent())article_dir=ancestor->attribute("dir");return &article;}}
  std::optional<Article> parse(){if(options.max_elements_to_parse>0U){const auto count=document.elements_by_tag_name("*").size();if(count>options.max_elements_to_parse)throw std::runtime_error("Aborting parsing document; "+std::to_string(count)+" elements found");}unwrap_images();unwrap_noscripts();metadata=extract_metadata();remove_nodes(all(document,{"script","noscript"}));prep_document();article_title=metadata.title;Node* content=grab_article();if(!content)return std::nullopt;fix_uris(*content);fix_srcsets_exact(*content);simplify(*content);if(!options.keep_classes)clean_classes(*content);if(!metadata.excerpt){const auto paragraphs=content->elements_by_tag_name("p");if(!paragraphs.empty())metadata.excerpt=trim(paragraphs.front()->text_content());}Article result;result.title=article_title;result.byline=metadata.byline?metadata.byline:article_byline;result.dir=article_dir;result.lang=article_lang;result.content=options.serializer?options.serializer(*content):content->inner_html();result.text_content=content->text_content();result.length=detail::javascript_string_length(result.text_content);result.excerpt=metadata.excerpt;result.site_name=metadata.site_name;result.published_time=metadata.published_time;return result;}
};

Readability::Readability(dom::Document& document,Options options):impl_(std::make_unique<Impl>(document,std::move(options))){}
Readability::~Readability()=default;Readability::Readability(Readability&&)noexcept=default;Readability& Readability::operator=(Readability&&)noexcept=default;
std::optional<Article> Readability::parse(){return impl_->parse();}
} // namespace readability
