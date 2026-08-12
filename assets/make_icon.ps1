# kite.ico を生成する。
#
# アイコンの正はこのスクリプト。.ico はここからの生成物なので、
# デザインを変えるときは .ico を直接編集せず、ここを直して再実行すること。
#
#   pwsh -File assets\make_icon.ps1
#
# デザイン: 青紫グラデーションの角丸正方形に、白い「欠けた菱形」。
#
# 姉妹プロジェクト Blinker（画像ビューア）の「欠けたリング」と同じ語彙で描いてある。
# 余白・角丸・線の作り(白一色、丸い端、閉じない一筆)はすべて同じ値で、違うのは
# 形と色だけ ─ 円か菱形か、水色か青紫か。並んで置かれたときに同じ作者のものだと
# 分かり、かつ取り違えないための距離。
#
# **色を Blinker に寄せないこと。** 形だけを変えて水色のままにした版を作ったが、
# 16px では輪郭が潰れて色しか残らず、タスクバーで見分けが付かなかった。色相を離し、
# ついでに明度も一段落としてある。
#
# 絵にしない。凧の骨も尾も描かない ─ 16px まで落ちれば細部は必ず潰れるし、
# 潰れた結果が「何かの塊」になるくらいなら、最初から輪郭だけのほうが強い。

[CmdletBinding()]
param(
  [string]$OutPath = (Join-Path $PSScriptRoot "kite.ico"),
  # 目で確かめるための 256px PNG。既定では書き出さない
  [string]$PreviewPath = "",
  # 各サイズは 4 倍で描いてから縮小する(小サイズの輪郭を滑らかにするため)
  [int]$Supersample = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

# Windows のシェルが使うサイズ一式
$sizes = @(16, 20, 24, 32, 48, 64, 128, 256)

# --- デザイン定数(256px 基準。他サイズへは比例させる) ---
$BASE        = 256.0
$COLOR_TOP   = "#8FA0FF"   # 左上
$COLOR_BOT   = "#3B3FBF"   # 右下
$BG_MARGIN   = 4.0         # 角丸正方形の余白
$BG_RADIUS   = 56.0

# 菱形は上下非対称。上の頂点までが短く、下の頂点までが長い ─ 左右対称の菱形は
# ただの回転した正方形に見えるが、この比を崩すだけで凧として読める。
$KITE_CX     = 128.0       # 中心 X
$KITE_TOP    = 52.0        # 上の頂点
$KITE_BOT    = 204.0       # 下の頂点
$KITE_BAR    = 112.0       # 左右の頂点の高さ(中心より上に置く)
$KITE_HALF   = 58.0        # 左右の頂点までの距離
$KITE_WIDTH  = 28.0        # 線幅
$KITE_GAP    = 0.5         # 左下の辺のうち欠けさせる割合。0 なら閉じた菱形

function New-RoundedRectPath([double]$x, [double]$y, [double]$w, [double]$h, [double]$r) {
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = $r * 2
  $path.AddArc($x,          $y,          $d, $d, 180, 90)
  $path.AddArc($x + $w - $d, $y,          $d, $d, 270, 90)
  $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d,   0, 90)
  $path.AddArc($x,          $y + $h - $d, $d, $d,  90, 90)
  $path.CloseFigure()
  $path
}

# 一辺 $size のアイコン画像を描く
function New-IconBitmap([int]$size) {
  $k = $size / $BASE
  $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $pt = { param($x, $y) New-Object System.Drawing.PointF([single]($x * $k), [single]($y * $k)) }
  try {
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
      (New-Object System.Drawing.PointF(0, 0)),
      (New-Object System.Drawing.PointF([single]$size, [single]$size)),
      [System.Drawing.ColorTranslator]::FromHtml($COLOR_TOP),
      [System.Drawing.ColorTranslator]::FromHtml($COLOR_BOT))
    $bg = New-RoundedRectPath ($BG_MARGIN * $k) ($BG_MARGIN * $k) `
                              (($BASE - $BG_MARGIN * 2) * $k) (($BASE - $BG_MARGIN * 2) * $k) `
                              ($BG_RADIUS * $k)
    $g.FillPath($brush, $bg)
    $bg.Dispose(); $brush.Dispose()

    # 左の頂点から時計回りに一筆。下の頂点まで来たら左の頂点へ戻る途中で止める。
    # 閉じずに残した分が「欠け」になる(閉じた菱形にすると、ただの図形記号になる)。
    # 注: PowerShell のカンマは算術より強く結合する。$a - $b, $c は
    # $a - ($b, $c) になって「配列は引けない」と言われるので、座標は 1 つずつ持つ
    $leftX  = $KITE_CX - $KITE_HALF
    $rightX = $KITE_CX + $KITE_HALF
    $startX = $leftX + ($KITE_CX - $leftX) * $KITE_GAP
    $startY = $KITE_BAR + ($KITE_TOP - $KITE_BAR) * $KITE_GAP

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddLine((& $pt $startX $startY), (& $pt $KITE_CX $KITE_TOP))
    $path.AddLine((& $pt $KITE_CX $KITE_TOP), (& $pt $rightX $KITE_BAR))
    $path.AddLine((& $pt $rightX $KITE_BAR), (& $pt $KITE_CX $KITE_BOT))
    $path.AddLine((& $pt $KITE_CX $KITE_BOT), (& $pt $leftX $KITE_BAR))

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, [single]($KITE_WIDTH * $k))
    $pen.StartCap  = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap    = [System.Drawing.Drawing2D.LineCap]::Round
    # 頂点を尖らせない。小さいサイズでは尖りが 1 ピクセルのゴミになって、
    # 輪郭がぶれて見える
    $pen.LineJoin  = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($pen, $path)
    $pen.Dispose(); $path.Dispose()
  } finally {
    $g.Dispose()
  }
  $bmp
}

# 4 倍で描いてから縮小した $size ピクセルの画像を返す
function New-IconBitmapSmooth([int]$size) {
  if ($Supersample -le 1) { return (New-IconBitmap $size) }
  $big = New-IconBitmap ($size * $Supersample)
  try {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
      $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
      $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
      $g.CompositingMode   = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
      $g.DrawImage($big, (New-Object System.Drawing.Rectangle(0, 0, $size, $size)))
    } finally {
      $g.Dispose()
    }
    return $bmp
  } finally {
    $big.Dispose()
  }
}

# 注: PowerShell は byte[] をパイプラインで 1 要素ずつに展開してしまうため、
# 戻り値も入れ物も [byte[]] を明示して保持すること
function Get-PngBytes([System.Drawing.Bitmap]$bmp) {
  $ms = New-Object System.IO.MemoryStream
  try {
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    [byte[]]$ms.ToArray()
  } finally {
    $ms.Dispose()
  }
}

# --- 各サイズを PNG にする ---
$pngs = New-Object 'System.Collections.Generic.List[byte[]]'
foreach ($size in $sizes) {
  $bmp = New-IconBitmapSmooth $size
  try {
    $pngs.Add([byte[]](Get-PngBytes $bmp))
    if ($PreviewPath -and $size -eq 256) { $bmp.Save($PreviewPath, [System.Drawing.Imaging.ImageFormat]::Png) }
  } finally { $bmp.Dispose() }
}

# --- ICO を組み立てる(全エントリ PNG 圧縮。Vista 以降が読める) ---
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($out)
try {
  $w.Write([uint16]0)             # reserved
  $w.Write([uint16]1)             # type: 1 = icon
  $w.Write([uint16]$sizes.Count)

  # 画像データは全エントリのディレクトリの直後から並べる
  $offset = 6 + 16 * $sizes.Count
  for ($i = 0; $i -lt $sizes.Count; $i++) {
    $size = $sizes[$i]
    $w.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))  # 256 は 0 で表す
    $w.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
    $w.Write([byte]0)             # パレット色数(トゥルーカラーは 0)
    $w.Write([byte]0)             # reserved
    $w.Write([uint16]1)           # プレーン数
    $w.Write([uint16]32)          # bpp
    $w.Write([uint32]$pngs[$i].Length)
    $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
  }
  foreach ($png in $pngs) { $w.Write([byte[]]$png, 0, $png.Length) }
  $w.Flush()

  [System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
} finally {
  $w.Dispose()
  $out.Dispose()
}

$total = (Get-Item $OutPath).Length
Write-Host ("{0} ({1} sizes, {2} bytes)" -f $OutPath, $sizes.Count, $total)
