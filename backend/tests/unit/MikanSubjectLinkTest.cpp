#include "anime_vault/services/MikanSubjectLink.hpp"
#include "../../src/infrastructure/network/MikanPageHosts.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using anime_vault::mikanLinkedSubject;
using anime_vault::mikanSeriesId;

TEST_CASE("Mikan mirror is tried only on primary transport or HTTP failure") {
    using anime_vault::WindowsHttpResult;
    const std::string mirrorPage = "mirror page";
    for (const auto primary : {WindowsHttpResult{}, WindowsHttpResult{307, "redirect"},
                              WindowsHttpResult{503, ""}, WindowsHttpResult{200, "ambiguous page"}}) {
        std::vector<std::string> hosts;
        const auto result = anime_vault::detail::fetchMikanPage([&](const std::string& host) {
            hosts.push_back(host);
            return host == "mikanani.me" ? primary : WindowsHttpResult{200, mirrorPage};
        });
        if (primary.status == 200) {
            CHECK(hosts == std::vector<std::string>{"mikanani.me"});
            CHECK(result.body == primary.body);
        } else {
            CHECK(hosts == std::vector<std::string>{"mikanani.me", "mikanani.kas.pub"});
            CHECK(result.body == mirrorPage);
        }
    }
}

TEST_CASE("Mikan series identity requires a single trusted series RSS feed") {
    CHECK(mikanSeriesId("https://mikanani.me/RSS/Bangumi?bangumiId=4011") == 4011);
    CHECK(mikanSeriesId("https://www.mikanani.kas.pub/rss/bangumi?subgroupid=203&BangumiId=4011") == 4011);
    for (const auto* url : {"http://mikanani.me/RSS/Bangumi?bangumiId=1",
            "https://mikanani.me.evil.test/RSS/Bangumi?bangumiId=1",
            "https://mikanani.me/RSS/MyBangumi?bangumiId=1",
            "https://mikanani.me/RSS/Bangumi?bangumiId=1&bangumiId=2",
            "https://mikanani.me/RSS/Bangumi?bangumiId=1&token=x",
            "https://mikanani.me/RSS/Bangumi?bangumiId=1#part",
            "https://mikanani.me/RSS/Bangumi?bangumiId=0",
            "https://mikanani.me/RSS/Bangumi?bangumiId=99999999999999999999"})
        CHECK_FALSE(mikanSeriesId(url));
}

TEST_CASE("Mikan series page identifies the labelled subject only") {
    const std::string section = R"(<p class="bangumi-info">Bangumi番组计划链接：<br /><a class="w-other-c" target="_blank" href="https://bgm.tv/subject/255209">https://bgm.tv/subject/255209</a></p>)";
    CHECK(mikanLinkedSubject("<a href='https://bgm.tv/subject/4011'>other</a>" + section) == 255209);
    CHECK_FALSE(mikanLinkedSubject("<!--" + section + "--><script>" + section + "</script>"));
    CHECK_FALSE(mikanLinkedSubject("<p>Bangumi番组计划链接：<a href='https://bgm.tv/subject/1'>1</a></p>"));
    CHECK_FALSE(mikanLinkedSubject("<p class='bangumi-info'>相关作品：<a href='https://bgm.tv/subject/1'>1</a></p>"));
    const std::string start = "<p class='bangumi-info other'>Bangumi番组计划链接：<br><a href='";
    for (const auto* url : {"https://bangumi.tv/subject/255209", "https://chii.in/subject/255209/"})
        CHECK(mikanLinkedSubject(start + url + "'>作品</a></p>") == 255209);
    for (const auto* url : {"http://bgm.tv/subject/255209", "https://bgm.tv.evil/subject/255209",
            "https://bgm.tv@evil/subject/255209", "https://bgm.tv/subject/255209/ep", "https://bgm.tv/subject/0"})
        CHECK_FALSE(mikanLinkedSubject(start + url + "'>https://bgm.tv/subject/255209</a></p>"));
    CHECK_FALSE(mikanLinkedSubject(section + start + "https://bgm.tv/subject/4011'>other</a></p>"));
    CHECK_FALSE(mikanLinkedSubject(start + "https://bgm.tv/subject/4011'>one</a><a href='https://bgm.tv/subject/1'>two</a></p>"));
}
