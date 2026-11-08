namespace AnimeVault.Desktop;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (args.Length == 0)
        {
            Application.Run(new MainForm());
            return;
        }
        if (!SteamLoginProtocol.TryParseArguments(args, out var options))
        {
            MessageBox.Show("Steam 登录参数无效，请从应用设置重新发起登录。", "Steam 登录", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        Application.Run(new SteamLoginForm(options!));
    }
}
