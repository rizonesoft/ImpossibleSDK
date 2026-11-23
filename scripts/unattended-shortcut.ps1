# Puts "<Title> <Runner> campaign" shortcuts on the Windows desktop, one per runner the run profile names (its own
# agent and its `runners`), each starting the unattended run in WSL on that runner and then attaching to it with
# keyboard and mouse (a run already going is just attached to), with the project's icon and the runner's badge in its
# corner; and "<Title> - Watch run" (attach) and "<Title> - Stop run" (asks first). The run lives in tmux, in WSL;
# Windows only starts, watches and stops it.
#
#   pwsh scripts/unattended-shortcut.ps1 -Distro Ubuntu -Icon assets/brand/project.ico
#   pwsh scripts/unattended-shortcut.ps1 -Remove       remove every shortcut and icon this project made
#
# Each link runs PowerShell, which runs `wsl.exe -d <distro> --cd <project in WSL> --exec bash -lc <command>`: the
# command `unattended-run.py`'s start_command, watch_command or stop_command gives (a test holds them equal), which on
# a failure says so and waits for Enter; a failure of WSL itself is said and waits for Enter too.
# Links and icons are written under a temporary name and moved over the old, so a failed save or icon keeps a
# working shortcut and leaves no broken one.
# The title is the launcher profile's (`.conclave/launchers.json`, read by `scripts/lib/launchers.py title`), the name
# the project's other links carry; with no profile, or its title not set yet, the links carry the folder's name.
# A link of this project's that a run does not write (an earlier title, a runner dropped) goes, with its icon.
# Removal finds this project's links by the mark in their description, so it works whatever the profile, the icon
# or the distro now are. Nothing here starts a run: only the operator's double-click does.
param(
  [string]$Distro = '',
  [string]$Icon = '',
  [string]$Project = '',
  [string]$ShortcutDir = [Environment]::GetFolderPath('Desktop'),
  [string]$IconDir = '',
  [switch]$Remove,
  # Write the links again with the arguments their last run recorded (after a kit update changed their commands).
  [switch]$Refresh,
  # Read the links themselves and say which are not the ones this script would write now; nothing is written.
  [switch]$Check,
  # The desktops searched for another link that shows an old-key icon: the user's and the public desktop.
  [string[]]$Desktops = @(
    [Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('CommonDesktopDirectory')),
  # For the tests: a stand-in for wsl.exe, and the Python that reads the profile and renders the icons.
  [string]$Wsl = '',
  [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
# The project this script is installed in holds the profile; -Project says where the run starts when that differs
# (a project in WSL given by its \\wsl.localhost\<distro>\ path, or its Linux path).
$root = Split-Path $here

function Refuse([string]$why) { "unattended-shortcut: $why; nothing was written"; exit 1 }

# What each run wrote (D02 T02 §12), one set per desktop folder: its arguments, which -Refresh and -Check run with
# again, and each link's kind and command, which `unattended-run.py status` compares with the current ones, so a link a
# kit update left behind is named. The links themselves are the truth: -Check reads them.
$recordPath = Join-Path $root 'build\unattended-shortcuts.json'
function Get-DeskKey([string]$dir) { [IO.Path]::GetFullPath($dir).TrimEnd('\').ToLowerInvariant() }
# The record is read by the runner's own reader (`unattended-run.py shortcut-record`), the one status uses: strict
# UTF-8, strict JSON and exactly the shape Save-Sets writes. One reader, so the script and status never disagree on
# a file (D02 T02 §12: reconciliations 1, 7, 9, 10, 11 and 12 each found a file this script's own reader took and
# Python's refused). Its result says what happened, never left to be read from its text (reconciliation 13): Ok with
# the sets (none without a record); a damaged record (the reader's exit 3), to be moved aside; or a reader that did
# not answer (missing, or ended without its answer), when the record, which may be sound, is kept for the reader.
$damagedAdvice = "move it aside (keep it to read the other folders back), then run the script with -Distro and " +
  '-Icon (and -ShortcutDir) for each folder, which writes a new one'
$readerAdvice = 'restore what reads it (python on PATH, and scripts/unattended-run.py, which ' +
  "python forge.py update <project> --only scripts/unattended-run.py puts back) and run the script again"
# A runner from an older kit has no `shortcut-record` (argparse refuses it with exit 2). When the project changed it,
# update keeps it, so --only cannot bring the reader: --kit-new writes the kit's beside it to merge (reconciliation 17).
$olderAdvice = "bring scripts/unattended-run.py up to this kit's: python forge.py update <project> --kit-new " +
  'writes the new one beside it as scripts/unattended-run.py.kit-new to merge (or --only, if the project never ' +
  'changed it), then run the script again'

$busyAdvice = 'run the script again once nothing holds the file (it may be sound)'
function Read-Record {
  $result = [pscustomobject]@{ Ok = $true; Sets = @(); Why = ''; Advice = '' }
  if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) { return $result }
  $result.Ok = $false
  $result.Advice = $readerAdvice
  $runner = Join-Path $here 'unattended-run.py'
  $py = if ($Python) { $Python } else {
    (Get-Command python -CommandType Application -ErrorAction Ignore | Select-Object -First 1).Source
  }
  if (-not $py -or -not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    $result.Why = 'python and scripts/unattended-run.py read it, and one of them is missing'
    return $result
  }
  $said = @(& $py -B $runner shortcut-record $recordPath 2>&1)
  $code = $LASTEXITCODE
  $text = ($said -join "`n").Trim()
  if ($code -eq 3 -and $text) {
    $result.Why = $text
    $result.Advice = $damagedAdvice
    return $result
  }
  if ($code -eq 4 -and $text) {  # held by another program: neither damaged nor read
    $result.Why = $text
    $result.Advice = $busyAdvice
    return $result
  }
  if ($code -eq 2 -and $text -match 'invalid choice') {
    $result.Why = 'scripts/unattended-run.py is from an older kit: it has no shortcut-record'
    $result.Advice = $olderAdvice
    return $result
  }
  if ($code -ne 0) {
    $result.Why = "its reader ended with exit $code" + $(if ($text) { ": $text" } else { ' and said nothing' })
    return $result
  }
  try { $data = ConvertFrom-Json -InputObject $text -NoEnumerate -ErrorAction Stop } catch { $data = $null }
  if ($data -isnot [Management.Automation.PSCustomObject] -or $data.sets -isnot [array]) {
    $result.Why = 'its reader gave no answer this script can read'
    return $result
  }
  $result.Ok = $true
  $result.Sets = @($data.sets)
  $result
}
$set = $null
if ($Refresh -or $Check) {
  if ($Remove) { Refuse '-Refresh and -Check work on the links -Remove removes: give one' }
  if ($Refresh -and $Check) { Refuse '-Refresh writes the links and -Check only reads them: give one' }
  $read = Read-Record
  $sets = @($read.Sets)
  if (-not $read.Ok) {
    Refuse ("the record of this project's shortcuts ($recordPath) cannot be read ($($read.Why)); it is kept, since " +
      "it may hold other folders' sets: $($read.Advice)")
  }
  $verb = if ($Refresh) { '-Refresh' } else { '-Check' }
  if ($PSBoundParameters.ContainsKey('ShortcutDir')) {
    $set = $sets | Where-Object { (Get-DeskKey $_.args.ShortcutDir) -eq (Get-DeskKey $ShortcutDir) } |
      Select-Object -First 1
  } elseif ($sets.Count -gt 1) {
    $each = $sets | ForEach-Object {
      "pwsh scripts/unattended-shortcut.ps1 $verb -ShortcutDir " + "'" + $_.args.ShortcutDir.Replace("'", "''") + "'"
    }
    Refuse "links of this project are recorded in $($sets.Count) folders; run once for each: $($each -join '; ')"
  } else {
    $set = $sets | Select-Object -First 1
  }
  # An argument given on the command line wins over the recorded one.
  if ($set) {
    foreach ($arg in 'Distro', 'Icon', 'Project', 'ShortcutDir', 'IconDir') {
      if (-not $PSBoundParameters.ContainsKey($arg) -and $set.args.$arg) {
        Set-Variable -Name $arg -Value $set.args.$arg
      }
    }
  }
}

# Links are written and read through the shell's own IShellLinkW (`scripts/lib/ShellLink.cs`, the launchers kit's,
# which this kit brings), which keeps a name, a mark, a path or an argument in any script at its full length, and
# spells a path long.
$linkCode = Join-Path $here 'lib\ShellLink.cs'
if (-not (Test-Path -LiteralPath $linkCode -PathType Leaf)) {
  Refuse 'there is no scripts/lib/ShellLink.cs (python forge.py kit <project> unattended installs it)'
}
Add-Type -Path $linkCode

if (-not $Project) { $Project = $root }
if (-not ($Project -match '^\\\\wsl(?:\.localhost|\$)\\' -or $Project.StartsWith('/'))) {
  $found = Resolve-Path -LiteralPath $Project -ErrorAction Ignore
  if (-not $found) { "unattended-shortcut: the project folder does not exist: '$Project'; nothing was written"; exit 1 }
  # ProviderPath, not Path: for a folder on a share Path keeps the provider's prefix
  # (`Microsoft.PowerShell.Core\FileSystem::`), which the mark and wslpath would then carry. Spelt long, so a folder
  # handed in by its 8.3 short name (C:\Users\RUNNER~1) is the same project as by its own.
  $Project = [Conclave.ShellPath]::Long($found.ProviderPath).TrimEnd('\')
}
$name = Split-Path $Project -Leaf
if (-not $IconDir) { $IconDir = Join-Path $env:LOCALAPPDATA $name }

# A PowerShell string literal of any text.
function PSQuote([string]$text) { "'" + $text.Replace("'", "''") + "'" }

# The project's identity, in its links' mark and its icons' key: one form for a project in WSL, whichever form -Project
# takes, with the distro in it, so /home/x/demo in Ubuntu and in Debian are two projects (D07 T01 section 7).
$projectDistro = ''
if ($Project -match '^\\\\wsl(?:\.localhost|\$)\\([^\\]+)\\(.*)$') {
  $projectDistro = $Matches[1]
  $identity = "\\wsl.localhost\$($Matches[1])\$($Matches[2].TrimEnd('\'))"
} elseif ($Project.StartsWith('/')) {
  if (-not $Distro) { Refuse "-Project '$Project' is a Linux path: -Distro names the distro it is in" }
  $projectDistro = $Distro
  $identity = "\\wsl.localhost\$Distro" + ($Project.TrimEnd('/') -replace '/', '\')
} else {
  $identity = $Project
}
# The form identities are compared in: a Windows path is spelt long and ignores case; in WSL the distro's name ignores
# case and the Linux path keeps it, since /home/x/Demo and /home/x/demo are two folders there.
function Get-Normal([string]$id) {
  if ($id -match '^\\\\wsl(?:\.localhost|\$)\\([^\\]+)(.*)$') {
    return '\\wsl.localhost\' + $Matches[1].ToLowerInvariant() + $Matches[2]
  }
  if ($id.StartsWith('/')) { return $id }
  [Conclave.ShellPath]::Long($id).ToLowerInvariant()
}
function Get-Key([string]$text) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))[0..3] | ForEach-Object { $_.ToString('x2') })
}
$prefix = 'conclave-unattended: '
$mark = "$prefix$identity"
$normal = Get-Normal $identity
$key = Get-Key $normal
# The key the script gave badges before it had one identity (the -Project form, in lower case). That key was one Linux
# path's in every distro, so a badge under it is this project's only when a link this project owns shows it (and no
# other link in its desktop folder does); it is never found by the key alone.
$oldKey = Get-Key $Project.ToLowerInvariant()
# A run is told apart from a later process that Windows gave the same id by when it started.
function Get-Started([int]$id) {
  $process = Get-Process -Id $id -ErrorAction Ignore
  # Ticks, written as text: ConvertFrom-Json would turn a date into a DateTime, which then never equals the text.
  if ($process) { [string]$process.StartTime.ToUniversalTime().Ticks } else { '' }
}
$labels = @{ claude = 'Claude'; codex = 'Codex'; agy = 'Gemini' }

# A link's fields, or nothing for a file the shell cannot load as one.
function Get-Link([IO.FileSystemInfo]$file) {
  try { [Conclave.ShellLink]::Read($file.FullName) } catch { $null }
}

function Get-Mark([IO.FileSystemInfo]$file) {
  $link = Get-Link $file
  if ($link) { $link.Description } else { '' }
}

# Whether a link is this project's: its mark names this identity, or it is the mark the script wrote before it had one
# identity (the -Project form as typed). An old mark of a Linux path names no distro, so it is this project's only when
# its link starts the run in this project's distro.
function Test-Owned([IO.FileSystemInfo]$file) {
  $text = Get-Mark $file
  if (-not $text.StartsWith($prefix)) { return $false }
  $id = Get-Normal $text.Substring($prefix.Length)
  if ($id -ceq $normal) { return $true }
  if (-not $Project.StartsWith('/') -or $id -cne $Project) { return $false }
  $words = "$(PSQuote '-d') $(PSQuote $projectDistro) "
  (Get-Link $file).Arguments.IndexOf($words, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

# This project's links: every .lnk in the desktop folder, hidden ones too, whose mark is its, whatever its name. A link
# that cannot be read (held open by another program) may be this project's: it is listed in $unreadableLinks, and a
# run that would change links stops on it.
$unreadableLinks = [Collections.Generic.List[string]]::new()
function Get-Owned {
  foreach ($link in @(Get-ChildItem -LiteralPath $ShortcutDir -Filter '*.lnk' -File -Force -ErrorAction Ignore)) {
    try { [void][Conclave.ShellLink]::Read($link.FullName) } catch {
      $cause = $_.Exception
      while ($cause.InnerException) { $cause = $cause.InnerException }
      # 0x80070020 or 0x80070005: held open, or not to be read now; any other failure is a file that is no link.
      if ($cause.HResult -in @(-2147024864, -2147024891)) {
        if (-not $script:unreadableLinks.Contains($link.FullName)) { $script:unreadableLinks.Add($link.FullName) }
      }
      continue
    }
    if (Test-Owned $link) { $link }
  }
}
function Stop-OnUnreadable {
  if ($script:unreadableLinks.Count) {
    Refuse ("$($script:unreadableLinks -join ', ') cannot be read now (held open by another program); close it and " +
      'run the script again')
  }
}

# Whether another distro also has this project's Linux path. The old key was the Linux path's alone, so only then can a
# badge under it be another distro's too, whatever folders their links sit in: WSL itself is asked, once, and when it
# cannot answer the badge is taken as shared. A project given by a \\wsl path or on a Windows drive had a key of
# its own.
$sharedOldKey = $null
function Test-SharedOldKey {
  if ($null -ne $script:sharedOldKey) { return $script:sharedOldKey }
  $script:sharedOldKey = $Project.StartsWith('/')
  if (-not $script:sharedOldKey) { return $false }
  $wslExe = if ($Wsl) { $Wsl } else {
    (Get-Command wsl.exe -CommandType Application -ErrorAction Ignore | Select-Object -First 1).Source
  }
  if (-not $wslExe) { return $true }
  $env:WSL_UTF8 = '1'
  $all = @(& $wslExe -l -q 2>$null | ForEach-Object { ($_ -replace "`0", '').Trim() } | Where-Object { $_ })
  if ($LASTEXITCODE -ne 0) { return $true }
  foreach ($distro in $all) {
    $spellings = Find-PathAnyCase $wslExe $distro $Project
    if ($null -eq $spellings) { return $true }  # WSL could not say
    # Another distro with the path in any case, or this one with it spelt otherwise too, may share the badge.
    $others = if ($distro -eq $projectDistro) { @($spellings | Where-Object { $_ -cne $Project }) } else { $spellings }
    if (@($others).Count) { return $true }
  }
  $script:sharedOldKey = $false
  $false
}

# Every spelling of `$path` that `$distro` has, in any letter case (the old key took the path in lower case), found one
# folder at a time: none, some, or $null when WSL cannot say (a folder it cannot list, a distro that does not start).
function Find-PathAnyCase([string]$wslExe, [string]$distro, [string]$path) {
  $found = @('/')
  foreach ($part in @($path.Trim('/').Split('/') | Where-Object { $_ })) {
    $pattern = $part -replace '([\\*?\[\]])', '\$1'  # find's -iname reads a glob: its own characters escaped
    $next = @()
    foreach ($dir in $found) {
      $out = @(& $wslExe -d $distro --exec find $dir -mindepth 1 -maxdepth 1 -iname $pattern -print 2>$null)
      if ($LASTEXITCODE -ne 0) { return $null }
      $next += @($out | ForEach-Object { ($_ -replace "`0", '').Trim() } | Where-Object { $_ })
    }
    if (-not $next) { return ,@() }
    $found = $next
  }
  ,@($found)
}

# Whether a link other than `$ours` shows `$badge` (the same file), in this run's desktop folder or in the user's or
# the public desktop, where the old script put its links unless told otherwise.
function Test-ShownByOthers([IO.FileSystemInfo]$badge, [object[]]$ours) {
  $paths = @($ours | ForEach-Object { Get-Normal $_.FullName })
  $folders = @(@($ShortcutDir) + $Desktops | Where-Object { $_ } | Select-Object -Unique)
  $links = foreach ($folder in $folders) {
    Get-ChildItem -LiteralPath $folder -Filter '*.lnk' -File -Force -ErrorAction Ignore  # hidden links too
  }
  foreach ($link in @($links)) {
    if ($paths -contains (Get-Normal $link.FullName)) { continue }
    $shown = (Get-Link $link).IconPath
    if ($shown -and (Get-Normal ([IO.Path]::GetFullPath($shown))) -ceq (Get-Normal $badge.FullName)) { return $true }
  }
  $false
}

# This project's badges: those under its key, and the old key's badges its own links show, when no other distro has
# its Linux path and no other link shows them.
# Named apart from the -Icon parameter: PowerShell names ignore case, and $icon would be that typed string.
function Get-Badges([object[]]$owned) {
  $found = @{}
  $mine = @(Get-ChildItem -LiteralPath $IconDir -Filter "campaign-*-$key.ico" -File -Force -ErrorAction Ignore)
  foreach ($badge in $mine) {
    $found[$badge.Name] = $badge
  }
  foreach ($link in $owned) {
    $where = (Get-Link $link).IconPath
    $shown = if ($where) { Get-Item -LiteralPath $where -Force -ErrorAction Ignore }
    if ($shown -and $shown.Name -like "campaign-*-$oldKey.ico" -and -not (Test-SharedOldKey) -and
        (Get-Normal $shown.DirectoryName) -ceq (Get-Normal ([IO.Path]::GetFullPath($IconDir))) -and
        -not (Test-ShownByOthers $shown $owned)) {
      $found[$shown.Name] = $shown
    }
  }
  $found.Values
}

# Each run that changes links keeps a journal, `<icon folder>\campaign-<key>.<pid>.journal.json`, written whole (a
# temporary file moved over it) before each step: the files it writes under temporary names, the moves it makes, and
# how far it got. A run stopped part way (its window closed, the machine lost power) leaves it behind, and before
# anything else each journal of this project's whose run is no longer running is played out: `writing` (only
# temporary files exist) removes them; `swapping` (the old set was being moved aside and the new one in) takes out each
# new file already moved in and puts each old one back, so the previous set stands whole; `committed` (the new set was
# in place) removes what was moved aside.
# Where every run of the user keeps each project's lock and journals: the user's own local application data, never a
# folder a run is given, so two runs given different folders still meet there.
$appData = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [Environment]::GetFolderPath('LocalApplicationData') }
$StateDir = Join-Path $appData 'conclave-unattended'
$journalPath = Join-Path $StateDir "campaign-$key.$PID.journal.json"
$started = Get-Started $PID
$moves = @()
$asidePlan = @()
function Write-Journal([string]$state) {
  $record = [ordered]@{ state = $state; started = $started; desk = $ShortcutDir; icons = $IconDir
    temps = @($moves | ForEach-Object { $_.from }); moves = @($moves); aside = @($asidePlan) }
  $temp = "$journalPath.tmp"
  [IO.File]::WriteAllText($temp, (ConvertTo-Json -InputObject $record -Depth 4), [Text.UTF8Encoding]::new($false))
  [IO.File]::Move($temp, $journalPath, $true)
}

# A journal is read as a claim, not trusted: it can name only this project's files in the two folders this run works
# in, by the names a run gives them, so one planted in a shared icon folder cannot move or delete anything else.
# A path is the folder's own file of `pattern`, or `$false`.
function Test-Within([string]$path, [string]$folder, [string]$pattern) {
  if (-not $path -or $path.IndexOfAny([IO.Path]::GetInvalidPathChars()) -ge 0) { return $false }
  $parent = [IO.Path]::GetDirectoryName($path)
  if (-not $parent) { return $false }
  $same = (Get-Normal ([IO.Path]::GetFullPath($parent))) -ceq (Get-Normal ([IO.Path]::GetFullPath($folder)))
  $same -and [IO.Path]::GetFileName($path) -match $pattern
}
# Whether a journal of the run `$owner` names only files such a run writes: links in the shortcut folder and badges in
# the icon folder, each moved aside as `<name>.<owner>.old` and written first as `<name>.<owner>.tmp.<ext>` (a link's
# temporary name drops its own `.lnk`).
function Test-Journal($record, [int]$owner) {
  if ($record.state -notin @('writing', 'swapping', 'committed')) { return $false }
  # The folders the journal's own run worked in (a run may be given others).
  $desk = if ($record.desk) { $record.desk } else { $ShortcutDir }
  $icons = if ($record.icons) { $record.icons } else { $IconDir }
  foreach ($m in @($record.moves)) {
    $link = Test-Within $m.to $desk '\.lnk$'
    if (-not ($link -or (Test-Within $m.to $icons "^campaign-[a-z]+-$key\.ico$"))) { return $false }
    $stem = if ($link) { $m.to.Substring(0, $m.to.Length - 4) } else { $m.to }
    $suffix = if ($link) { 'lnk' } else { 'ico' }
    if ($m.from -cne "$stem.$owner.tmp.$suffix") { return $false }
  }
  foreach ($old in @($record.aside)) {
    $inside = (Test-Within $old.to $desk '\.lnk$') -or
      (Test-Within $old.to $icons "^campaign-[a-z]+-($key|$oldKey)\.ico$")
    if (-not $inside -or $old.old -cne "$($old.to).$owner.old") { return $false }
  }
  $temps = @($record.moves | ForEach-Object { $_.from })
  foreach ($temp in @($record.temps)) { if ($temps -cnotcontains $temp) { return $false } }
  $true
}
# Whether a file a journal would remove is this project's: a link with its mark, a badge under its key.
function Test-Mine([string]$path) {
  $file = Get-Item -LiteralPath $path -Force -ErrorAction Ignore  # -Force: a hidden link is the project's too
  if (-not $file) { return $false }
  # A badge only by its exact name (under its key or the old one, or that moved aside or written first); any other file,
  # `campaign-claude-<key>.ico.lnk` included, is this project's only by its mark.
  if ($file.Name -match "^campaign-[a-z]+-($key|$oldKey)\.ico(\.\d+\.(old|tmp\.ico))?$") { return $true }
  Test-Owned $file
}

# Each step is safe to repeat, so a recovery that is itself stopped is finished by the next run: a file moved in is
# taken out only while what it replaced is still moved aside (or, for a new name, while it is still this project's),
# and a file moved aside goes back only where nothing has taken its name.
function Restore-Stopped {
  $journals = @(Get-ChildItem -LiteralPath $StateDir -Filter "campaign-$key.*.journal.json*" -File -Force `
    -ErrorAction Ignore)
  # Every stopped run's journal is read and checked first: one that cannot be played holds them all, so nothing is
  # replayed over a swap that is not understood.
  $play = @()
  foreach ($file in $journals) {
    if ($file.Name -notmatch '\.(\d+)\.journal\.json(\.tmp)?$') { continue }
    $owner, $partial = [int]$Matches[1], [bool]$Matches[2]
    # A journal under this run's own id is an earlier process's: this run has written none yet.
    if ($partial) {  # the journal step it was writing never began
      if ($owner -eq $PID -or -not (Get-Process -Id $owner -ErrorAction Ignore)) {
        Remove-Item -LiteralPath $file.FullName
      }
      continue
    }
    $record = try { Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8 | ConvertFrom-Json } catch { $null }
    # Its run is still going only when a process with its id started when the journal says: Windows reuses ids.
    if ($record -and $record.started -and (Get-Started $owner) -ceq $record.started) { continue }
    if (-not $record -or -not (Test-Journal $record $owner)) {
      "unattended-shortcut: $($file.FullName) cannot be read or names files outside this project's links; it " +
        'is left as it is, and no run changes this project''s links until it is moved away'
      $script:keptJournals++
      $script:unreadJournals++
      continue
    }
    # A file it would take out or put back that is not this project's (another project's link at a moved-in name, a
    # planted record) holds it whole, as an unreadable one does: a run cannot tell a race from a planted record, so it
    # changes nothing over either until the file or the journal is moved away.
    # Every file it names (a destination, whether or not its own file was moved in yet; a file moved aside; a temporary
    # file) that holds another project's file is not this run's to touch.
    $foreign = @(@($record.moves | Where-Object { (Test-Path -LiteralPath $_.to) -and -not (Test-Mine $_.to) } |
        ForEach-Object { $_.to }) +
      @($record.aside | Where-Object { (Test-Path -LiteralPath $_.old) -and -not (Test-Mine $_.old) } |
        ForEach-Object { $_.old }) +
      @($record.aside | Where-Object { (Test-Path -LiteralPath $_.to) -and -not (Test-Mine $_.to) } |
        ForEach-Object { $_.to }) +
      @(@($record.moves | ForEach-Object { $_.from }) + @($record.temps) | Select-Object -Unique |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) -and -not (Test-Mine $_) }))
    if ($foreign) {
      "unattended-shortcut: $($file.FullName) names $($foreign -join ', '), which is not this project's: the journal " +
        'and those files are left as they are, and no run changes this project''s links until one is moved away'
      $script:keptJournals++
      $script:unreadJournals++
      continue
    }
    $play += , @($file, $record, $owner)
  }
  if ($script:unreadJournals) { return }
  foreach ($entry in $play) {
    $file, $record, $owner = $entry
    # -Remove takes the set off: before it deletes anything the journal says `committed`, so a removal that is itself
    # stopped part way is finished by any later run as a removal, and no original it took off comes back.
    if ($Remove -and $record.state -ne 'committed') {
      $record.state = 'committed'
      $rewrite = "$($file.FullName).tmp"
      $text = ConvertTo-Json -InputObject $record -Depth 4
      [IO.File]::WriteAllText($rewrite, $text, [Text.UTF8Encoding]::new($false))
      [IO.File]::Move($rewrite, $file.FullName, $true)
    }
    # Each step that fails (a file held open, a name another file holds) is said, counted, and left for the next run,
    # which finds the journal still there.
    $staying = 0
    foreach ($temp in @($record.temps)) {
      if (Test-Mine $temp) {
        try { Remove-Item -LiteralPath $temp; "removed: $temp" }
        catch { $staying++; "unattended-shortcut: $temp stays ($_); the journal is kept and the next run tries again" }
      }
    }
    if ($record.state -eq 'swapping') {
      foreach ($m in @($record.moves)) {
        $replaced = @($record.aside | Where-Object { $_.to -ceq $m.to })
        # Only this project's own file is ever taken out: one another project put at the name since is never touched.
        $new = if ($replaced) { Test-Path -LiteralPath $replaced[0].old } else { -not (Test-Path -LiteralPath $m.from) }
        if ($new -and (Test-Mine $m.to)) {
          try { Remove-Item -LiteralPath $m.to; "removed: $($m.to)" }
          catch {
            $staying++
            "unattended-shortcut: $($m.to) stays ($_); the journal is kept and the next run tries again"
          }
        }
      }
      foreach ($old in @($record.aside)) {
        if ((Test-Path -LiteralPath $old.old) -and -not (Test-Path -LiteralPath $old.to) -and (Test-Mine $old.old)) {
          try { Move-Item -LiteralPath $old.old -Destination $old.to; "restored: $($old.to)" }
          catch { "unattended-shortcut: $($old.to) could not be put back ($_)" }
        }
      }
      $left = @($record.aside | Where-Object { Test-Path -LiteralPath $_.old })
      foreach ($old in $left) {
        "unattended-shortcut: $($old.old) cannot go back to $($old.to) (another file holds the name, or it is held " +
          'open); the journal is kept and the next run tries again'
      }
    } elseif ($record.state -eq 'committed') {
      foreach ($old in @($record.aside)) {
        if (Test-Mine $old.old) {
          try { Remove-Item -LiteralPath $old.old -Force; "removed: $($old.old)" }
          catch { "unattended-shortcut: $($old.old) stays ($_); the journal is kept and the next run tries again" }
        }
      }
      $left = @($record.aside | Where-Object { Test-Path -LiteralPath $_.old })
    } else {
      $left = @()
    }
    if ($staying) { $left += @($staying) }
    # The journal goes once nothing it names is left to do; until then each run plays it again.
    if (-not $left) { Remove-Item -LiteralPath $file.FullName; continue }
    $script:keptJournals++
  }
}
# One run of the project at a time: every run that changes links or plays a journal holds this lock for its life. It is
# opened with no sharing and deleted when closed, so the system frees it when the process ends, however it ends.
# -Check only reads, so it makes no state folder and takes no lock: it needs no right to write anywhere.
if (-not $Check) {
  try { New-Item -ItemType Directory -Force -Path $StateDir | Out-Null }
  catch { Refuse "the state folder '$StateDir' cannot be made ($($_.Exception.Message))" }
  $lockPath = Join-Path $StateDir "campaign-$key.lock"
  $lock = $null
  for ($try = 0; $try -lt 20 -and -not $lock; $try++) {
    try {
      $lock = [IO.FileStream]::new($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None, 1, [IO.FileOptions]::DeleteOnClose)
    } catch {
      $cause = $_.Exception
      while ($cause.InnerException) { $cause = $cause.InnerException }
      # 0x80070020: a sharing violation, another run holding the lock; any other failure is said as it is.
      if ($cause.HResult -ne -2147024864) { Refuse "the lock '$lockPath' cannot be opened ($($cause.Message))" }
      Start-Sleep -Milliseconds 250
    }
  }
  if (-not $lock) { Refuse "another run of this project holds $lockPath; run the script again once it has ended" }
}

$keptJournals = 0
$unreadJournals = 0
# -Check only reads: a stopped run's journal is named, never played (that would change what it is asked to inspect).
if ($Check) {
  $stopped = @(Get-ChildItem -LiteralPath $StateDir -Filter "campaign-$key.*.journal.json*" -File -Force `
      -ErrorAction Ignore)
  if ($stopped) {
    foreach ($file in $stopped) { "unfinished: $($file.FullName)" }
    "unattended-shortcut: a stopped run of this project is not finished, so its links are not checked; " +
      'pwsh scripts/unattended-shortcut.ps1 -Refresh finishes it'
    exit 1
  }
}
# Recovery writes, so it is the writer's alone: -Check never plays a journal, even one a stopped run left after its
# scan above.
if (-not $Check) { Restore-Stopped }
# The set a refresh or a check works from is read again now that no other run can change it: a removal or a write
# that ran while this one waited for the lock is what it sees.
if ($set) {
  $again = Read-Record
  if (-not $again.Ok) { Refuse "the record of '$($set.args.ShortcutDir)' cannot be read now ($($again.Why))" }
  $now = @($again.Sets) | Where-Object { (Get-DeskKey $_.args.ShortcutDir) -eq (Get-DeskKey $set.args.ShortcutDir) } |
    Select-Object -First 1
  if (-not $now -or ($now | ConvertTo-Json -Depth 6 -Compress) -ne ($set | ConvertTo-Json -Depth 6 -Compress)) {
    Refuse "the record of '$($set.args.ShortcutDir)' changed while this run waited for the lock; run it again"
  }
}
# A journal that cannot be read may name a swap not finished: nothing changes this project's links over it.
if ($unreadJournals) {
  Refuse 'a journal of this project cannot be played (see above); move it away once its files are sorted'
}

# -Remove is one journaled change too (-Force throughout: a hidden or read-only link is the project's too): every
# owned link is moved aside, then every badge (so no link is ever left
# without its icon), and only once all are aside are they deleted. A failure puts every one back; a stop leaves a
# journal the next run plays out (a `swapping` one puts the set back, a `committed` one finishes the removal).
# Writes the record with this folder's set replaced by `$entry` (taken out for $null), whole: a temporary file moved
# over the old. The links are in place, or gone, whatever happens here: a record that cannot be written is said, and
# `status` cannot then tell a stale link (-Check still can).
function Save-Sets($entry) {
  $read = Read-Record
  if (-not $read.Ok) {
    "unattended-shortcut: the record $recordPath cannot be read ($($read.Why)), so it is left as it is (it may hold " +
      "other folders' sets); status cannot tell whether these links are current (-Check reads them). To start a new " +
      "record: $($read.Advice)"
    return
  }
  $others = @($read.Sets | Where-Object { $_ -and (Get-DeskKey $_.args.ShortcutDir) -ne (Get-DeskKey $ShortcutDir) })
  $all = @($others) + @($entry | Where-Object { $_ })
  $temp = "$recordPath.$PID.tmp"
  try {
    if (-not $all) { if (Test-Path -LiteralPath $recordPath) { Remove-Item -LiteralPath $recordPath }; return }
    New-Item -ItemType Directory -Force -Path (Split-Path $recordPath) | Out-Null
    [IO.File]::WriteAllText($temp, (@{ sets = $all } | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temp, $recordPath, $true)
  } catch {
    Remove-Item -LiteralPath $temp -ErrorAction Ignore
    "unattended-shortcut: the record $recordPath could not be written ($($_.Exception.Message)); status cannot " +
      'tell whether the links are current (-Check reads them)'
  }
}

if ($Remove) {
  $links = @(Get-Owned)
  Stop-OnUnreadable
  $badges = @(Get-Badges $links)
  # The project's links in another recorded folder that share this icon folder show the same badges: they stay. So
  # do they while the record cannot be read, since it may name such a folder.
  $read = Read-Record
  $sharing = @($read.Sets | Where-Object {
      (Get-DeskKey $_.args.ShortcutDir) -ne (Get-DeskKey $ShortcutDir) -and $_.args.IconDir -and
      (Get-DeskKey $_.args.IconDir) -eq (Get-DeskKey $IconDir) })
  if (-not $read.Ok) {
    "unattended-shortcut: the record $recordPath cannot be read ($($read.Why)), so the badges in '$IconDir' stay " +
      "(another folder's links may show them)"
  }
  if ($sharing -or -not $read.Ok) { $badges = @() }
  $asidePlan = @(@($links) + @($badges) |
    ForEach-Object { [pscustomobject]@{ to = $_.FullName; old = "$($_.FullName).$PID.old" } })
  $aside = [Collections.Generic.List[object]]::new()
  try {
    Write-Journal 'swapping'
    foreach ($old in $asidePlan) { Move-Item -LiteralPath $old.to -Destination $old.old; $aside.Add($old) }
    Write-Journal 'committed'
  } catch {
    $why = $_.Exception.Message
    $whole = $true
    foreach ($old in $aside) {
      try { Move-Item -LiteralPath $old.old -Destination $old.to -ErrorAction Stop } catch { $whole = $false }
    }
    if ($whole) { Remove-Item -LiteralPath $journalPath -ErrorAction Ignore }
    "unattended-shortcut: the shortcuts could not be removed ($why); they stay as they were; run -Remove again"
    exit 1
  }
  foreach ($old in $aside) { "removed: $($old.to)" }
  $left = @($aside |
    Where-Object { Remove-Item -LiteralPath $_.old -Force -ErrorAction Ignore; Test-Path -LiteralPath $_.old })
  if (-not $left) { Remove-Item -LiteralPath $journalPath -ErrorAction Ignore }
  Save-Sets $null
  "unattended-shortcut: $($links.Count) shortcut(s) and $($badges.Count) icon(s) of $Project removed"
  if ($left -or $keptJournals) {
    "unattended-shortcut: a journal is kept (see above); run -Remove again once it can finish"
    exit 1
  }
  exit 0
}

# A stopped run whose journal is kept (an original it cannot put back yet) is finished first: a new set written over
# it would be undone when that journal is played again.
if ($keptJournals) { Refuse "a stopped run is not finished (see above); free what it names and run the script again" }
if (($Refresh -or $Check) -and -not $set) {
  $found = @(Get-Owned)
  Stop-OnUnreadable
  if (-not $found) {
    "unattended-shortcut: no shortcut of $Project is recorded or in '$ShortcutDir'; nothing to do"
    exit 0
  }
  # The distro is in the link's command; the icon it was made from is not, so the operator names it.
  $seen = '<distro>'
  if ((Get-Link $found[0]).Arguments -match "'-d' '((?:[^']|'')+)'") { $seen = $Matches[1] -replace "''", "'" }
  "unattended-shortcut: $($found.Count) shortcut(s) of $Project in '$ShortcutDir' were written before this kit " +
    'kept a record of them, so they cannot be read back as current; write them again once with: ' +
    "pwsh scripts/unattended-shortcut.ps1 -Distro $(PSQuote $seen) -Icon <the project's icon> -ShortcutDir " +
    (PSQuote $ShortcutDir)
  exit 1
}
if (-not $Distro) { Refuse '-Distro names no WSL distro' }
if (-not $Check -and -not ($Icon -and (Test-Path -LiteralPath $Icon -PathType Leaf))) {
  Refuse "-Icon is not a file: '$Icon'"
}
if (-not $Python) {
  $Python = (Get-Command python -CommandType Application -ErrorAction Ignore | Select-Object -First 1).Source
}
if (-not $Python) { Refuse 'python is not on PATH' }
if (-not $Wsl) {
  $Wsl = (Get-Command wsl.exe -CommandType Application -ErrorAction Ignore | Select-Object -First 1).Source
}
if (-not $Wsl) { Refuse 'wsl.exe is not on PATH: install WSL' }
$env:WSL_UTF8 = '1'  # wsl.exe lists its distros in UTF-16 otherwise
# Every call into WSL is --exec: `wsl.exe -- <command>` hands the command to the login shell, which eats the
# backslashes of a Windows path and parses the start command again (found by the live proof, 2026-11-13).

# The runners, read by the kit's own profile loader, so a profile the run would refuse is refused here too.
$reader = @'
import importlib.util, json, sys
spec = importlib.util.spec_from_file_location("unattended_lib", sys.argv[1])
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)
profile = lib.load_profile(sys.argv[2])
print(json.dumps([profile["agent"], *profile.get("runners", {})]))
'@
$read = & $Python -B -c $reader (Join-Path $here 'lib\unattended.py') $root 2>&1
if ($LASTEXITCODE -ne 0) { Refuse "the run profile cannot be read: $(($read | Out-String).Trim())" }
$agents = @(($read | Select-Object -Last 1) | ConvertFrom-Json)

# The title, read by the launchers kit's own loader (which this kit brings), so a profile its links would refuse is
# refused here too; `null` is a profile with no title yet, as that kit installs it.
$title = $name
if (Test-Path -LiteralPath (Join-Path $root '.conclave\launchers.json')) {
  $loader = Join-Path $here 'lib\launchers.py'
  if (-not (Test-Path -LiteralPath $loader -PathType Leaf)) {
    Refuse ('the launcher profile cannot be read: there is no scripts/lib/launchers.py ' +
      '(python forge.py kit <project> unattended installs it)')
  }
  $read = & $Python -B $loader title --root $root 2>&1
  if ($LASTEXITCODE -ne 0) { Refuse "the launcher profile cannot be read: $(($read | Out-String).Trim())" }
  $set = ($read | Select-Object -Last 1) | ConvertFrom-Json
  if ($set) { $title = $set }
}

$listed = @(& $Wsl -l -q 2>$null | ForEach-Object { ($_ -replace "`0", '').Trim() } | Where-Object { $_ })
if ($LASTEXITCODE -ne 0 -or $listed -notcontains $Distro) {
  Refuse "WSL has no distro '$Distro' (it lists: $($listed -join ', '))"
}

# The project's path in WSL: a \\wsl.localhost\<distro>\ or \\wsl$\<distro>\ path is that distro's own path, a Linux
# path is itself, and a Windows-drive path is what wslpath says.
if ($Project -match '^\\\\wsl(?:\.localhost|\$)\\([^\\]+)\\(.*)$') {
  if ($Matches[1] -ne $Distro) { Refuse "the project is in distro '$($Matches[1])', not '$Distro'" }
  $linux = '/' + ($Matches[2] -replace '\\', '/')
} elseif ($Project.StartsWith('/')) {
  $linux = $Project
} else {
  $linux = (& $Wsl -d $Distro --exec wslpath -a $Project 2>$null | Select-Object -First 1)
  if ($LASTEXITCODE -ne 0 -or -not $linux) { Refuse "wslpath could not translate '$Project'" }
  $linux = $linux.Trim()
}
& $Wsl -d $Distro --exec test -d $linux 2>$null
if ($LASTEXITCODE -ne 0) { Refuse "the project path '$linux' does not exist in '$Distro'" }

# One argument as the Windows command line passes it (backslashes doubled before a quote, the quote escaped).
function Quote([string]$arg) {
  if ($arg -and $arg -notmatch '[\s"]') { return $arg }
  '"' + ($arg -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

$renderer = Join-Path $here 'campaign-icon.py'
$pwsh = (Get-Process -Id $PID).Path  # the PowerShell 7 that runs this script runs the links too
# ProviderPath, not Path: for an icon on a share (\\wsl.localhost\...) Path keeps the provider's prefix
# (`Microsoft.PowerShell.Core\FileSystem::`), and the renderer cannot open a file named that way.
$brand = if ($Check) { $Icon } else { (Resolve-Path -LiteralPath $Icon).ProviderPath }

# Every link and its badge, by runner. A name held by a file this project did not write (another project's link with
# the same title and runner, or a folder) is refused before anything is written: a move would replace it.
# The commands are `unattended-run.py`'s start_command, watch_command and stop_command (a test holds them equal).
# The campaign link starts the run, then attaches to it writable (a read-only tmux client takes no key and no mouse
# but detach, not even a scroll), so a run already going is just attached to. Stop's command holds no double quote:
# the Windows command line between PowerShell and wsl.exe strips them.
function Get-StartCommand([string]$agent) {
  "python3 scripts/unattended-run.py start --agent $agent; python3 scripts/unattended-run.py attach || " +
    "{ echo 'The run did not start: the reason is above. Press Enter to close.'; read -r _; }"
}
$watchCommand = "python3 scripts/unattended-run.py attach || " +
  "{ echo 'There is no run to watch: the reason is above. Press Enter to close.'; read -r _; }"
$stopCommand = "read -r -p 'Stop the unattended run? Type y and press Enter: ' answer; " +
  'case $answer in y) python3 scripts/unattended-run.py stop;; *) echo ''Not stopped.'';; esac; ' +
  "echo 'Press Enter to close.'; read -r _"
function New-Planned([string]$kind, [string]$label, [string]$command, [string]$what) {
  $badge = Join-Path $IconDir "campaign-$kind-$key.ico"
  [pscustomobject]@{ Agent = $kind; Label = $label; Badge = $badge; BadgeTemp = "$badge.$PID.tmp.ico"
    Link = Join-Path $ShortcutDir "$label.lnk"; LinkTemp = Join-Path $ShortcutDir "$label.$PID.tmp.lnk"
    Command = $command; What = $what }
}
# One campaign link per runner, then Watch and Stop, one each whatever the runner, badged like it (an eye, a square).
$planned = @(foreach ($agent in $agents) {
    New-Planned $agent "$title $($labels[$agent]) campaign" (Get-StartCommand $agent) 'start the run'
  }) + @(
  (New-Planned 'watch' "$title - Watch run" $watchCommand 'watch the run'),
  (New-Planned 'stop' "$title - Stop run" $stopCommand 'stop the run'))
# The arguments a link of `$p` carries: PowerShell runs wsl.exe, so a failure of WSL itself (a drive gone, a distro
# removed) is said and waits for Enter too; the command is in the link, not in a script on the project's drive, which
# may be the one that is gone.
function Get-LinkArguments($p) {
  $words = @('-d', $Distro, '--cd', $linux, '--exec', 'bash', '-lc', $p.Command)
  $code = "& $(PSQuote $Wsl) $(($words | ForEach-Object { PSQuote $_ }) -join ' '); if (`$LASTEXITCODE -ne 0) { " +
    "Write-Host ('WSL could not $($p.What) (exit ' + `$LASTEXITCODE + '): the reason is above. ' + " +
    "'Press Enter to close.'); " +
    "[void][Console]::ReadLine() }"
  "-NoLogo -NoProfile -Command $(Quote $code)"
}

# This project's links as they are, before this run writes any file of its own: one that cannot be read stops it.
$owned = @(Get-Owned)
Stop-OnUnreadable
if ($Check) {
  # Each link this script would write, read back: missing, current or stale (another command, target or icon), and
  # each link of this project's it would not write. Nothing is written.
  $bad = 0
  foreach ($p in $planned) {
    $file = Get-Item -LiteralPath $p.Link -Force -ErrorAction Ignore
    $link = if ($file) { Get-Link $file }
    if (-not $link) { "missing: $($p.Link)"; $bad++; continue }
    $same = $link.Arguments -ceq (Get-LinkArguments $p) -and $link.Target -eq $pwsh -and
      $link.Description -ceq $mark -and $link.IconPath -eq $p.Badge -and
      (Test-Path -LiteralPath $p.Badge -PathType Leaf)
    if ($same) { "current: $($p.Link)" } else { "stale: $($p.Link)"; $bad++ }
  }
  $names = @($planned | ForEach-Object { Split-Path $_.Link -Leaf })
  foreach ($file in $owned | Where-Object { $names -notcontains $_.Name }) {
    "not written now: $($file.FullName)"
    $bad++
  }
  if ($bad) {
    "unattended-shortcut: $bad shortcut(s) are not current; run pwsh scripts/unattended-shortcut.ps1 -Refresh"
    exit 1
  }
  "unattended-shortcut: every shortcut of $Project in '$ShortcutDir' is current"
  exit 0
}
foreach ($p in $planned) {
  $held = Get-Item -LiteralPath $p.Link -Force -ErrorAction Ignore
  if ($held -and ($held.PSIsContainer -or -not (Test-Owned $held))) {
    Refuse "the shortcut name '$($p.Label).lnk' is held by a file this project did not write: '$($held.FullName)'"
  }
}
# A link or badge of this project's that this run does not write (an earlier title, a runner dropped, an old mark's)
# goes in the same change. Compared by file name: each list is of one folder, whose spelling (an 8.3 short name) may
# differ.
$writtenLinks = @($planned | ForEach-Object { Split-Path $_.Link -Leaf })
$writtenIcons = @($planned | ForEach-Object { Split-Path $_.Badge -Leaf })
# Every badge it retires, an old key's its own links show included, goes in the same change as the links.
$stale = @($owned | Where-Object { $writtenLinks -notcontains $_.Name }) +
  @(Get-Badges $owned | Where-Object { $writtenIcons -notcontains $_.Name })
# Removes this run's temporary files; returns how many could not be removed.
function Clear-Temps {
  $kept = 0
  foreach ($temp in @($planned | ForEach-Object { $_.BadgeTemp; $_.LinkTemp })) {
    if (-not (Test-Path -LiteralPath $temp)) { continue }
    try { Remove-Item -LiteralPath $temp } catch { $kept++; "unattended-shortcut: $temp stays ($_)" }
  }
  $kept
}

$moves = foreach ($p in $planned) {
  [pscustomobject]@{ from = $p.BadgeTemp; to = $p.Badge }
  [pscustomobject]@{ from = $p.LinkTemp; to = $p.Link }
}
$asidePlan = @()

# Ends a run that failed: its journal goes once its temporary files are gone, and stays (for the next run, which
# removes them) while one of them cannot be removed.
function Stop-Run([string]$why) {
  $out = @(Clear-Temps)
  $out | Select-Object -SkipLast 1
  if (-not $out[-1]) { Remove-Item -LiteralPath $journalPath -ErrorAction Ignore }
  "unattended-shortcut: $why"
  exit 1
}

# Two steps: every badge and link is written under its temporary name first, and only then moved over the old, so a
# failure before the moves leaves every previous link and badge as it was and no temporary file.
New-Item -ItemType Directory -Force -Path $IconDir, $ShortcutDir | Out-Null
Write-Journal 'writing'
foreach ($p in $planned) {
  & $Python -B $renderer badge $brand $p.Agent $p.BadgeTemp
  if ($LASTEXITCODE -ne 0) { Stop-Run "the icon for $($p.Label) could not be rendered; its shortcut is left as it was" }
  try {
    $data = [Conclave.ShellLinkData]::new()
    $data.Target = $pwsh
    $data.Arguments = Get-LinkArguments $p
    $data.WorkingDirectory = $env:USERPROFILE
    $data.Description = $mark
    $data.IconPath = $p.Badge
    [Conclave.ShellLink]::Write($p.LinkTemp, $data)
  } catch {
    Stop-Run "the shortcut $($p.Label) could not be saved ($($_.Exception.Message)); the old one is kept"
  }
}

# The new set goes in as one change: every link and badge it replaces or retires is moved aside first, each link moved
# aside is checked again (now that no other writer can change it, so one another installer put there after the check
# above is never taken), then each new file is moved in (never over a file), and on any failure each file moved in is
# taken out and each file moved aside is put back. The journal says how far it got, for a run stopped part way.
$asidePlan = @(@($moves | Where-Object { Test-Path -LiteralPath $_.to } | ForEach-Object { $_.to }) +
  @($stale | ForEach-Object { $_.FullName }) | ForEach-Object { [pscustomobject]@{ to = $_; old = "$_.$PID.old" } })
$aside = [Collections.Generic.List[object]]::new()
$placed = [Collections.Generic.List[string]]::new()
try {
  Write-Journal 'swapping'
  foreach ($old in $asidePlan) { Move-Item -LiteralPath $old.to -Destination $old.old; $aside.Add($old) }
  foreach ($old in $aside) {
    if ($old.to.EndsWith('.lnk') -and -not (Test-Owned (Get-Item -LiteralPath $old.old -Force))) {
      throw "$($old.to) was written by another project while this run was working"
    }
  }
  foreach ($m in $moves) { Move-Item -LiteralPath $m.from -Destination $m.to; $placed.Add($m.to) }
  Write-Journal 'committed'
} catch {
  $why = $_.Exception.Message
  $whole = $true
  foreach ($to in $placed) { try { Remove-Item -LiteralPath $to -ErrorAction Stop } catch { $whole = $false } }
  foreach ($old in $aside) {
    try { Move-Item -LiteralPath $old.old -Destination $old.to -ErrorAction Stop } catch { $whole = $false }
  }
  if ($whole) { Stop-Run "the shortcuts could not be put in place ($why); the previous ones are kept" }
  # The journal still says `swapping`, so the next run puts back what this one could not.
  @(Clear-Temps) | Select-Object -SkipLast 1
  "unattended-shortcut: the shortcuts could not be put in place ($why), and the previous ones could not all be " +
    "put back; $journalPath is kept and the next run finishes it"
  exit 1
}
foreach ($p in $planned) { "shortcut written: $($p.Link)" }
foreach ($file in $stale) { "removed: $($file.FullName)" }
# This folder's set in the record, for -Refresh, -Check and `status`.
Save-Sets ([ordered]@{
    written = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    args = [ordered]@{ Distro = $Distro; Icon = $brand; Project = $Project
      ShortcutDir = [IO.Path]::GetFullPath($ShortcutDir); IconDir = [IO.Path]::GetFullPath($IconDir) }
    links = @($planned | ForEach-Object { [ordered]@{ label = $_.Label; kind = $_.Agent; command = $_.Command } })
  })
# A file moved aside that cannot be deleted now stays named in the journal (`committed`), and the next run, or -Remove,
# removes it.
$left = @($aside |
  Where-Object { Remove-Item -LiteralPath $_.old -Force -ErrorAction Ignore; Test-Path -LiteralPath $_.old })
if (-not $left) { Remove-Item -LiteralPath $journalPath -ErrorAction Ignore; exit 0 }
"unattended-shortcut: the shortcuts are in place; $($left.Count) old file(s) are left for the next run"
exit 1
