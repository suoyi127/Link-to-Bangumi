#pragma once
#include <string>
#include <string_view>
namespace anime_vault {
std::string foldMikanChinese(std::string_view text);
std::string mikanTitleKey(std::string_view text);
std::string foldScrapeTitle(std::string_view title);
std::string scrapeTitleKey(std::string_view title);
std::u32string normalizeScrapeTitle(std::string_view title);
int mikanSubtitleScripts(std::string_view title, std::string* cleanTitle = nullptr);
} // namespace anime_vault
