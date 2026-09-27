#include "FilenameFixture.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace anime_vault::test_support {
namespace {

using json = nlohmann::json;

[[noreturn]] void invalid(std::size_t index, std::string_view field, std::string_view reason) {
    throw std::invalid_argument("fixture[" + std::to_string(index) + "]." +
                                std::string(field) + ": " + std::string(reason));
}

const json& required(const json& item, std::size_t index, std::string_view field) {
    const auto it = item.find(std::string(field));
    if (it == item.end()) {
        invalid(index, field, "is required");
    }
    return *it;
}

std::string requiredString(const json& item, std::size_t index, std::string_view field) {
    const auto& value = required(item, index, field);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        invalid(index, field, "must be a non-empty string");
    }
    return value.get<std::string>();
}

std::optional<std::string> optionalString(const json& item, std::size_t index,
                                          std::string_view field) {
    const auto it = item.find(std::string(field));
    if (it == item.end()) {
        return std::nullopt;
    }
    if (!it->is_string() || it->get_ref<const std::string&>().empty()) {
        invalid(index, field, "must be a non-empty string when present");
    }
    return it->get<std::string>();
}

std::optional<std::uint32_t> optionalUnsigned(const json& item, std::size_t index,
                                              std::string_view field) {
    const auto it = item.find(std::string(field));
    if (it == item.end()) {
        return std::nullopt;
    }
    if (!it->is_number_unsigned() || it->get<std::uint64_t>() >
                                         std::numeric_limits<std::uint32_t>::max()) {
        invalid(index, field, "must be an unsigned 32-bit integer when present");
    }
    return it->get<std::uint32_t>();
}

EpisodeType parseEpisodeType(std::string_view value, std::size_t index) {
    if (value == "normal") return EpisodeType::normal;
    if (value == "sp") return EpisodeType::sp;
    if (value == "ova") return EpisodeType::ova;
    if (value == "ncop") return EpisodeType::ncop;
    if (value == "nced") return EpisodeType::nced;
    if (value == "unknown") return EpisodeType::unknown;
    invalid(index, "episode_type", "has an unsupported value");
}

ConfidenceBand parseConfidenceBand(std::string_view value, std::size_t index) {
    if (value == "low") return ConfidenceBand::low;
    if (value == "medium") return ConfidenceBand::medium;
    if (value == "high") return ConfidenceBand::high;
    invalid(index, "confidence_band", "has an unsupported value");
}

FilenameFixture parseFixture(const json& item, std::size_t index) {
    if (!item.is_object()) {
        invalid(index, "item", "must be an object");
    }
    static const std::unordered_set<std::string> allowed{
        "filename", "title", "season", "episode", "episode_type", "release_group",
        "resolution", "version", "confidence_band", "warnings"};
    for (auto it = item.begin(); it != item.end(); ++it) {
        if (!allowed.contains(it.key())) {
            invalid(index, it.key(), "is not a supported field");
        }
    }

    FilenameFixture fixture;
    fixture.filename = requiredString(item, index, "filename");
    if (fixture.filename.find_first_of("/\\") != std::string::npos ||
        fixture.filename.find(':') != std::string::npos) {
        invalid(index, "filename", "must contain a filename only, without a path");
    }
    fixture.title = requiredString(item, index, "title");
    fixture.season = optionalUnsigned(item, index, "season");
    const auto episode = optionalString(item, index, "episode");
    if (episode) {
        fixture.episode = EpisodeNumber::parse(*episode);
        if (!fixture.episode || fixture.episode->toString() != *episode) {
            invalid(index, "episode", "must use canonical EpisodeNumber syntax");
        }
    }
    fixture.episodeType = parseEpisodeType(requiredString(item, index, "episode_type"), index);
    fixture.releaseGroup = optionalString(item, index, "release_group");
    fixture.resolution = optionalString(item, index, "resolution");
    fixture.version = optionalUnsigned(item, index, "version");
    fixture.confidenceBand =
        parseConfidenceBand(requiredString(item, index, "confidence_band"), index);

    const auto& warnings = required(item, index, "warnings");
    if (!warnings.is_array()) {
        invalid(index, "warnings", "must be an array of strings");
    }
    for (const auto& warning : warnings) {
        if (!warning.is_string() || warning.get_ref<const std::string&>().empty()) {
            invalid(index, "warnings", "must contain only non-empty strings");
        }
        fixture.warnings.push_back(warning.get<std::string>());
    }
    return fixture;
}

}  // namespace

std::vector<FilenameFixture> loadFilenameFixtures(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open filename fixtures: " + path.string());
    }

    std::vector<std::unordered_set<std::string>> objectKeys;
    const json::parser_callback_t rejectDuplicateKeys =
        [&objectKeys](int, json::parse_event_t event, json& value) {
            if (event == json::parse_event_t::object_start) {
                objectKeys.emplace_back();
            } else if (event == json::parse_event_t::key) {
                const auto key = value.get<std::string>();
                if (!objectKeys.back().insert(key).second) {
                    throw std::invalid_argument("duplicate JSON key: " + key);
                }
            } else if (event == json::parse_event_t::object_end) {
                objectKeys.pop_back();
            }
            return true;
        };

    json document;
    try {
        document = json::parse(input, rejectDuplicateKeys);
    } catch (const json::parse_error& error) {
        throw std::invalid_argument("invalid JSON: " + std::string(error.what()));
    }
    if (!document.is_array()) {
        throw std::invalid_argument("filename fixture root must be an array");
    }

    std::vector<FilenameFixture> fixtures;
    fixtures.reserve(document.size());
    std::unordered_set<std::string> names;
    for (std::size_t index = 0; index < document.size(); ++index) {
        auto fixture = parseFixture(document[index], index);
        if (!names.insert(fixture.filename).second) {
            invalid(index, "filename", "must be unique");
        }
        fixtures.push_back(std::move(fixture));
    }
    return fixtures;
}

ConfidenceBand confidenceBand(double confidence) {
    if (!std::isfinite(confidence) || confidence < 0.0 || confidence > 1.0) {
        throw std::invalid_argument("confidence must be finite and between 0 and 1");
    }
    if (confidence >= 0.85) return ConfidenceBand::high;
    if (confidence >= 0.60) return ConfidenceBand::medium;
    return ConfidenceBand::low;
}

}  // namespace anime_vault::test_support
