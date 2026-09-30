using System.Diagnostics;
using System.Text.Json;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace AnimeVault.Desktop;

internal sealed class MainForm : Form
{
    private readonly WebView2 browser = new() { Dock = DockStyle.Fill };
    private BackendSession? backend;
    private bool restarting;
    private readonly CancellationTokenSource closing = new();

    internal MainForm()
    {
        Text = "Anime Vault";
        Width = 1280;
        Height = 800;
        MinimumSize = new Size(800, 500);
        Controls.Add(browser);
        Shown += async (_, _) => await InitializeAsync();
        FormClosed += (_, _) =>
        {
            closing.Cancel();
            backend?.Dispose();
            closing.Dispose();
        };
    }

    private async Task InitializeAsync()
    {
        try
        {
            var layout = DesktopLayout.Resolve(AppContext.BaseDirectory);
            backend = await BackendSession.StartAsync(layout, closing.Token);
            Directory.CreateDirectory(layout.ProfileDirectory);
            var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: layout.ProfileDirectory);
            await browser.EnsureCoreWebView2Async(environment);
            browser.CoreWebView2.NavigationStarting += (_, args) =>
            {
                if (backend is not null && IsSameOrigin(args.Uri, backend.Origin)) return;
                args.Cancel = true;
                OpenExternal(args.Uri);
            };
            browser.CoreWebView2.NewWindowRequested += (_, args) =>
            {
                args.Handled = true;
                OpenExternal(args.Uri);
            };
            browser.CoreWebView2.WebMessageReceived += async (_, args) =>
            {
                // 仅接受当前本机页面的固定指令，忽略外部页面及任意命令参数。
                if (backend is null || restarting || !IsSameOrigin(args.Source, backend.Origin)) return;
                try
                {
                    using var message = JsonDocument.Parse(args.WebMessageAsJson);
                    if (message.RootElement.ValueKind != JsonValueKind.Object ||
                        !message.RootElement.TryGetProperty("command", out var command) ||
                        command.ValueKind != JsonValueKind.String || command.GetString() != "restart-backend") return;
                }
                catch (JsonException) { return; }
                await RestartBackendAsync(layout);
            };
            browser.Source = backend.Origin;
        }
        catch (OperationCanceledException) when (closing.IsCancellationRequested) { }
        catch (Exception error)
        {
            backend?.Dispose();
            backend = null;
            MessageBox.Show(this,
                $"Anime Vault 无法启动：{error.Message}\n\n若提示 WebView2 缺失，请安装 Microsoft Edge WebView2 Runtime；否则请重新安装应用。",
                "启动失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            Close();
        }
    }

    private async Task RestartBackendAsync(DesktopLayout layout)
    {
        restarting = true;
        try
        {
            backend?.Dispose();
            backend = null;
            backend = await BackendSession.StartAsync(layout, closing.Token);
            browser.Source = backend.Origin;
        }
        catch (OperationCanceledException) when (closing.IsCancellationRequested) { }
        catch (Exception error)
        {
            MessageBox.Show(this, $"目录已保存，但后端重启失败：{error.Message}\n请关闭并重新打开应用。",
                "重启失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally { restarting = false; }
    }

    private static bool IsSameOrigin(string raw, Uri origin) =>
        Uri.TryCreate(raw, UriKind.Absolute, out var uri) &&
        uri.Scheme == origin.Scheme && uri.Host == origin.Host && uri.Port == origin.Port;

    private static void OpenExternal(string raw)
    {
        if (!Uri.TryCreate(raw, UriKind.Absolute, out var uri) ||
            (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps)) return;
        Process.Start(new ProcessStartInfo(uri.AbsoluteUri) { UseShellExecute = true });
    }
}
