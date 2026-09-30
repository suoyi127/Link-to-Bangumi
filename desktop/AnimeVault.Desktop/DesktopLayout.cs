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
        // 数据与浏览器配置统一跟随安装目录；安装器仅为 Data 授予写权限。
        return new DesktopLayout(root, backend, web, Path.Combine(root, "Data", "WebView2"));
    }
}
