# Writes gen\assets.h with the controller pictures as byte arrays (kPng_pad_xbox, kPng_pad_ps).
#   powershell -ExecutionPolicy Bypass -File embed_pngs.ps1 <graphics folder> <gen folder>
param([string]$src = ".", [string]$gen = "..\gen")
$sb = New-Object Text.StringBuilder
[void]$sb.Append("// made by graphics/embed_pngs.ps1 (PNG files as bytes)`r`n")
foreach ($n in 'pad_xbox', 'pad_ps')
{
    $b = [IO.File]::ReadAllBytes((Resolve-Path (Join-Path $src "$n.png")))
    [void]$sb.Append("static const unsigned char kPng_$n[$($b.Length)] = {`r`n")
    for ($i = 0; $i -lt $b.Length; $i += 24)
    {
        $end = [Math]::Min($i + 24, $b.Length)
        [void]$sb.Append((($b[$i..($end - 1)] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ','))
        if ($end -lt $b.Length) { [void]$sb.Append(',') }
        [void]$sb.Append("`r`n")
    }
    [void]$sb.Append("};`r`n")
}
[IO.File]::WriteAllText((Join-Path (Resolve-Path $gen) "assets.h"), $sb.ToString())
"assets.h written"
