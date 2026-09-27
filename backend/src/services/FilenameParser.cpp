#include "anime_vault/services/FilenameParser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace anime_vault {
namespace {

struct BracketToken {
    std::string text;
    std::size_t start;
    std::size_t length;
};

struct EpisodeMatch {
    std::optional<EpisodeNumber> number;
    EpisodeType type{EpisodeType::unknown};
    std::optional<std::uint32_t> season;
    std::optional<std::uint32_t> version;
    std::size_t start{};
    bool ambiguous{};
};

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n-_");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n-_");
    return text.substr(first, last - first + 1);
}

void replaceAll(std::string& value, std::string_view from, std::string_view to) {
    std::size_t at = 0;
    while ((at = value.find(from, at)) != std::string::npos) {
        value.replace(at, from.size(), to);
        at += to.size();
    }
}

std::string removeExtension(std::string_view filename) {
    std::string stem(filename);
    const auto dot = stem.rfind('.');
    if (dot == std::string::npos) return stem;
    std::string extension = stem.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == ".mkv" || extension == ".mp4" || extension == ".avi" ||
        extension == ".mov" || extension == ".webm") {
        stem.resize(dot);
    }
    return stem;
}

std::string normalize(std::string stem) {
    static const std::regex spaces(" +");
    replaceAll(stem, "［", "[");
    replaceAll(stem, "］", "]");
    replaceAll(stem, "【", "[");
    replaceAll(stem, "】", "]");
    replaceAll(stem, "－", "-");
    replaceAll(stem, "—", "-");
    replaceAll(stem, "　", " ");
    for (char& ch : stem) {
        if (ch == '\t' || ch == '\r' || ch == '\n' || ch == '_') ch = ' ';
    }
    return trim(std::regex_replace(stem, spaces, " "));
}

std::vector<BracketToken> brackets(const std::string& text) {
    std::vector<BracketToken> result;
    static const std::regex pattern(R"(\[([^\]]*)\])");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
         it != std::sregex_iterator(); ++it) {
        result.push_back({(*it)[1].str(), static_cast<std::size_t>(it->position()),
                          static_cast<std::size_t>(it->length())});
    }
    return result;
}

std::optional<std::string> resolutionOf(const std::string& text) {
    static const std::regex pattern(R"((2160|1080|720|480)\s*p\b)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        return match[1].str() + "p";
    }
    return std::nullopt;
}

std::optional<std::uint32_t> unsignedNumber(std::string_view text) {
    std::uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

bool technicalToken(const std::string& token) {
    static const std::regex pattern(
        R"((\b(2160|1080|720|480)p\b|\b(HEVC|AVC|AAC|WebRip|CHS|CHT|SRT|x264|x265)\b))",
        std::regex::icase);
    return std::regex_search(token, pattern);
}

void extractReleaseGroup(std::string& text, ParseResult& result) {
    static const std::regex episodeToken(R"([0-9]+(?:v[0-9]+)?)", std::regex::icase);
    const auto tokens = brackets(text);
    if (tokens.empty() || tokens.front().start != 0) return;
    const auto& first = tokens.front();
    if (first.text.empty() || technicalToken(first.text) ||
        std::regex_match(first.text, episodeToken)) {
        return;
    }
    result.releaseGroup = first.text;
    text.erase(0, first.length);
    text = trim(text);
}

std::optional<EpisodeMatch> matchPattern(const std::string& text, const std::regex& pattern,
                                         std::size_t episodeGroup, EpisodeType type,
                                         bool ambiguous = false) {
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return std::nullopt;
    const auto number = EpisodeNumber::parse(match[episodeGroup].str());
    if (!number) return std::nullopt;
    EpisodeMatch found;
    found.number = number;
    found.type = type;
    found.start = static_cast<std::size_t>(match.position());
    found.ambiguous = ambiguous;
    return found;
}

std::optional<EpisodeMatch> detectEpisode(const std::string& text) {
    const auto insensitive = std::regex::icase;
    static const std::regex seasonPattern(
        R"((?:^|[ -])S([0-9]{1,2})E([0-9]{1,3})(?=$|[ \[]))", insensitive);
    static const std::regex epPattern(R"((?:^|[ -])EP?([0-9]{1,3})(?=$|[ \[]))", insensitive);
    static const std::regex bracketPattern(R"(([0-9]{1,3})(?:v([0-9]+))?)", insensitive);
    static const std::regex dashPattern(R"( - ([0-9]{1,3})(?=$|[ \[]))");
    static const std::regex decimalPattern(R"( - ([0-9]{1,3}\.[0-9])(?=$|[ \[]))");
    static const std::regex specialPattern(
        R"((?:^| - )(SP|OVA|NCOP|NCED)([0-9]{1,3})(?=$|[ \[]))", insensitive);
    static const std::regex barePattern(R"( ([0-9]{1,3})(?=$| \[))");

    std::smatch seasonMatch;
    if (std::regex_search(text, seasonMatch, seasonPattern)) {
        if (const auto number = EpisodeNumber::parse(seasonMatch[2].str())) {
            EpisodeMatch found;
            found.number = number;
            found.type = EpisodeType::normal;
            found.season = unsignedNumber(seasonMatch[1].str());
            found.start = static_cast<std::size_t>(seasonMatch.position());
            return found;
        }
    }
    if (auto found = matchPattern(text, epPattern, 1, EpisodeType::normal)) return found;

    for (const auto& token : brackets(text)) {
        std::smatch match;
        if (std::regex_match(token.text, match, bracketPattern)) {
            EpisodeMatch found;
            found.number = EpisodeNumber::parse(match[1].str());
            found.type = EpisodeType::normal;
            found.start = token.start;
            if (match[2].matched) {
                found.version = unsignedNumber(match[2].str());
                if (!found.version) return std::nullopt;
            }
            return found;
        }
    }

    if (auto found = matchPattern(text, dashPattern, 1, EpisodeType::normal)) return found;
    if (auto found = matchPattern(text, decimalPattern, 1, EpisodeType::normal)) return found;

    std::smatch specialMatch;
    if (std::regex_search(text, specialMatch, specialPattern)) {
        auto kind = specialMatch[1].str();
        std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        EpisodeMatch found;
        found.number = EpisodeNumber::parse(specialMatch[2].str());
        found.type = EpisodeType::sp;
        found.start = static_cast<std::size_t>(specialMatch.position());
        if (kind == "OVA") found.type = EpisodeType::ova;
        else if (kind == "NCOP") found.type = EpisodeType::ncop;
        else if (kind == "NCED") found.type = EpisodeType::nced;
        return found;
    }

    if (auto found = matchPattern(text, barePattern, 1, EpisodeType::normal, true)) return found;
    return std::nullopt;
}

std::string cleanTitle(const std::string& text, std::size_t end) {
    auto title = trim(text.substr(0, end));
    const auto tokens = brackets(title);
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (technicalToken(it->text)) title.erase(it->start, it->length);
    }
    title = trim(title);
    while (!title.empty() && title.back() == '.') title.pop_back();
    return trim(title);
}

}  // namespace

ParseResult FilenameParser::parse(std::string_view filename) const {
    static const std::regex multiplePattern(
        R"( - [0-9]{1,3}(?:-[0-9]{1,3}|\+[0-9]{1,3})(?=$|[ \[]))");
    static const std::regex batchPattern(R"( Complete Batch(?=$| \[))", std::regex::icase);
    ParseResult result;
    result.originalName = filename;
    if (filename.find_first_of("/\\:") != std::string_view::npos) {
        result.warnings.push_back("invalid_filename");
        return result;
    }

    auto text = normalize(removeExtension(filename));
    if (text.empty() || text == "[]" || text == "...") {
        result.warnings.push_back("invalid_filename");
        return result;
    }
    result.resolution = resolutionOf(text);
    extractReleaseGroup(text, result);

    std::smatch multiple;
    if (std::regex_search(text, multiple, multiplePattern)) {
        result.title = cleanTitle(text, static_cast<std::size_t>(multiple.position()));
        result.warnings.push_back("multiple_episodes");
        return result;
    }

    const auto episode = detectEpisode(text);
    if (episode) {
        result.episode = episode->number;
        result.episodeType = episode->type;
        result.season = episode->season;
        result.version = episode->version;
        result.title = cleanTitle(text, episode->start);
        result.confidence = episode->ambiguous ? 0.70 : 0.95;
        if (episode->ambiguous) result.warnings.push_back("ambiguous_episode");
    } else {
        std::smatch batch;
        if (std::regex_search(text, batch, batchPattern)) {
            result.title = cleanTitle(text, static_cast<std::size_t>(batch.position()));
            result.warnings.push_back("batch_release");
        } else {
            result.title = cleanTitle(text, text.size());
            result.warnings.push_back("episode_missing");
        }
    }
    if (result.title.empty()) {
        result.confidence = 0.0;
        result.warnings.push_back("title_missing");
    }
    return result;
}

}  // namespace anime_vault
