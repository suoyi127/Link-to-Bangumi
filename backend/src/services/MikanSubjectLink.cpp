#include "anime_vault/services/MikanSubjectLink.hpp"

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

#include <charconv>
#include <memory>
#include <regex>

namespace anime_vault {
namespace {
std::optional<std::int64_t> positiveId(std::string_view digits) {
    std::int64_t id{};
    const auto end = digits.data() + digits.size();
    const auto parsed = std::from_chars(digits.data(), end, id);
    if (parsed.ec != std::errc{} || parsed.ptr != end || id <= 0) return {};
    return id;
}

std::string_view trim(std::string_view text) {
    constexpr std::string_view space = " \t\r\n\f";
    const auto begin = text.find_first_not_of(space);
    if (begin == text.npos) return {};
    return text.substr(begin, text.find_last_not_of(space) - begin + 1);
}

std::optional<std::int64_t> subjectHref(lxb_dom_node_t* anchor) {
    size_t length{};
    const auto href = lxb_dom_element_get_attribute(lxb_dom_interface_element(anchor),
        reinterpret_cast<const lxb_char_t*>("href"), 4, &length);
    if (!href || length > 128) return {};
    const std::string_view url(reinterpret_cast<const char*>(href), length);
    for (const std::string_view prefix : {"https://bgm.tv/subject/", "https://bangumi.tv/subject/", "https://chii.in/subject/"}) {
        if (!url.starts_with(prefix)) continue;
        auto digits = url.substr(prefix.size());
        if (digits.ends_with('/')) digits.remove_suffix(1);
        return positiveId(digits);
    }
    return {};
}

bool collectSection(lxb_dom_node_t* section, std::optional<std::int64_t>& subject) {
    auto* node = section->first_child;
    while (node && node->type == LXB_DOM_NODE_TYPE_COMMENT) node = node->next;
    if (!node || node->type != LXB_DOM_NODE_TYPE_TEXT) return true;
    const auto& label = lxb_dom_interface_character_data(node)->data;
    const auto text = trim({reinterpret_cast<const char*>(label.data), label.length});
    if (text != "Bangumi番组计划链接：" && text != "Bangumi番组计划链接:") return true;

    // 只认官方字段内的直接链接，出现未知结构或不同条目时放弃同步。
    bool found = false;
    for (node = node->next; node; node = node->next) {
        if (node->type == LXB_DOM_NODE_TYPE_COMMENT || node->local_name == LXB_TAG_BR) continue;
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            const auto& value = lxb_dom_interface_character_data(node)->data;
            if (trim({reinterpret_cast<const char*>(value.data), value.length}).empty()) continue;
        }
        if (node->local_name != LXB_TAG_A) return false;
        const auto id = subjectHref(node);
        if (!id || (subject && subject != id)) return false;
        subject = id;
        found = true;
    }
    return found;
}
}

std::optional<std::int64_t> mikanSeriesId(const std::string& feedUrl) {
    if (feedUrl.size() > 1024) return {};
    static const std::regex feed(
        R"(^https://(?:www\.)?mikanani\.(?:me|kas\.pub)/RSS/Bangumi\?(?:bangumiId=([0-9]{1,19})(?:&subgroupid=([0-9]{1,19}))?|subgroupid=([0-9]{1,19})&bangumiId=([0-9]{1,19}))$)",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_match(feedUrl, match, feed)) return {};
    const auto group = match[2].matched ? match[2] : match[3];
    if (group.matched && !positiveId(group.str())) return {};
    return positiveId(match[1].matched ? match[1].str() : match[4].str());
}

std::optional<std::int64_t> mikanLinkedSubject(std::string_view html) {
    if (html.empty() || html.size() > 2 * 1024 * 1024) return {};
    const std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)> doc(
        lxb_html_document_create(), lxb_html_document_destroy);
    if (!doc || lxb_html_document_parse(doc.get(), reinterpret_cast<const lxb_char_t*>(html.data()),
            html.size()) != LXB_STATUS_OK || !doc->body) return {};
    const auto destroySections = [](lxb_dom_collection_t* collection) { lxb_dom_collection_destroy(collection, true); };
    const std::unique_ptr<lxb_dom_collection_t, decltype(destroySections)> sections(
        lxb_dom_collection_make(&doc->dom_document, 8), destroySections);
    if (!sections || lxb_dom_elements_by_class_name(lxb_dom_interface_element(doc->body), sections.get(),
            reinterpret_cast<const lxb_char_t*>("bangumi-info"), 12) != LXB_STATUS_OK) return {};
    std::optional<std::int64_t> subject;
    for (size_t i = 0; i < lxb_dom_collection_length(sections.get()); ++i) {
        auto* node = lxb_dom_collection_node(sections.get(), i);
        if (node->local_name == LXB_TAG_P && !collectSection(node, subject)) return {};
    }
    return subject;
}
}
