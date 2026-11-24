<#
.SYNOPSIS
A Windows 11 Hyper-V machine for tests that need a screen, a mouse and a keyboard.

.DESCRIPTION
-Build  installs Windows unattended from -IsoPath, signs the local account in and takes the
        checkpoint "clean" of the signed-in desktop. Refuses when the machine exists.
-Reclean takes clean again from clean's own desktop, after this script's setup of it changed.
-Run    restores clean, copies -In to C:\job\in, runs -Command in the signed-in desktop,
        copies C:\job\out to -Out and turns the machine off. -Out\result.json says what
        happened: {"outcome":"ran","exit":N,...} or {"outcome":"not-now","reason":"..."}.
-Stop   turns the machine off and restores clean, unless a live run holds it.

Exit: the command's own code after -Run, 75 not now, 3 the machine was never reachable or the
command gave no exit code, 2 a bad argument, 4 another run holds this machine, 1 a failure.

The guest shuts itself down -HardSec after the session opens, so a run that was killed cannot
leave the machine up. -Avoid names machines this one must not run beside: -Run answers not now
while one of them is anything but off.

Windows PowerShell 5.1, Hyper-V Administrators membership, no elevation. The account's password
is random, made here and kept only as DPAPI Export-Clixml in -VmPath. It is on the answer media
while Windows installs; that media is deleted and the guest's cached copies are removed before
clean is taken. The machine has no network adapter.
#>
[CmdletBinding()]
param(
    [switch]$Build, [switch]$Reclean, [switch]$Run, [switch]$Stop,
    [Parameter(Mandatory)] [string]$Name,
    [Parameter(Mandatory)] [string]$VmPath,
    [string]$IsoPath, [int]$ImageIndex = 6,
    [string]$In, [string]$Out, [string]$Command,
    [int]$HardSec = 300, [string[]]$Avoid = @()
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$Account = 'tester'
$Ns = 'root\virtualization\v2'
$script:Touched = $false

function Say([string]$Text) { Write-Host ('winvm: {0} {1}' -f (Get-Date -Format 'HH:mm:ss'), $Text) }

function Wait-For([scriptblock]$Test, [int]$Seconds, [string]$What, [int]$Every = 10) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $r = & $Test
        if ($r) { return $r }
        Start-Sleep -Seconds $Every
    }
    throw "timed out after $Seconds s waiting for $What"
}

function Get-Guest([string]$Key) {
    $vm = Get-CimInstance -Namespace $Ns -ClassName Msvm_ComputerSystem -Filter "ElementName='$Name'"
    $kvp = Get-CimAssociatedInstance -InputObject $vm -ResultClassName Msvm_KvpExchangeComponent
    foreach ($item in @($kvp.GuestExchangeItems)) {
        if (-not $item) { continue }
        $doc = [xml]$item
        if (($doc.INSTANCE.PROPERTY | Where-Object { $_.NAME -eq 'Name' }).VALUE -eq $Key) {
            return ($doc.INSTANCE.PROPERTY | Where-Object { $_.NAME -eq 'Data' }).VALUE
        }
    }
}

function Send-Key([uint32]$Key) {
    $vm = Get-CimInstance -Namespace $Ns -ClassName Msvm_ComputerSystem -Filter "ElementName='$Name'"
    $kb = Get-CimAssociatedInstance -InputObject $vm -ResultClassName Msvm_Keyboard
    [void](Invoke-CimMethod -InputObject $kb -MethodName TypeKey -Arguments @{ keyCode = $Key })
}

# Only this user and SYSTEM; Hyper-V adds the machine's own identity to the files it attaches.
function Protect-Folder([string]$Path) {
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    $inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    foreach ($who in [Security.Principal.WindowsIdentity]::GetCurrent().User, (New-Object Security.Principal.SecurityIdentifier 'S-1-5-18')) {
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($who, 'FullControl', $inherit, 'None', 'Allow')))
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}

function New-Iso([string]$Dir, [string]$Path) {
    if (-not ('WinVm.Iso' -as [type])) {
        Add-Type -TypeDefinition @'
using System; using System.IO; using System.Runtime.InteropServices; using System.Runtime.InteropServices.ComTypes;
namespace WinVm { public static class Iso {
  public static void Save(object stream, string path) {
    IStream s = (IStream)stream; System.Runtime.InteropServices.ComTypes.STATSTG st; s.Stat(out st, 1);
    byte[] buf = new byte[1 << 20]; IntPtr got = Marshal.AllocHGlobal(4);
    try { using (FileStream f = File.Create(path)) { long left = st.cbSize;
      while (left > 0) { s.Read(buf, buf.Length, got); int n = Marshal.ReadInt32(got); if (n <= 0) break; f.Write(buf, 0, n); left -= n; }
      if (left != 0) throw new IOException("short ISO stream"); } }
    finally { Marshal.FreeHGlobal(got); } } } }
'@
    }
    $fsi = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
    $fsi.FileSystemsToCreate = 3
    $fsi.VolumeName = 'WINVM'
    $fsi.Root.AddTree($Dir, $false)
    [WinVm.Iso]::Save($fsi.CreateResultImage().ImageStream, $Path)
}

# VK7JG... is Microsoft's published generic key: it selects Pro and activates nothing.
function Get-Answer([string]$Password) {
    $pw = [Security.SecurityElement]::Escape($Password)
    $ready = [Security.SecurityElement]::Escape('cmd /c reg add "HKLM\SOFTWARE\Microsoft\Virtual Machine\Guest" /v winvm-desktop /d ready /f')
    $attrs = 'processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"'
    $locale = '<InputLocale>en-US</InputLocale><SystemLocale>en-US</SystemLocale><UILanguage>en-US</UILanguage><UserLocale>en-US</UserLocale>'
    return @"
<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
  <settings pass="windowsPE">
    <component name="Microsoft-Windows-International-Core-WinPE" $attrs>
      <SetupUILanguage><UILanguage>en-US</UILanguage></SetupUILanguage>$locale
    </component>
    <component name="Microsoft-Windows-Setup" $attrs>
      <DiskConfiguration><Disk wcm:action="add"><DiskID>0</DiskID><WillWipeDisk>true</WillWipeDisk>
        <CreatePartitions>
          <CreatePartition wcm:action="add"><Order>1</Order><Type>EFI</Type><Size>260</Size></CreatePartition>
          <CreatePartition wcm:action="add"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>
          <CreatePartition wcm:action="add"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
        </CreatePartitions>
        <ModifyPartitions>
          <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>
          <ModifyPartition wcm:action="add"><Order>2</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>
        </ModifyPartitions>
      </Disk></DiskConfiguration>
      <ImageInstall><OSImage>
        <InstallFrom><MetaData wcm:action="add"><Key>/IMAGE/INDEX</Key><Value>$ImageIndex</Value></MetaData></InstallFrom>
        <InstallTo><DiskID>0</DiskID><PartitionID>3</PartitionID></InstallTo>
      </OSImage></ImageInstall>
      <UserData><AcceptEula>true</AcceptEula><ProductKey><Key>VK7JG-NPHTM-C97JM-9MPGT-3V66T</Key><WillShowUI>OnError</WillShowUI></ProductKey></UserData>
    </component>
  </settings>
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" $attrs><ComputerName>UI-TEST</ComputerName></component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-International-Core" $attrs>$locale</component>
    <component name="Microsoft-Windows-Shell-Setup" $attrs>
      <OOBE>
        <HideEULAPage>true</HideEULAPage><HideOEMRegistrationScreen>true</HideOEMRegistrationScreen>
        <HideOnlineAccountScreens>true</HideOnlineAccountScreens><HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>
        <HideLocalAccountScreen>true</HideLocalAccountScreen><ProtectYourPC>3</ProtectYourPC>
      </OOBE>
      <UserAccounts><LocalAccounts><LocalAccount wcm:action="add">
        <Name>$Account</Name><DisplayName>$Account</DisplayName><Group>Administrators</Group>
        <Password><Value>$pw</Value><PlainText>true</PlainText></Password>
      </LocalAccount></LocalAccounts></UserAccounts>
      <AutoLogon><Enabled>true</Enabled><LogonCount>1</LogonCount><Username>$Account</Username>
        <Password><Value>$pw</Value><PlainText>true</PlainText></Password></AutoLogon>
      <FirstLogonCommands><SynchronousCommand wcm:action="add"><Order>1</Order><CommandLine>$ready</CommandLine></SynchronousCommand></FirstLogonCommands>
      <TimeZone>UTC</TimeZone>
    </component>
  </settings>
</unattend>
"@
}

function Open-Session([int]$Seconds) {
    $cred = Import-Clixml -LiteralPath (Join-Path $VmPath 'account.clixml')
    return Wait-For -Seconds $Seconds -Every 2 -What 'PowerShell Direct' -Test {
        try { New-PSSession -VMName $Name -Credential $cred -ErrorAction Stop } catch { $null }
    }
}

# PowerShell Direct has no desktop, so an interactive task runs the command, under a console host with no window.
function Start-OnDesktop($Session, [string]$Line, [string]$Output) {
    Invoke-Command -Session $Session -ArgumentList $Line, $Account, $Output -ScriptBlock {
        param($cmd, $who, $to)
        foreach ($d in 'C:\job\in', 'C:\job\out', 'C:\job\run') { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content -LiteralPath 'C:\job\run\run.cmd' -Encoding ASCII -Value @(
            '@echo off', 'cd /d C:\job\in', "$cmd > $to 2>&1",
            '> C:\job\run\rc.tmp echo %ERRORLEVEL%', 'move /y C:\job\run\rc.tmp C:\job\run\rc.txt > nul')
        $action = New-ScheduledTaskAction -Execute 'conhost.exe' -Argument '--headless cmd.exe /c C:\job\run\run.cmd'
        $principal = New-ScheduledTaskPrincipal -UserId $who -LogonType Interactive
        Register-ScheduledTask -TaskName 'winvm-job' -Action $action -Principal $principal -Force | Out-Null
        Start-ScheduledTask -TaskName 'winvm-job'
    }
}

# Takes "clean" from the running, signed-in desktop. Windows keeps a program the person did not start from coming to the
# front, so a test's window could never be the foreground one; here nobody is at the keyboard to protect. The setting is
# allowed only to the process that made the last input, so a key nothing listens to (F24) comes first.
function Save-Clean() {
    Send-Key 0x1B      # first sign-in opens Start
    Start-Sleep -Seconds 5
    $s = Open-Session 300
    try {
        $script = @'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class U {
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SystemParametersInfoW(uint a, uint b, IntPtr c, uint d);
  [DllImport("user32.dll")] public static extern bool SystemParametersInfoW(uint a, uint b, ref uint c, uint d);
}
"@
[U]::keybd_event(0x87, 0, 0, [UIntPtr]::Zero); [U]::keybd_event(0x87, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300
[void][U]::SystemParametersInfoW(0x2001, 0, [IntPtr]::Zero, 3)
$now = [uint32]1; [void][U]::SystemParametersInfoW(0x2000, 0, [ref]$now, 0)
exit [int]($now -ne 0)
'@
        Invoke-Command -Session $s -ArgumentList $script -ScriptBlock { param($text) New-Item -ItemType Directory -Path 'C:\job' -Force | Out-Null; Set-Content -LiteralPath 'C:\job\front.ps1' -Value $text -Encoding UTF8 }
        Start-OnDesktop $s 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\job\front.ps1' 'C:\job\run\front.txt'
        $rc = Wait-For -Seconds 120 -Every 2 -What 'the foreground setting' -Test {
            Invoke-Command -Session $s -ScriptBlock { if (Test-Path 'C:\job\run\rc.txt') { "$(Get-Content 'C:\job\run\rc.txt' -Raw)".Trim() } }
        }
        if ($rc -ne '0') { throw "the foreground setting did not hold (exit $rc)" }
        Invoke-Command -Session $s -ScriptBlock {
            Unregister-ScheduledTask -TaskName 'winvm-job' -Confirm:$false
            Remove-Item -LiteralPath 'C:\job' -Recurse -Force
        }
    } finally { Remove-PSSession $s }
    # The old clean goes only once its successor exists.
    $next = Checkpoint-VM -Name $Name -SnapshotName 'clean-next' -Passthru
    Get-VMSnapshot -VMName $Name | Where-Object { $_.Name -eq 'clean' } | Remove-VMSnapshot
    Rename-VMSnapshot -VMSnapshot $next -NewName 'clean'
    Say 'checkpoint clean taken'
}

function Invoke-Reclean() {
    $script:Touched = $true
    Set-Off
    Restore-VMSnapshot -VMName $Name -Name 'clean' -Confirm:$false
    if ((Get-VM -Name $Name).State -ne 'Running') { Start-VM -Name $Name }
    Save-Clean
    return 0
}

function Set-Off() {
    $vm = Get-VM | Where-Object { $_.Name -eq $Name }
    if ($vm -and $vm.State -ne 'Off') { Stop-VM -Name $Name -TurnOff -Force }
}

function Invoke-Build() {
    if (Get-VM | Where-Object { $_.Name -eq $Name }) { Say "$Name exists and is left as it was"; return 2 }
    if (-not $IsoPath -or -not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) { Say "no ISO at $IsoPath"; return 2 }
    if ((Test-Path -LiteralPath $VmPath) -and @(Get-ChildItem -LiteralPath $VmPath -Force).Count) { Say "$VmPath is not empty"; return 2 }
    New-Item -ItemType Directory -Path $VmPath -Force | Out-Null
    Protect-Folder $VmPath
    $alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'.ToCharArray()
    $bytes = New-Object byte[] 24
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $pw = -join ($bytes | ForEach-Object { $alphabet[$_ % $alphabet.Length] })
    New-Object Management.Automation.PSCredential ($Account, (ConvertTo-SecureString $pw -AsPlainText -Force)) |
        Export-Clixml -LiteralPath (Join-Path $VmPath 'account.clixml')
    $stage = Join-Path $VmPath 'stage'
    $media = Join-Path $VmPath 'answer.iso'
    try {
        New-Item -ItemType Directory -Path $stage | Out-Null
        Set-Content -LiteralPath (Join-Path $stage 'autounattend.xml') -Value (Get-Answer $pw) -Encoding UTF8
        New-Iso $stage $media
        Remove-Item -LiteralPath $stage -Recurse -Force

        $script:Touched = $true
        New-VM -Name $Name -Generation 2 -MemoryStartupBytes 4GB -NewVHDPath (Join-Path $VmPath 'disk.vhdx') -NewVHDSizeBytes 80GB -Path $VmPath | Out-Null
        Set-VM -Name $Name -ProcessorCount 4 -StaticMemory -CheckpointType Standard -AutomaticCheckpointsEnabled $false -SnapshotFileLocation $VmPath -AutomaticStartAction Nothing -AutomaticStopAction TurnOff
        Get-VMNetworkAdapter -VMName $Name | Remove-VMNetworkAdapter
        Set-VMFirmware -VMName $Name -EnableSecureBoot On -SecureBootTemplate MicrosoftWindows
        Set-VMKeyProtector -VMName $Name -NewLocalKeyProtector
        Enable-VMTPM -VMName $Name
        Set-VMVideo -VMName $Name -HorizontalResolution 1920 -VerticalResolution 1080 -ResolutionType Single
        $dvd = Add-VMDvdDrive -VMName $Name -Path $IsoPath -Passthru
        Add-VMDvdDrive -VMName $Name -Path $media
        Set-VMFirmware -VMName $Name -FirstBootDevice $dvd
        Start-VM -Name $Name
        # The firmware asks for a key to boot the DVD. x is no accelerator on a Setup page, where Enter would press Cancel.
        for ($i = 0; $i -lt 10; $i++) { Start-Sleep -Seconds 2; Send-Key 0x58 }
        Say 'installing Windows'
        Wait-For -Seconds 5400 -Every 20 -What 'the signed-in desktop' -Test { (Get-Guest 'winvm-desktop') -eq 'ready' } | Out-Null
        Say 'signed in'
        $s = Open-Session 300
        try {
            Invoke-Command -Session $s -ScriptBlock {
                Set-ItemProperty 'HKCU:\Control Panel\Desktop' -Name ScreenSaveActive -Value '0'
                foreach ($x in 'monitor-timeout-ac', 'standby-timeout-ac', 'hibernate-timeout-ac') { & powercfg.exe /change $x 0 }
                # The cached answer files hold the password.
                foreach ($f in 'C:\Windows\Panther\unattend.xml', 'C:\Windows\Panther\unattend-original.xml', 'C:\Windows\Panther\Unattend', 'C:\Windows\System32\Sysprep\unattend.xml') {
                    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Recurse -Force }
                }
            }
        } finally { Remove-PSSession $s }
        # The checkpoint keeps no media: the answer media is deleted below, and an ISO that moved would stop a restore.
        Get-VMDvdDrive -VMName $Name | Set-VMDvdDrive -Path $null
        Start-Sleep -Seconds 90      # first sign-in settles
        Save-Clean
    } finally {
        Set-Off
        if (Get-VM | Where-Object { $_.Name -eq $Name }) { Get-VMDvdDrive -VMName $Name | Where-Object { $_.Path } | Set-VMDvdDrive -Path $null }
        foreach ($p in $stage, $media) { if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force } }
    }
    return 0
}

function Write-Result($Value) {
    [IO.File]::WriteAllText((Join-Path $Out 'result.json'), ($Value | ConvertTo-Json -Compress) + "`n")
}

function Send-Not([string]$Reason) {
    Say "not now: $Reason"
    Write-Result @{ outcome = 'not-now'; reason = $Reason }
    return 75
}

function Invoke-Run() {
    if (-not $Command -or -not $Out) { Say 'give -Command and -Out'; return 2 }
    if ((Test-Path -LiteralPath $Out) -and @(Get-ChildItem -LiteralPath $Out -Force).Count) { Say "$Out is not empty"; return 2 }
    New-Item -ItemType Directory -Path $Out -Force | Out-Null
    $script:Out = (Resolve-Path -LiteralPath $Out).ProviderPath
    foreach ($other in @(Get-VM | Where-Object { $Avoid -contains $_.Name -and $_.State -ne 'Off' })) { return Send-Not "$($other.Name) is $($other.State)" }
    $began = Get-Date
    $script:Touched = $true
    Set-Off
    Restore-VMSnapshot -VMName $Name -Name 'clean' -Confirm:$false
    try { if ((Get-VM -Name $Name).State -ne 'Running') { Start-VM -Name $Name } }
    catch {
        # 0x8007000E: Hyper-V cannot back the machine's memory.
        if ("$($_.Exception.Message) $($_.Exception.HResult)" -match 'memory|8007000E') { return Send-Not 'Hyper-V could not get the memory' }
        throw
    }
    $s = $null
    try {
        try { $s = Open-Session 120 } catch { Say $_.Exception.Message; return 3 }
        Invoke-Command -Session $s -ArgumentList $HardSec -ScriptBlock {
            param($sec)
            # Not shutdown.exe /t: its sign-out notice takes the foreground from the window under test.
            $off = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -Command `"Start-Sleep -Seconds $sec; Stop-Computer -Force`""
            Register-ScheduledTask -TaskName 'winvm-limit' -Action $off -User 'SYSTEM' -RunLevel Highest -Force | Out-Null
            Start-ScheduledTask -TaskName 'winvm-limit'
            foreach ($d in 'C:\job\in', 'C:\job\out', 'C:\job\run') { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        }
        $up = ((Get-Date) - $began).TotalSeconds
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        if ($In) {
            $zip = Join-Path $env:TEMP "winvm-$PID.zip"
            try {
                [IO.Compression.ZipFile]::CreateFromDirectory((Resolve-Path -LiteralPath $In).ProviderPath, $zip)
                Copy-Item -LiteralPath $zip -Destination 'C:\job\in.zip' -ToSession $s
            } finally { if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force } }
            Invoke-Command -Session $s -ScriptBlock {
                Add-Type -AssemblyName System.IO.Compression.FileSystem
                [IO.Compression.ZipFile]::ExtractToDirectory('C:\job\in.zip', 'C:\job\in')
            }
        }
        $sent = ((Get-Date) - $began).TotalSeconds
        Start-OnDesktop $s $Command 'C:\job\out\stdout.txt'
        $rc = $null
        try {
            $rc = Wait-For -Seconds $HardSec -Every 1 -What 'the command' -Test {
                Invoke-Command -Session $s -ErrorAction Stop -ScriptBlock { if (Test-Path 'C:\job\run\rc.txt') { "$(Get-Content 'C:\job\run\rc.txt' -Raw)".Trim() } }
            }
        } catch { Say "no exit code: $($_.Exception.Message)"; return 3 }
        if ($rc -notmatch '^-?[0-9]+$') { Say "no exit code: '$rc'"; return 3 }
        Invoke-Command -Session $s -ScriptBlock {
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [IO.Compression.ZipFile]::CreateFromDirectory('C:\job\out', 'C:\job\out.zip')
        }
        $zip = Join-Path $env:TEMP "winvm-out-$PID.zip"
        try {
            Copy-Item -LiteralPath 'C:\job\out.zip' -Destination $zip -FromSession $s
            [IO.Compression.ZipFile]::ExtractToDirectory($zip, $Out)
        } finally { if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force } }
        $all = ((Get-Date) - $began).TotalSeconds
        Write-Result @{ outcome = 'ran'; exit = [int]$rc; seconds = [int]$all; startSeconds = [int]$up; copySeconds = [int]($sent - $up) }
        Say "exit $rc in $([int]$all) s (start $([int]$up) s)"
        return [int]$rc
    } finally { if ($s) { Remove-PSSession $s } }
}

if (@($Build, $Reclean, $Run, $Stop | Where-Object { $_ }).Count -ne 1) { Say 'give one of -Build, -Reclean, -Run and -Stop'; exit 2 }
$lock = New-Object Threading.Mutex($false, "Global\winvm-$($Name.ToLowerInvariant())")
try { $held = $lock.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
if (-not $held) { Say "another run holds $Name"; exit 4 }
# Cmdlets in a finally entered by exit are skipped, so every path returns its code and the exit comes last.
$code = 1
try {
    if ($Build) { $code = Invoke-Build | Select-Object -Last 1 }
    elseif ($Reclean) { $code = Invoke-Reclean | Select-Object -Last 1 }
    elseif ($Run) { $code = Invoke-Run | Select-Object -Last 1 }
    else { $script:Touched = $true; Set-Off; Restore-VMSnapshot -VMName $Name -Name 'clean' -Confirm:$false; $code = 0 }
} catch { Say "failed: $($_.Exception.Message)" }
finally { if ($script:Touched) { Set-Off } }
exit $code
