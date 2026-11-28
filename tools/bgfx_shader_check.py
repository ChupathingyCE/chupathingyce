"""The bgfx shader check: every shader the bgfx renderer can be asked for,
compiled for each backend the build has, failing on a shader that doesn't
compile (game issue #1833).

    python3 tools/bgfx_shader_check.py --binary build/linux64/chupathingy --out /tmp/shaders

bgfx's shaders are made when the game runs, from the translators' GLSL
(port/linux/src/d3d8_bgfx.inl), so a build compiles none of them. This runs
the build with HALO_BGFX_SHADER_CHECK (port/linux/src/d3d8_bgfx_check.inl),
which compiles the port's own shaders, each of the game's vertex programs
and the pixel shader of every key in the stock maps' list, through the very
functions a draw's shaders go through, then stops the game before it makes a
window:

- Vulkan (Linux, FreeBSD, Windows): glslang's SPIR-V in bgfx's container,
  each SPIR-V then checked by spirv-val when it is on the PATH (or
  --spirv-val names it);
- Direct3D 11 (Windows): SPIRV-Cross's HLSL compiled by d3dcompiler_47.dll;
- Metal (macOS): SPIRV-Cross's Metal source compiled by the Metal device.

It needs no maps and no GPU but a Mac's Metal device. The run has its own
home, save and data folders and is off the network, and runs a copy of the
build, so nothing it writes ends up beside the build (#2029). The report and each
failing shader's GLSL and error are in --out (report.txt, <backend>/).
Exits 1 if any shader fails.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def game_copy(binary, scratch):
	"""a copy of the build (the folder binary is in, or the macOS application
	it is in) in scratch, and the copy's binary. The game writes config.toml
	beside itself on Windows and Linux: run in the dist/ folder, it left the
	settings template in the release's zips (#2029)"""
	bundle = next((parent for parent in binary.parents if parent.suffix == ".app"), None)
	top = bundle or binary.parent
	copy = scratch / "game" / top.name
	shutil.copytree(top, copy, symlinks=True)
	return copy / binary.relative_to(top)


def run_check(binary, out, timeout):
	"""the build's check into out: its exit status and output"""
	scratch = Path(tempfile.mkdtemp(prefix="bgfx-shader-check-"))
	try:
		for name in ("home", "saves", "data"):
			(scratch / name).mkdir()
		binary = game_copy(binary, scratch)
		environment = {
			"PATH": os.environ.get("PATH", "/usr/bin:/bin"),
			"HOME": str(scratch / "home"),
			"HALO_SAVE_ROOT": str(scratch / "saves"),
			"HALO_DATA_ROOT": str(scratch / "data"),
			"HALO_UPDATE_AUTO": "false",
			"HALO_DISCORD_APPLICATION": "",
			"HALO_CRASH_REPORTS": "no",
			"HALO_NET_ONLINE": "false",
			"HALO_NET_LIST_GAMES": "false",
			"HALO_NET_REPORT_GAMES": "false",
			"HALO_NET_BROWSER": "",
			"HALO_STEAM_DISCOVERY": "false",
			"SDL_AUDIO_DRIVER": "dummy",
			"SDL_VIDEO_DRIVER": "dummy",
			"HALO_BGFX_SHADER_CHECK": str(out),
		}
		for name in ("LD_LIBRARY_PATH", "SYSTEMROOT", "TEMP", "TMP", "WINDIR"):
			if name in os.environ:
				environment[name] = os.environ[name]
		result = subprocess.run([str(binary)], env=environment, cwd=str(scratch), capture_output=True, text=True,
		                        errors="replace", timeout=timeout)
		return result.returncode, result.stdout + result.stderr
	finally:
		shutil.rmtree(scratch, ignore_errors=True)


def spirv_validate(tool, folder):
	"""spirv-val over each SPIR-V in folder: the failures' lines"""
	failures = []
	for path in sorted(folder.glob("*.spv")):
		result = subprocess.run([tool, "--target-env", "vulkan1.0", str(path)], capture_output=True, text=True)
		if result.returncode != 0:
			failures.append(f"vulkan: {path.stem} FAILED spirv-val: {(result.stdout + result.stderr).strip()}")
	return failures


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--binary", required=True, help="the build to check (its chupathingy executable)")
	parser.add_argument("--out", required=True, help="where the report and the failing shaders go")
	parser.add_argument("--spirv-val", help="spirv-val to check Vulkan's SPIR-V with (default: the PATH's, if any)")
	parser.add_argument("--timeout", type=float, default=900, help="seconds the check may take")
	options = parser.parse_args()

	out = Path(options.out).resolve()
	if out.exists():
		shutil.rmtree(out)
	out.mkdir(parents=True)
	status, output = run_check(Path(options.binary).resolve(), out, options.timeout)
	report = out / "report.txt"
	if not report.is_file():
		print(output[-4000:])
		print(f"bgfx shader check: the build wrote no report (exit {status}): is it a build with bgfx, "
		      "from after HALO_BGFX_SHADER_CHECK was added?")
		return 1
	lines = report.read_text(errors="replace").splitlines()
	failures = [line for line in lines if "FAILED" in line]
	tool = options.spirv_val or shutil.which("spirv-val")
	vulkan = out / "vulkan"
	if vulkan.is_dir():
		if tool:
			spirv = spirv_validate(tool, vulkan)
			failures += spirv
			lines.append(f"spirv-val: {len(list(vulkan.glob('*.spv')))} checked, {len(spirv)} failed")
		else:
			lines.append("spirv-val: not on the PATH, the SPIR-V is not validated")
	for line in lines:
		if "FAILED" not in line:
			print(line)
	for line in failures:
		print(line)
	if status != 0 and not failures:
		print(output[-4000:])
		failures.append(f"the check stopped with {status}")
	print("bgfx shader check:", "FAIL" if failures else "PASS")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
