using AnimeVault.Desktop;

var temporary = Path.Combine(Path.GetTempPath(), "anime-vault-layout-" + Guid.NewGuid().ToString("N"));
try
{
    Directory.CreateDirectory(Path.Combine(temporary, "backend"));
    Directory.CreateDirectory(Path.Combine(temporary, "web"));
    File.WriteAllText(Path.Combine(temporary, "backend", "anime_vault_server.exe"), "test");
    File.WriteAllText(Path.Combine(temporary, "web", "index.html"), "test");

    var layout = DesktopLayout.Resolve(temporary);
    Require(layout.ApplicationRoot == temporary);
    Require(layout.BackendExecutable == Path.Combine(temporary, "backend", "anime_vault_server.exe"));
    Require(layout.WebDirectory == Path.Combine(temporary, "web"));
    Require(layout.ProfileDirectory == Path.Combine(temporary, "Data", "WebView2"));

    File.Delete(Path.Combine(temporary, "web", "index.html"));
    RequireThrows(() => DesktopLayout.Resolve(temporary));
    File.WriteAllText(Path.Combine(temporary, "web", "index.html"), "test");
    File.Delete(Path.Combine(temporary, "backend", "anime_vault_server.exe"));
    RequireThrows(() => DesktopLayout.Resolve(temporary));
    Console.WriteLine("DesktopLayout tests passed");
}
finally
{
    Directory.Delete(temporary, recursive: true);
}

static void Require(bool condition)
{
    if (!condition) throw new Exception("Desktop layout assertion failed");
}

static void RequireThrows(Action action)
{
    try { action(); }
    catch (FileNotFoundException) { return; }
    throw new Exception("Expected missing layout file to be rejected");
}
