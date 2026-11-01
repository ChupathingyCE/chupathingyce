# The Visual Studio build's steps before it compiles (projects/windows/halo.vcxproj),
# with what Windows has: no Python.
#
# 1. Embeds the high-res HUD textures, the menus' titles, the fonts and the
#    menus' files (port/assets) in the game as C data, as
#    tools/embed_assets.py does for the ninja builds: writes -Output when it
#    is older than the assets.
# 2. Embeds the overlay's fonts (port/linux/ui/fonts: the game browser's) as
#    C data, as tools/embed_assets.py --fonts does: writes -FontsOutput when
#    it is older than the fonts.
# 3. Writes the version (the VERSION file's, a build of one's own:
#    tools/version.py) to -VersionOutput, for port/linux/src/updater.c.
# 4. Downloads SDL3's Visual C++ development package (headers, and the
#    import library and DLL of each architecture) to -SdlDir once, and checks
#    the download against -SdlSha256, as tools/windows_build.py does.
#
# Windows PowerShell 5.1 runs it (and PowerShell 7).

param(
	[Parameter(Mandatory = $true)][string]$Root,
	[Parameter(Mandatory = $true)][string]$Output,
	[string]$FontsOutput = "",
	[string]$VersionOutput = "",
	[string]$SdlDir = "",
	[string]$SdlVersion = "3.4.16",
	[string]$SdlSha256 = "",
	[string]$SdlArch = "x86"
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Join-Parts([string]$base, [string]$relative)
{
	return [System.IO.Path]::Combine($base, $relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
}

# data as lines of 32-bit little-endian words (padded with zeros)
function Add-Words([System.Text.StringBuilder]$text, [byte[]]$data)
{
	$length = $data.Length
	$padded = $length + ((4 - ($length % 4)) % 4)
	if ($padded -ne $length)
	{
		$copy = New-Object byte[] $padded
		[System.Array]::Copy($data, $copy, $length)
		$data = $copy
	}
	$column = 0
	for ($offset = 0; $offset -lt $padded; $offset += 4)
	{
		if ($column -eq 0)
		{
			[void]$text.Append("`t")
		}
		else
		{
			[void]$text.Append(' ')
		}
		[void]$text.Append('0x').Append([System.BitConverter]::ToUInt32($data, $offset).ToString('x8')).Append(',')
		$column++
		if ($column -eq 8)
		{
			[void]$text.Append("`n")
			$column = 0
		}
	}
	if ($column -ne 0)
	{
		[void]$text.Append("`n")
	}
}

function Read-BigEndian([byte[]]$data, [int]$offset)
{
	return ([int]$data[$offset] -shl 24) -bor ([int]$data[$offset + 1] -shl 16) -bor ([int]$data[$offset + 2] -shl 8) -bor [int]$data[$offset + 3]
}

function Embed-Assets
{
	$listings = @(
		@{ Folder = 'port/assets/hud'; List = 'port/assets/hud/layout.json'; Title = 0 },
		@{ Folder = 'port/assets/titles'; List = 'port/assets/titles/titles.json'; Title = 1 }
	)
	$fontFolder = 'port/assets/fonts'
	$fontList = Join-Parts $Root 'port/assets/fonts/fonts.json'

	# the textures: each one's file, its entry in its list, and whether it is a title
	$textures = New-Object System.Collections.ArrayList
	$inputs = New-Object System.Collections.ArrayList
	[void]$inputs.Add($PSCommandPath)
	foreach ($listing in $listings)
	{
		$list = Join-Parts $Root $listing.List
		if (-not (Test-Path -LiteralPath $list))
		{
			continue
		}
		[void]$inputs.Add($list)
		$assets = (Get-Content -LiteralPath $list -Raw | ConvertFrom-Json).assets
		foreach ($asset in $assets)
		{
			$file = Join-Parts (Join-Parts $Root $listing.Folder) ($asset.name + '.png')
			[void]$inputs.Add($file)
			[void]$textures.Add(@{ File = $file; Asset = $asset; Title = $listing.Title })
		}
	}
	$fonts = @()
	$fontFiles = @()
	if (Test-Path -LiteralPath $fontList)
	{
		[void]$inputs.Add($fontList)
		$fonts = @((Get-Content -LiteralPath $fontList -Raw | ConvertFrom-Json).fonts)
		$fontFiles = @($fonts | ForEach-Object { $_.file } | Sort-Object -Unique)
		foreach ($name in $fontFiles)
		{
			[void]$inputs.Add((Join-Parts (Join-Parts $Root $fontFolder) $name))
		}
	}

	# the menus' files (port/assets/menus/menus.json lists them)
	$menuFolder = 'port/assets/menus'
	$menuList = Join-Parts $Root 'port/assets/menus/menus.json'
	$menuFiles = @()
	if (Test-Path -LiteralPath $menuList)
	{
		[void]$inputs.Add($menuList)
		$menuFiles = @((Get-Content -LiteralPath $menuList -Raw | ConvertFrom-Json).files)
		foreach ($name in $menuFiles)
		{
			[void]$inputs.Add((Join-Parts (Join-Parts $Root $menuFolder) $name))
		}
	}

	# SMAA's shader and lookup textures (port/third_party/smaa), and the names
	# port/linux/src/xgpu_post.c declares them by (tools/embed_assets.py's
	# SMAA_FILES)
	$smaaFolder = 'port/third_party/smaa'
	$smaaFiles = @(@('SMAA.hlsl', 'xgpu_smaa_shader'), @('area_tex.zlib', 'xgpu_smaa_area_texture'),
		@('search_tex.zlib', 'xgpu_smaa_search_texture'))
	foreach ($smaa in $smaaFiles)
	{
		$path = Join-Parts (Join-Parts $Root $smaaFolder) $smaa[0]
		if (Test-Path -LiteralPath $path)
		{
			[void]$inputs.Add($path)
		}
	}

	# the head-related impulse responses (port/assets/hrtf)
	$hrtfFile = Join-Parts $Root 'port/assets/hrtf/sadie_d2_48k.hrtf'
	if (Test-Path -LiteralPath $hrtfFile)
	{
		[void]$inputs.Add($hrtfFile)
	}

	# nothing to do when the output is newer than everything it is made from
	if (Test-Path -LiteralPath $Output)
	{
		$written = (Get-Item -LiteralPath $Output).LastWriteTimeUtc
		$stale = $false
		foreach ($path in $inputs)
		{
			if ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $written)
			{
				$stale = $true
				break
			}
		}
		if (-not $stale)
		{
			return
		}
	}
	Write-Host "Embedding the high-res HUD, titles, fonts and menus: $Output"

	$text = New-Object System.Text.StringBuilder (16 * 1024 * 1024)
	[void]$text.Append("/* generated by port/windows/tools/prebuild.ps1 from port/assets: do not edit */`n`n")
	[void]$text.Append("#include `"hud_hires.h`"`n`n")
	$table = New-Object System.Text.StringBuilder
	$index = 0
	foreach ($texture in $textures)
	{
		$asset = $texture.Asset
		$name = $asset.name + '.png'
		$data = [System.IO.File]::ReadAllBytes($texture.File)
		# an 8-bit RGBA, non-interlaced PNG (the only kind hud_hires.c reads)
		if ($data.Length -lt 29 -or $data[0] -ne 0x89 -or $data[1] -ne 0x50 -or $data[2] -ne 0x4E -or $data[3] -ne 0x47 -or
			$data[12] -ne 0x49 -or $data[13] -ne 0x48 -or $data[14] -ne 0x44 -or $data[15] -ne 0x52)
		{
			throw "${name}: not a PNG"
		}
		$width = Read-BigEndian $data 16
		$height = Read-BigEndian $data 20
		if ($data[24] -ne 8 -or $data[25] -ne 6 -or $data[28] -ne 0)
		{
			throw "${name}: must be 8-bit RGBA and not interlaced"
		}
		$scale = [int]$asset.scale
		if ($width -ne ([int]$asset.width * $scale) -or $height -ne ([int]$asset.height * $scale))
		{
			throw "${name}: ${width}x${height}, not ${scale}x its bitmap's $($asset.width)x$($asset.height)"
		}
		[void]$text.Append("static const unsigned int asset$index[] = {`n")
		Add-Words $text $data
		[void]$text.Append("};`n`n")
		$tag = ([string]$asset.tag).Replace('\', '\\')
		$coverage = 0
		$pointThreshold = 0
		if ($asset.PSObject.Properties['cells'])
		{
			foreach ($cell in $asset.cells)
			{
				if ($cell.kind -eq 'meter')
				{
					$coverage = 1
				}
				if ($cell.PSObject.Properties['thresholds'] -and $cell.thresholds)
				{
					$pointThreshold = 1
				}
			}
		}
		$crc = ([uint32]$asset.crc).ToString('x8')
		[void]$table.Append("`t{ `"$tag`", $([int]$asset.bitmap), $width, $height, 0x${crc}u, $coverage, $pointThreshold, $($texture.Title), asset$index, $($data.Length) },`n")
		$index++
	}
	[void]$text.Append("const struct hud_hires_embedded hud_hires_embedded[] =`n{`n")
	[void]$text.Append($table.ToString())
	[void]$text.Append("};`n")
	[void]$text.Append("const unsigned int hud_hires_embedded_count = $index;`n`n")

	# the fonts, and which draws each font tag (text_hires.h)
	[void]$text.Append("#include `"text_hires.h`"`n`n")
	$sizes = @{}
	for ($font = 0; $font -lt $fontFiles.Count; $font++)
	{
		$data = [System.IO.File]::ReadAllBytes((Join-Parts (Join-Parts $Root $fontFolder) $fontFiles[$font]))
		$sizes[$fontFiles[$font]] = $data.Length
		[void]$text.Append("static const unsigned int font$font[] = {`n")
		Add-Words $text $data
		[void]$text.Append("};`n`n")
	}
	[void]$text.Append("const struct text_hires_embedded text_hires_embedded[] =`n{`n")
	foreach ($font in $fonts)
	{
		$fontIndex = [System.Array]::IndexOf($fontFiles, [string]$font.file)
		$tag = ([string]$font.tag).Replace('\', '\\')
		# (its role, "large" or "small", and how wide it is drawn: a number as
		# Python writes it, 1.0 and not 1)
		$role = 'small'
		if ($font.PSObject.Properties['role'] -and $font.role)
		{
			$role = [string]$font.role
		}
		$fontWidth = 1.0
		if ($font.PSObject.Properties['width'])
		{
			$fontWidth = [double]$font.width
		}
		$widthText = $fontWidth.ToString('R', [System.Globalization.CultureInfo]::InvariantCulture)
		if ($widthText -notmatch '[.eE]')
		{
			$widthText += '.0'
		}
		[void]$text.Append("`t{ `"$tag`", `"$($font.file)`", font$fontIndex, $($sizes[[string]$font.file]), `"$role`", ${widthText}f },`n")
	}
	if ($fonts.Count -eq 0)
	{
		[void]$text.Append("`t{ 0 },`n")
	}
	[void]$text.Append("};`n")
	[void]$text.Append("const unsigned int text_hires_embedded_count = $($fonts.Count);`n`n")

	# the menus' files (menu_files.h)
	[void]$text.Append("#include `"menu_files.h`"`n`n")
	$menuSizes = New-Object System.Collections.ArrayList
	for ($menu = 0; $menu -lt $menuFiles.Count; $menu++)
	{
		$name = [string]$menuFiles[$menu]
		$data = [System.IO.File]::ReadAllBytes((Join-Parts (Join-Parts $Root $menuFolder) $name))
		if ($name.EndsWith('.png'))
		{
			if ($data.Length -lt 29 -or $data[0] -ne 0x89 -or $data[1] -ne 0x50 -or $data[2] -ne 0x4E -or $data[3] -ne 0x47 -or
				$data[12] -ne 0x49 -or $data[13] -ne 0x48 -or $data[14] -ne 0x44 -or $data[15] -ne 0x52)
			{
				throw "${name}: not a PNG"
			}
			if ($data[24] -ne 8 -or $data[25] -ne 6 -or $data[28] -ne 0)
			{
				throw "${name}: must be 8-bit RGBA and not interlaced"
			}
		}
		[void]$menuSizes.Add($data.Length)
		[void]$text.Append("static const unsigned int menu$menu[] = {`n")
		Add-Words $text $data
		[void]$text.Append("};`n`n")
	}
	[void]$text.Append("const struct menu_file_embedded menu_files_embedded[] =`n{`n")
	for ($menu = 0; $menu -lt $menuFiles.Count; $menu++)
	{
		[void]$text.Append("`t{ `"$($menuFiles[$menu])`", menu$menu, $($menuSizes[$menu]) },`n")
	}
	if ($menuFiles.Count -eq 0)
	{
		[void]$text.Append("`t{ 0 },`n")
	}
	[void]$text.Append("};`n")
	[void]$text.Append("const unsigned int menu_files_embedded_count = $($menuFiles.Count);`n`n")

	# SMAA's shader, as text a GLSL compiler takes (ASCII, ending in a NUL),
	# and its lookup textures (xgpu_post.c); each of size 0 that the checkout
	# does not have. Android has no SMAA.
	[void]$text.Append("#ifndef HALO_ANDROID`n")
	foreach ($smaa in $smaaFiles)
	{
		$path = Join-Parts (Join-Parts $Root $smaaFolder) $smaa[0]
		$data = New-Object byte[] 0
		if (Test-Path -LiteralPath $path)
		{
			$data = [System.IO.File]::ReadAllBytes($path)
			if ($smaa[0].EndsWith('.hlsl'))
			{
				$ascii = New-Object byte[] ($data.Length + 1)
				for ($at = 0; $at -lt $data.Length; $at++)
				{
					$ascii[$at] = if ($data[$at] -lt 0x80) { $data[$at] } else { 0x20 }
				}
				$data = $ascii
			}
		}
		[void]$text.Append("`nconst unsigned int $($smaa[1])[] = {`n")
		if ($data.Length -gt 0)
		{
			Add-Words $text $data
		}
		else
		{
			[void]$text.Append("`t0,`n")
		}
		[void]$text.Append("};`n")
		[void]$text.Append("const unsigned long $($smaa[1])_size = $($data.Length);`n")
	}
	[void]$text.Append("`n#endif`n")

	# the mixer's head-related impulse responses (dsound_sdl.c), of size 0 if
	# the checkout does not have them (tools/embed_assets.py's HRTF_FILE)
	$data = New-Object byte[] 0
	if (Test-Path -LiteralPath $hrtfFile)
	{
		$data = [System.IO.File]::ReadAllBytes($hrtfFile)
	}
	[void]$text.Append("`nconst unsigned int dsound_hrtf_data[] = {`n")
	if ($data.Length -gt 0)
	{
		Add-Words $text $data
	}
	else
	{
		[void]$text.Append("`t0,`n")
	}
	[void]$text.Append("};`n")
	[void]$text.Append("const unsigned long dsound_hrtf_data_size = $($data.Length);`n")

	$folder = [System.IO.Path]::GetDirectoryName($Output)
	if ($folder -and -not (Test-Path -LiteralPath $folder))
	{
		[void](New-Item -ItemType Directory -Force -Path $folder)
	}
	[System.IO.File]::WriteAllText($Output, $text.ToString(), (New-Object System.Text.UTF8Encoding($false)))
}

# the overlay's fonts, in posix_ui_font.c's order, from the repository's root
# (tools/embed_assets.py's UI_FONT_FILES: tools/visual_studio.py checks that
# the two agree)
$uiFontFiles = @(
	'port/linux/ui/fonts/NotoSans-Regular.ttf',
	'port/linux/ui/fonts/NotoSans-Bold.ttf',
	'port/linux/ui/fonts/input_xbox.ttf',
	'port/linux/ui/fonts/input_playstation.ttf',
	'port/linux/ui/fonts/input_nintendo.ttf',
	'port/linux/ui/fonts/input_keyboard.ttf',
	'port/assets/fonts/CyborgInterfaceGothic-Regular.ttf',
	'port/assets/fonts/CyborgInterfaceGothic-Regular.ttf',
	'port/assets/fonts/OpenCE-Regular.ttf',
	'port/linux/ui/fonts/platform_icons.ttf'
)
# the controller's button icons the overlay's prompts draw, in ui_overlay.c's
# order (tools/embed_assets.py's PROMPT_ICONS, checked the same way)
$promptIcons = @(
	'port/assets/prompts/button-a.png',
	'port/assets/prompts/button-b.png',
	'port/assets/prompts/button-x.png',
	'port/assets/prompts/button-y.png',
	'port/assets/prompts/button-start.png',
	'port/assets/prompts/trigger-left.png',
	'port/assets/prompts/trigger-right.png',
	'port/assets/prompts/bumper-left.png',
	'port/assets/prompts/bumper-right.png',
	'port/assets/prompts/button-back.png'
)

# an 8-bit RGBA, non-interlaced PNG's pixels (rows from the top, 4 bytes a
# pixel), its filters undone, as tools/embed_assets.py's png_rgba does
Add-Type -Language CSharp -TypeDefinition @"
public static class HaloPng
{
	public static byte[] Rgba(byte[] data, out int width, out int height)
	{
		width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
		height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
		if (data[24] != 8 || data[25] != 6 || data[28] != 0)
			throw new System.Exception("not an 8-bit RGBA, non-interlaced PNG");
		System.IO.MemoryStream compressed = new System.IO.MemoryStream();
		int offset = 8;
		while (offset < data.Length)
		{
			int length = (data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) | data[offset + 3];
			if (data[offset + 4] == 'I' && data[offset + 5] == 'D' && data[offset + 6] == 'A' && data[offset + 7] == 'T')
				compressed.Write(data, offset + 8, length);
			offset += 12 + length;
		}
		/* (the zlib stream's two bytes of header before its deflate data) */
		compressed.Position = 2;
		System.IO.MemoryStream raw = new System.IO.MemoryStream();
		using (System.IO.Compression.DeflateStream inflate = new System.IO.Compression.DeflateStream(compressed, System.IO.Compression.CompressionMode.Decompress))
			inflate.CopyTo(raw);
		byte[] bytes = raw.ToArray();
		int stride = width * 4;
		byte[] pixels = new byte[stride * height];
		byte[] previous = new byte[stride];
		for (int row = 0; row < height; row++)
		{
			int kind = bytes[row * (stride + 1)];
			byte[] line = new byte[stride];
			System.Array.Copy(bytes, row * (stride + 1) + 1, line, 0, stride);
			for (int index = 0; index < stride; index++)
			{
				int left = index >= 4 ? line[index - 4] : 0;
				int up = previous[index];
				int corner = index >= 4 ? previous[index - 4] : 0;
				if (kind == 1)
					line[index] = (byte)(line[index] + left);
				else if (kind == 2)
					line[index] = (byte)(line[index] + up);
				else if (kind == 3)
					line[index] = (byte)(line[index] + (left + up) / 2);
				else if (kind == 4)
				{
					int guess = left + up - corner;
					int a = System.Math.Abs(guess - left), b = System.Math.Abs(guess - up), c = System.Math.Abs(guess - corner);
					line[index] = (byte)(line[index] + (a <= b && a <= c ? left : b <= c ? up : corner));
				}
			}
			System.Array.Copy(line, 0, pixels, row * stride, stride);
			previous = line;
		}
		return pixels;
	}
}
"@

function Write-IfChanged([string]$path, [string]$content)
{
	$folder = [System.IO.Path]::GetDirectoryName($path)
	if ($folder -and -not (Test-Path -LiteralPath $folder))
	{
		[void](New-Item -ItemType Directory -Force -Path $folder)
	}
	if ((Test-Path -LiteralPath $path) -and ([System.IO.File]::ReadAllText($path) -ceq $content))
	{
		return
	}
	[System.IO.File]::WriteAllText($path, $content, (New-Object System.Text.UTF8Encoding($false)))
}

function Embed-Fonts
{
	if (-not $FontsOutput)
	{
		return
	}
	$inputs = @($PSCommandPath) + @($uiFontFiles + $promptIcons | ForEach-Object { Join-Parts $Root $_ })
	if (Test-Path -LiteralPath $FontsOutput)
	{
		$written = (Get-Item -LiteralPath $FontsOutput).LastWriteTimeUtc
		$stale = $false
		foreach ($path in $inputs)
		{
			if ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $written)
			{
				$stale = $true
				break
			}
		}
		if (-not $stale)
		{
			return
		}
	}
	Write-Host "Embedding the overlay's fonts: $FontsOutput"

	$text = New-Object System.Text.StringBuilder (8 * 1024 * 1024)
	[void]$text.Append("/* generated by port/windows/tools/prebuild.ps1 from port/linux/ui/fonts and port/assets/fonts: do not edit */`n`n")
	for ($font = 0; $font -lt $uiFontFiles.Count; $font++)
	{
		$data = [System.IO.File]::ReadAllBytes((Join-Parts $Root $uiFontFiles[$font]))
		[void]$text.Append("static const unsigned int font$font[] = {`n")
		Add-Words $text $data
		[void]$text.Append("};`n`n")
	}
	[void]$text.Append("/* (posix_ui_font.c's: each font's data, as words) */`n")
	[void]$text.Append("const unsigned int *const ui_font_files[$($uiFontFiles.Count)] =`n{`n`t")
	$names = @()
	for ($font = 0; $font -lt $uiFontFiles.Count; $font++)
	{
		$names += "font$font"
	}
	[void]$text.Append(($names -join ', ')).Append(",`n};`n`n")
	# the prompts' icons, as their pixels, each square and of one size
	$size = 0
	for ($icon = 0; $icon -lt $promptIcons.Count; $icon++)
	{
		$width = 0
		$height = 0
		$pixels = [HaloPng]::Rgba([System.IO.File]::ReadAllBytes((Join-Parts $Root $promptIcons[$icon])), [ref]$width, [ref]$height)
		if ($width -ne $height -or ($size -and $width -ne $size))
		{
			throw "$($promptIcons[$icon]): the prompts' icons must be square and of one size"
		}
		$size = $width
		[void]$text.Append("static const unsigned int icon$icon[] = {`n")
		Add-Words $text $pixels
		[void]$text.Append("};`n`n")
	}
	[void]$text.Append("/* (ui_overlay.c's: the prompts' icons, each ui_prompt_icon_size square) */`n")
	[void]$text.Append("const int ui_prompt_icon_size = $size;`n")
	[void]$text.Append("const unsigned int *const ui_prompt_icons[$($promptIcons.Count)] =`n{`n`t")
	$names = @()
	for ($icon = 0; $icon -lt $promptIcons.Count; $icon++)
	{
		$names += "icon$icon"
	}
	[void]$text.Append(($names -join ', ')).Append(",`n};`n")

	$folder = [System.IO.Path]::GetDirectoryName($FontsOutput)
	if ($folder -and -not (Test-Path -LiteralPath $folder))
	{
		[void](New-Item -ItemType Directory -Force -Path $folder)
	}
	[System.IO.File]::WriteAllText($FontsOutput, $text.ToString(), (New-Object System.Text.UTF8Encoding($false)))
}

# the version: a build of one's own is <VERSION>-dev, and never looks for
# updates (tools/version.py; only a release's build does)
function Write-Version
{
	if (-not $VersionOutput)
	{
		return
	}
	$version = 'dev'
	$file = Join-Parts $Root 'VERSION'
	if (Test-Path -LiteralPath $file)
	{
		$version = ([System.IO.File]::ReadAllText($file)).Trim() + '-dev'
	}
	# the build's identity (port/linux/src/build_identity.c; tools/version.py,
	# identity_defines): a -dev build's channel, and the checkout's commit
	# (HALO_COMMIT, else git's, else unknown)
	$commit = $env:HALO_COMMIT
	if (-not $commit)
	{
		try
		{
			$commit = (& git -C $Root rev-parse --short=8 HEAD 2>$null | Out-String).Trim()
		}
		catch
		{
			$commit = ''
		}
	}
	if (-not $commit)
	{
		$commit = 'unknown'
	}
	# (and the executable's version information, port/windows/halo.rc, as
	# tools/version.py's resource_version gives it: 0.7.5c is 0,7,5,3)
	$numbers = '0,0,0,0'
	if ($version -match '^(\d+)\.(\d+)\.(\d+)([a-z]?)')
	{
		$letter = 0
		if ($Matches[4])
		{
			$letter = [int][char]$Matches[4] - [int][char]'a' + 1
		}
		$numbers = "$($Matches[1]),$($Matches[2]),$($Matches[3]),$letter"
	}
	$content = "/* generated by port/windows/tools/prebuild.ps1 from VERSION: do not edit */`n" +
		"#define HALO_VERSION `"$version`"`n" +
		"#define HALO_RELEASE_BUILD 0`n" +
		"#define HALO_CHANNEL `"dev`"`n" +
		"#define HALO_COMMIT `"$commit`"`n" +
		"#define HALO_RC_VERSION_NUMBERS $numbers`n" +
		"#define HALO_RC_VERSION_STRING `"$version`"`n"
	Write-IfChanged $VersionOutput $content
}

function Fetch-Sdl
{
	if (-not $SdlDir)
	{
		return
	}
	# (the library of the platform that is built: the x86 or the x64 one)
	$library = Join-Parts $SdlDir "lib/$SdlArch/SDL3.lib"
	if (Test-Path -LiteralPath $library)
	{
		return
	}
	$parent = [System.IO.Path]::GetDirectoryName($SdlDir.TrimEnd('\', '/'))
	if (-not (Test-Path -LiteralPath $parent))
	{
		[void](New-Item -ItemType Directory -Force -Path $parent)
	}
	$url = "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion/SDL3-devel-$SdlVersion-VC.zip"
	$archive = Join-Parts $parent "SDL3-devel-$SdlVersion-VC.zip"
	Write-Host "Downloading $url"
	[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor [System.Net.SecurityProtocolType]::Tls12
	Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
	# (the pinned hash of the package: tools/windows_build.py, SDL_SHA256)
	if ($SdlSha256)
	{
		$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($digest -ne $SdlSha256.ToLowerInvariant())
		{
			Remove-Item -LiteralPath $archive
			throw "${url}: SHA-256 $digest, not the pinned $SdlSha256"
		}
	}
	Expand-Archive -LiteralPath $archive -DestinationPath $parent -Force
	Remove-Item -LiteralPath $archive
	if (-not (Test-Path -LiteralPath $library))
	{
		throw "SDL3 $SdlVersion was downloaded, but $library is not there"
	}
}

Embed-Assets
Embed-Fonts
Write-Version
Fetch-Sdl
