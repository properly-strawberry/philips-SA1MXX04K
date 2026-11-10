# Run from Windows: powershell -ExecutionPolicy Bypass -File tools/test_windows.ps1
# Uses only this project's executable and its local FFmpeg SDK. No installs.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'dist\philips.exe'
$ffmpeg = Join-Path $root 'third_party\ffmpeg\bin\ffmpeg.exe'
$work = Join-Path $root 'gen\player-tests'
$music = Join-Path $work 'Music'
$nested = Join-Path $music (([char]0x104) + 'zuolas')
New-Item -ItemType Directory -Force $nested | Out-Null
$script:checks = [System.Collections.Generic.List[string]]::new()

function Run-Player([string[]] $Arguments) {
    $quoted = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $process = Start-Process -FilePath $exe -ArgumentList $quoted -PassThru
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw 'Player test timed out after 30 seconds'
    }
    return $process.ExitCode
}
function Check([bool] $Ok, [string] $Name) {
    if (-not $Ok) { throw "FAIL $Name" }
    $script:checks.Add("PASS $Name")
    Write-Host "PASS $Name"
}
function Tone([string] $Path, [int] $Rate, [int] $Channels) {
    $frames = $Rate * 3
    $bytes = $frames * $Channels * 2
    $file = [IO.File]::Create($Path)
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([int]($bytes + 36))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
        $writer.Write([int]16); $writer.Write([int16]1)
        $writer.Write([int16]$Channels); $writer.Write([int]$Rate)
        $writer.Write([int]($Rate * $Channels * 2))
        $writer.Write([int16]($Channels * 2)); $writer.Write([int16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([int]$bytes)
        for ($i=0; $i -lt $frames; $i++) {
            $writer.Write([int16](9000 * [Math]::Sin(2 * [Math]::PI * 440 * $i / $Rate)))
            if ($Channels -eq 2) {
                $writer.Write([int16](6000 * [Math]::Sin(2 * [Math]::PI * 660 * $i / $Rate)))
            }
        }
    } finally { $writer.Dispose(); $file.Dispose() }
}

if (-not (Test-Path $exe)) { throw "Build missing: $exe" }
if (-not (Test-Path $ffmpeg)) { throw "FFmpeg SDK missing: $ffmpeg" }
$wav = Join-Path $music ('01 - ' + [char]0x160 + 'irdis.wav')
Tone $wav 48000 2
$flac = Join-Path $nested ('02 - ' + [char]0x17D + 'vaigzdes.flac')
$mp3 = Join-Path $music '03 - Sunshine.mp3'
$aac = Join-Path $work 'test.m4a'
$opus = Join-Path $work 'test.opus'
$mono = Join-Path $work 'mono-22050.wav'
Tone $mono 22050 1
foreach ($target in @($flac, $mp3, $aac, $opus)) {
    & $ffmpeg -v error -nostdin -y -i $wav -metadata title='GoGear test' -metadata artist='Test artist' $target
    if ($LASTEXITCODE -ne 0) { throw "FFmpeg fixture encoding failed: $target" }
}
foreach ($inputFile in @($wav, $flac, $mp3, $aac, $opus, $mono)) {
    $id = [IO.Path]::GetFileName($inputFile)
    $actual = Join-Path $work ($id + '.actual.pcm')
    $expected = Join-Path $work ($id + '.expected.pcm')
    Remove-Item $actual -ErrorAction SilentlyContinue
    & $ffmpeg -v error -nostdin -y -i $inputFile -map 0:a:0 -ac 2 -ar 48000 -c:a pcm_s16le -f s16le $expected
    if ($LASTEXITCODE -ne 0) { throw 'Reference PCM decoding failed' }
    $result = Run-Player @('--decode-test', $inputFile, $actual)
    Check ($result -eq 0) "decoder exit: $id"
    Check ((Get-Item $actual).Length -gt 0) "nonempty PCM: $id"
    Check ((Get-FileHash $actual).Hash -eq (Get-FileHash $expected).Hash) "sample-exact FFmpeg reference: $id"
}
$broken = Join-Path $work 'broken.mp3'
foreach ($inputFile in @($wav, $flac, $mp3, $aac, $opus, $mono)) {
    $id = [IO.Path]::GetFileName($inputFile)
    $playbackReport = Join-Path $work ($id + '.playback.txt')
    Remove-Item $playbackReport -ErrorAction SilentlyContinue
    Check ((Run-Player @('--playback-test', $inputFile, $playbackReport)) -eq 0) "buffered waveOut playback: $id"
    $stats = Get-Content -Raw $playbackReport | ConvertFrom-StringData
    Check ($stats.finished -eq '1' -and $stats.error -eq '0') "playback drains to EOF: $id"
    Check ($stats.underruns -eq '0') "no output underruns: $id"
    Check ([double]::Parse($stats.peak_buffer_seconds, [Globalization.CultureInfo]::InvariantCulture) -ge 0.5) "startup prebuffer and read-ahead: $id"
}
foreach ($inputFile in @($wav, $flac)) {
    $id = [IO.Path]::GetFileName($inputFile)
    $actual = Join-Path $work ($id + '.seek.pcm')
    $expected = Join-Path $work ($id + '.seek-expected.pcm')
    $full = [IO.File]::ReadAllBytes((Join-Path $work ($id + '.expected.pcm')))
    $trimmed = [byte[]]::new($full.Length - 192000)
    [Array]::Copy($full, 192000, $trimmed, 0, $trimmed.Length)
    [IO.File]::WriteAllBytes($expected, $trimmed)
    Check ((Run-Player @('--decode-seek-test', $inputFile, $actual, '1')) -eq 0) "seek decode exit: $id"
    Check ((Get-FileHash $actual).Hash -eq (Get-FileHash $expected).Hash) "sample-exact seek to one second: $id"
}
[IO.File]::WriteAllText($broken, 'This is deliberately not an audio file.')
Check ((Run-Player @('--decode-test', $broken, (Join-Path $work 'broken.pcm'))) -ne 0) 'invalid audio rejected'
$report = Join-Path $work 'ui-smoke.txt'
$screenshot = Join-Path $work 'player.png'
Remove-Item $report -ErrorAction SilentlyContinue
$exitCode = Run-Player @('--music', $music, '--smoke-test', $report, '--screenshot', $screenshot)
Check ($exitCode -eq 0) 'UI playback smoke test exit'
Check (Test-Path $report) 'UI report exists'
foreach ($line in Get-Content $report) { Check ($line.StartsWith('PASS ')) $line.Substring(5) }
Check (Test-Path $screenshot) 'UI screenshot exists'
$script:checks | Set-Content -Encoding UTF8 (Join-Path $work 'results.txt')
Write-Host "All tests passed. Results and screenshot: $work"
