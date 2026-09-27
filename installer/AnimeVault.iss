#ifndef PackageDir
  #error PackageDir is required
#endif
#ifndef PrereqDir
  #error PrereqDir is required
#endif
#ifndef InstallerOutputDir
  #error InstallerOutputDir is required
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{2B8F1FD4-642C-4D7D-833A-37F0E3FB48DB}
AppName=Anime Vault
AppVersion={#AppVersion}
AppPublisher=suoyi127
DefaultDirName={autopf}\Anime Vault
DefaultGroupName=Anime Vault
OutputDir={#InstallerOutputDir}
OutputBaseFilename=Setup
Compression=lzma2
SolidCompression=yes
SetupArchitecture=x64
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes
CloseApplications=yes
UninstallDisplayIcon={app}\AnimeVault.exe

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PrereqDir}\vc_redist.x64.exe"; Flags: dontcopy
Source: "{#PrereqDir}\MicrosoftEdgeWebview2Setup.exe"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\Anime Vault"; Filename: "{app}\AnimeVault.exe"
Name: "{autodesktop}\Anime Vault"; Filename: "{app}\AnimeVault.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\AnimeVault.exe"; Description: "启动 Anime Vault"; Flags: nowait postinstall skipifsilent

[Code]
function HasVisualCppRuntime: Boolean;
var
  Installed: Cardinal;
  Version: String;
  Minor: Integer;
begin
  Result := False;
  if not RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then Exit;
  if Installed <> 1 then Exit;
  if not RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version) then Exit;
  { The backend is built with MSVC 14.50; older v14 runtimes are insufficient. }
  if Copy(Version, 1, 4) <> 'v14.' then Exit;
  Minor := StrToIntDef(Copy(Version, 5, 2), 0);
  Result := Minor >= 50;
end;

function HasWebView2Runtime: Boolean;
var
  Version: String;
  Key: String;
begin
  Key := 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  Result := False;
  if RegQueryStringValue(HKLM32, Key, 'pv', Version) then
    Result := (Version <> '') and (Version <> '0.0.0.0');
  if not Result and RegQueryStringValue(HKCU, Key, 'pv', Version) then
    Result := (Version <> '') and (Version <> '0.0.0.0');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if not HasVisualCppRuntime then begin
    ExtractTemporaryFile('vc_redist.x64.exe');
    if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then begin
      Result := '无法启动 Microsoft Visual C++ 运行库安装程序。';
      Exit;
    end;
    if ExitCode = 3010 then begin
      NeedsRestart := True;
      Result := 'Visual C++ 运行库安装后需要重启。请重启 Windows，再运行安装程序。';
      Exit;
    end;
    if (ExitCode <> 0) or (not HasVisualCppRuntime) then begin
      Result := 'Visual C++ 运行库安装失败（代码 ' + IntToStr(ExitCode) + '）。';
      Exit;
    end;
  end;
  if not HasWebView2Runtime then begin
    ExtractTemporaryFile('MicrosoftEdgeWebview2Setup.exe');
    if not Exec(ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe'), '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then begin
      Result := '无法启动 Microsoft Edge WebView2 Runtime 安装程序。';
      Exit;
    end;
    if (ExitCode <> 0) or (not HasWebView2Runtime) then
      Result := 'WebView2 Runtime 安装失败（代码 ' + IntToStr(ExitCode) + '）；请检查网络连接。';
  end;
end;
