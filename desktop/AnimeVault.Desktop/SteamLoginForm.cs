using System.Net;
using System.Net.Http.Json;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace AnimeVault.Desktop;

internal sealed class SteamLoginForm : Form
{
    private readonly SteamLoginOptions options;
    private readonly WebView2 browser = new() { Dock = DockStyle.Fill };
    private readonly Label address = new() { Dock = DockStyle.Top, Height = 32, AutoEllipsis = true, Padding = new Padding(8) };
    private readonly Label hint = new() { Dock = DockStyle.Top, Height = 64, Padding = new Padding(8), Text = "请在 Steam 官方页面登录并完成 Steam Guard。首次使用请手动注册 API Key；完成后将自动保存并关闭。" };
    private readonly Button retry = new() { Dock = DockStyle.Bottom, Height = 34, Text = "重试保存", Visible = false };
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 1000 };
    private readonly CancellationTokenSource closing = new();
    private readonly HttpClient client = new(new HttpClientHandler { AllowAutoRedirect = false, UseProxy = false }) { Timeout = TimeSpan.FromSeconds(8) };
    private readonly DateTime deadline = DateTime.UtcNow.AddMinutes(15);
    private bool busy;
    private bool completed;
    private bool closingStarted;
    private bool allowClose;
    private long navigationVersion;

    internal SteamLoginForm(SteamLoginOptions options)
    {
        this.options = options;
        Text = "Steam 官方登录 — Anime Vault";
        Width = 1050;
        Height = 820;
        MinimumSize = new Size(800, 600);
        Controls.Add(browser);
        Controls.Add(address);
        Controls.Add(hint);
        Controls.Add(retry);
        Shown += async (_, _) => await InitializeAsync();
        timer.Tick += async (_, _) => await CaptureAsync();
        retry.Click += async (_, _) => { retry.Visible = false; timer.Start(); await CaptureAsync(); };
        FormClosing += OnClosing;
        FormClosed += (_, _) => { timer.Dispose(); browser.Dispose(); client.Dispose(); closing.Dispose(); };
    }

    private async Task InitializeAsync()
    {
        try
        {
            var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: options.Profile);
            if (closingStarted) return;
            await browser.EnsureCoreWebView2Async(environment);
            if (closingStarted) return;
            var core = browser.CoreWebView2;
            // Steam 页面不具备宿主消息桥，回调只由原生 HTTP 客户端提交。
            core.Settings.IsWebMessageEnabled = false;
            core.Settings.AreHostObjectsAllowed = false;
            core.Settings.AreDevToolsEnabled = false;
            core.Settings.AreDefaultContextMenusEnabled = false;
            core.NavigationStarting += (_, args) =>
            {
                navigationVersion++;
                if (SteamLoginProtocol.IsAllowedNavigation(args.Uri)) return;
                args.Cancel = true;
                hint.Text = "已阻止非 Steam 页面跳转。请在此窗口内完成 Steam 登录。";
            };
            // 官方登录可能嵌入验证码或空白框架；仅限制顶层导航，密钥也仅从已校验的顶层页面读取。
            core.NewWindowRequested += (_, args) =>
            {
                args.Handled = true;
                if (!closingStarted && SteamLoginProtocol.IsAllowedNavigation(args.Uri)) core.Navigate(args.Uri);
            };
            core.DownloadStarting += (_, args) => args.Cancel = true;
            core.PermissionRequested += (_, args) => args.State = CoreWebView2PermissionState.Deny;
            core.SourceChanged += (_, _) => address.Text = core.Source;
            core.DocumentTitleChanged += (_, _) => Text = "Steam 官方登录 — " + core.DocumentTitle;
            core.Navigate("https://steamcommunity.com/dev/apikey?l=english");
            timer.Start();
        }
        catch (Exception) when (!closingStarted)
        {
            hint.Text = "Steam 登录窗口启动失败，请检查 WebView2 Runtime 后关闭窗口重试。";
        }
        catch (Exception) when (closingStarted) { }
    }

    private async Task CaptureAsync()
    {
        if (closingStarted || completed || busy) return;
        if (DateTime.UtcNow >= deadline)
        {
            timer.Stop();
            retry.Visible = false;
            hint.Text = "登录已超过 15 分钟，请关闭窗口并从设置重新发起登录。";
            return;
        }
        var core = browser.CoreWebView2;
        if (core is null || !SteamLoginProtocol.IsApiKeyPage(core.Source)) return;
        busy = true;
        SteamLoginCapture? capture = null;
        string? json = null;
        try
        {
            var version = navigationVersion;
            json = await core.ExecuteScriptAsync(SteamLoginProtocol.CaptureScript);
            // 执行前后同时校验页面，避免导航竞态将其他页面的结果用于保存。
            if (closingStarted || version != navigationVersion || !SteamLoginProtocol.IsApiKeyPage(core.Source) ||
                !SteamLoginProtocol.TryParseCapture(json, out capture)) return;
            using var response = await client.PostAsJsonAsync(options.Callback,
                new { token = options.Token, steamId = capture!.SteamId, apiKey = capture.ApiKey }, closing.Token);
            if (closingStarted) return;
            if (response.StatusCode == HttpStatusCode.OK)
            {
                completed = true;
                timer.Stop();
                Close();
            }
            else ShowRetry();
        }
        catch (OperationCanceledException) when (closingStarted) { }
        catch (Exception) when (closingStarted) { }
        catch (Exception) when (!closingStarted) { ShowRetry(); }
        finally
        {
            // 不保留密钥缓存；重试时重新读取当前官方页面。
            capture = null;
            json = null;
            busy = false;
        }
    }

    private void ShowRetry()
    {
        timer.Stop();
        hint.Text = "暂时无法保存 Steam 账号。请确认主应用仍在运行，稍后点击重试保存；也可关闭后重新登录。";
        retry.Visible = true;
    }

    private async void OnClosing(object? sender, FormClosingEventArgs args)
    {
        if (allowClose) return;
        args.Cancel = true;
        if (closingStarted) return;
        closingStarted = true;
        timer.Stop();
        closing.Cancel();
        browser.Enabled = false;
        retry.Enabled = false;
        if (!completed)
        {
            // 关闭前仅通知本机取消；短超时确保网络异常不会阻塞退出。
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try
            {
                using var response = await client.PostAsJsonAsync(options.Callback,
                    new { token = options.Token, cancelled = true }, timeout.Token);
            }
            catch (Exception) { }
        }
        allowClose = true;
        BeginInvoke(Close);
    }
}
