using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text.Json;

namespace AnimeVault.Desktop;

internal sealed class BackendSession : IDisposable
{
    private static readonly HttpClient HealthClient = new() { Timeout = TimeSpan.FromSeconds(2) };
    private readonly Process process;
    internal Uri Origin { get; }

    private BackendSession(Process process, Uri origin)
    {
        this.process = process;
        Origin = origin;
    }

    internal static async Task<BackendSession> StartAsync(DesktopLayout layout, CancellationToken cancellationToken)
    {
        for (var attempt = 0; attempt < 5; attempt++)
        {
            var port = ReserveLoopbackPort();
            var token = Convert.ToHexString(RandomNumberGenerator.GetBytes(16)).ToLowerInvariant();
            var startInfo = new ProcessStartInfo(layout.BackendExecutable)
            {
                WorkingDirectory = Path.GetDirectoryName(layout.BackendExecutable)!,
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            };
            startInfo.Environment["ANIME_VAULT_PORT"] = port.ToString();
            startInfo.Environment["ANIME_VAULT_WEB_DIR"] = layout.WebDirectory;
            startInfo.Environment["ANIME_VAULT_INSTANCE_TOKEN"] = token;
            startInfo.Environment.TryGetValue("ANIME_VAULT_BANGUMI_USER_AGENT", out var userAgent);
            if (string.IsNullOrWhiteSpace(userAgent))
                startInfo.Environment["ANIME_VAULT_BANGUMI_USER_AGENT"] = "suoyi127/Link-to-Bangumi/0.1 (Windows)";

            var process = Process.Start(startInfo) ?? throw new InvalidOperationException("无法启动后端程序。");
            var origin = new Uri($"http://127.0.0.1:{port}/");
            var session = new BackendSession(process, origin);
            try
            {
                await session.WaitUntilReadyAsync(token, cancellationToken);
                return session;
            }
            catch (PortOccupiedException) when (attempt < 4)
            {
                session.Dispose();
            }
            catch
            {
                session.Dispose();
                throw;
            }
        }
        throw new InvalidOperationException("无法分配本地服务端口。");
    }

    private async Task WaitUntilReadyAsync(string token, CancellationToken cancellationToken)
    {
        var deadline = DateTime.UtcNow.AddSeconds(20);
        while (DateTime.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (process.HasExited)
            {
                // A listener that appeared after reservation won the port race, regardless of our exit code.
                if (PortIsOccupied(Origin.Port)) throw new PortOccupiedException();
                throw new InvalidOperationException($"后端提前退出（代码 {process.ExitCode}）。");
            }
            try
            {
                using var response = await HealthClient.GetAsync(new Uri(Origin, "health"), cancellationToken);
                if (response.IsSuccessStatusCode)
                {
                    try
                    {
                        using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync(cancellationToken));
                        if (json.RootElement.TryGetProperty("instanceToken", out var received) &&
                            received.GetString() == token) return;
                    }
                    catch (JsonException) { }
                    throw new PortOccupiedException();
                }
            }
            catch (HttpRequestException) { }
            catch (TaskCanceledException) when (!cancellationToken.IsCancellationRequested) { }
            await Task.Delay(150, cancellationToken);
        }
        throw new TimeoutException("后端启动超时。请检查安装文件或安全软件设置。");
    }

    private static int ReserveLoopbackPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private static bool PortIsOccupied(int port)
    {
        using var listener = new TcpListener(IPAddress.Loopback, port);
        try { listener.Start(); return false; }
        catch (SocketException) { return true; }
    }

    public void Dispose()
    {
        if (!process.HasExited) process.Kill(entireProcessTree: true);
        process.Dispose();
    }

    private sealed class PortOccupiedException : Exception;
}
