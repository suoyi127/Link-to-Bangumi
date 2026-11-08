using System.Text.Json;
using AnimeVault.Desktop;

internal static class SteamLoginProtocolTests
{
    internal static void Run()
    {
        var token = new string('a', 64);
        var callback = "http://127.0.0.1:32123/api/steam/login/complete";
        string[] Args(string url) => ["--steam-login", "--callback", url, "--token", token, "--profile", Path.GetFullPath("test-profile")];
        Check(SteamLoginProtocol.TryParseArguments(Args(callback), out _), "valid CLI");
        foreach (var url in new[] { "http://localhost:32123/api/steam/login/complete", "http://127.1:32123/api/steam/login/complete", callback + "?x=1", callback + "#x", callback.Replace("http:", "https:"), callback.Replace("127.0.0.1", "127.0.0.1.evil.test"), callback.Replace("127.0.0.1", "user@127.0.0.1"), callback.Replace(":32123", ":0"), callback.Replace("/api/", "/x/../api/") })
            Check(!SteamLoginProtocol.TryParseArguments(Args(url), out _), "reject callback: " + url);
        Check(!SteamLoginProtocol.TryParseArguments(Args(callback).Concat(new[] { "--extra" }).ToArray(), out _), "extra field");
        var badToken = Args(callback); badToken[4] = "short";
        Check(!SteamLoginProtocol.TryParseArguments(badToken, out _), "bad token");
        var duplicate = Args(callback); duplicate[5] = "--token";
        Check(!SteamLoginProtocol.TryParseArguments(duplicate, out _), "duplicate field");
        Check(SteamLoginProtocol.IsAllowedNavigation("https://login.steampowered.com/login"), "Steam auth");
        foreach (var url in new[] { "javascript:alert(1)", "http://steamcommunity.com/", "https://steamcommunity.com.evil.test/", "https://evil@steamcommunity.com/", "https://steamcommunity.com:444/" })
            Check(!SteamLoginProtocol.IsAllowedNavigation(url), "blocked navigation");
        var key = new string('A', 32);
        string Payload(string origin, string path, string id, string apiKey) => JsonSerializer.Serialize(new { origin, path, steamId = id, apiKey });
        var valid = Payload("https://steamcommunity.com", "/dev/apikey", "76561198000000000", key);
        Check(SteamLoginProtocol.TryParseCapture(valid, out var capture) && capture!.ApiKey == key, "valid capture");
        foreach (var payload in new[] { "null", "{}", "not json", Payload("https://evil.test", "/dev/apikey", "76561198000000000", key), Payload("https://steamcommunity.com", "/other", "76561198000000000", key), Payload("https://steamcommunity.com", "/dev/apikey", "123", key), Payload("https://steamcommunity.com", "/dev/apikey", "76561198000000000", new string('Z', 32)) })
            Check(!SteamLoginProtocol.TryParseCapture(payload, out _), "reject capture");
        Check(SteamLoginProtocol.IsApiKeyPage("https://steamcommunity.com/dev/apikey?l=english"), "API query allowed");
        Check(!SteamLoginProtocol.IsApiKeyPage("https://store.steampowered.com/dev/apikey"), "API origin exact");
        Console.WriteLine("SteamLoginProtocol tests passed");
    }

    private static void Check(bool condition, string description)
    {
        if (!condition) throw new Exception("Steam login assertion failed: " + description);
    }
}
