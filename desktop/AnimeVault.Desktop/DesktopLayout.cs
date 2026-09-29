namespace AnimeVault.Desktop;

internal sealed record DesktopLayout(string ApplicationRoot, string BackendExecutable, string WebDirectory, string ProfileDirectory)
{
    internal static DesktopLayout Resolve(string baseDirectory)
    {
        var root = Path.GetFullPath(baseDirectory);
        var backend = Path.Combine(root, "backend", "anime_vault_server.exe");
        var web = Path.Combine(root, "web");
        var index = Path.Combine(web, "index.html");
        if (!File.Exists(backend)) throw new FileNotFoundException("未找到后端程序；请重新安装 Anime Vault。", backend);
        if (!File.Exists(index)) throw new FileNotFoundException("未找到前端页面；请重新安装 Anime Vault。", index);
        var localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(localAppData)) throw new InvalidOperationException("无法定位用户数据目录。");
        return new DesktopLayout(root, backend, web, Path.Combine(localAppData, "AnimeVault", "WebView2"));
    }
}
