using System.Text.Json;
using System.Text.RegularExpressions;

namespace AnimeVault.Desktop;

internal sealed record SteamLoginOptions(Uri Callback, string Token, string Profile);
internal sealed record SteamLoginCapture(string SteamId, string ApiKey);

internal static class SteamLoginProtocol
{
    internal static bool TryParseArguments(string[] args, out SteamLoginOptions? options)
    {
        options = null;
        if (args.Length != 7 || args[0] != "--steam-login") return false;
        var fields = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 1; i < args.Length; i += 2)
            if (!fields.TryAdd(args[i], args[i + 1])) return false;
        if (!fields.TryGetValue("--callback", out var callback) ||
            !fields.TryGetValue("--token", out var token) ||
            !fields.TryGetValue("--profile", out var profile) ||
            !Regex.IsMatch(token, "\\A[0-9a-fA-F]{64}\\z") ||
            !Path.IsPathFullyQualified(profile)) return false;
        // 在 URI 规范化前检查原文，拒绝回环缩写、路径折叠和额外参数。
        if (!Regex.IsMatch(callback, "\\Ahttp://127\\.0\\.0\\.1:[0-9]{1,5}/api/steam/login/complete\\z") ||
            !Uri.TryCreate(callback, UriKind.Absolute, out var uri) || uri.Port < 1 || uri.Port > 65535) return false;
        options = new(uri, token, profile);
        return true;
    }

    internal static bool IsAllowedNavigation(string raw) =>
        Uri.TryCreate(raw, UriKind.Absolute, out var uri) && uri.Scheme == "https" &&
        uri.Port == 443 && uri.UserInfo.Length == 0 &&
        uri.Host is "steamcommunity.com" or "store.steampowered.com" or "login.steampowered.com" or "help.steampowered.com";

    internal static bool IsApiKeyPage(string raw) => IsAllowedNavigation(raw) &&
        new Uri(raw).Host == "steamcommunity.com" && new Uri(raw).AbsolutePath == "/dev/apikey";

    internal static bool TryParseCapture(string json, out SteamLoginCapture? capture)
    {
        capture = null;
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            string? Read(string name) => root.ValueKind == JsonValueKind.Object &&
                root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;
            var id = Read("steamId");
            var key = Read("apiKey");
            if (Read("origin") != "https://steamcommunity.com" || Read("path") != "/dev/apikey" ||
                id is null || key is null || !Regex.IsMatch(id, "\\A[0-9]{17}\\z") ||
                !Regex.IsMatch(key, "\\A[0-9a-fA-F]{32}\\z")) return false;
            capture = new(id, key);
            return true;
        }
        catch (JsonException) { return false; }
    }

    internal const string CaptureScript = """
        (() => {
          if (location.origin !== 'https://steamcommunity.com' || location.pathname !== '/dev/apikey') return null;
          const visible = el => el && el.getClientRects().length > 0 && getComputedStyle(el).visibility !== 'hidden';
          let apiKey = '';
          for (const p of document.querySelectorAll('#bodyContents_ex p')) {
            if (!visible(p)) continue;
            const match = p.innerText.trim().match(/^Key:\s*([0-9a-fA-F]{32})\s*$/);
            if (match) { apiKey = match[1]; break; }
          }
          let steamId = typeof window.g_steamID === 'string' ? window.g_steamID : '';
          if (!/^[0-9]{17}$/.test(steamId)) {
            steamId = '';
            for (const a of document.querySelectorAll('#global_header a.user_avatar, #global_header a.global_action_link[href*="/profiles/"]')) {
              if (!visible(a)) continue;
              const u = new URL(a.href);
              const match = u.pathname.match(/^\/profiles\/([0-9]{17})\/?$/);
              if (u.origin === 'https://steamcommunity.com' && match) { steamId = match[1]; break; }
            }
          }
          if (!apiKey || !/^[0-9]{17}$/.test(steamId)) return null;
          return { origin: location.origin, path: location.pathname, steamId, apiKey };
        })()
        """;
}
