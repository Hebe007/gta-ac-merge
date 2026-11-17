# Install and run the GTA link (stage 1)

## Build the GTA script (once, and after each `git pull` that changes `gta/` or `common/`)
Needs Visual Studio 2022 or newer with **Desktop development with C++**, and the ScriptHookV SDK unzipped into
`Scripthook\` in this repo (any folder layout inside; it is git-ignored).

Open **Developer PowerShell for VS** (Start menu), then:

    cd C:\Users\Hebe007\gta-ac-merge
    cmake -S . -B build -A x64
    cmake --build build --config Release --target GtaAcLink

Result: `build\gta\Release\GtaAcLink.asi`. The configure step prints `ScriptHookV SDK: ...` when it finds the SDK.

## Install
1. **GTA:** copy `build\gta\Release\GtaAcLink.asi` and `gta\gta_link.ini` into the GTA V folder
   (next to `GTA5_Enhanced.exe` and `ScriptHookV.dll`).
2. **Assetto Corsa:** copy the folder `csp\GtaLink` to `<AC>\apps\lua\GtaLink`
   (so `<AC>\apps\lua\GtaLink\manifest.ini` exists).

## Run
1. Start AC on **gta_ls / downtown** (practice) through Content Manager, windowed.
2. In AC, open the **GTA Link** app from the app sidebar. It should say `publishing` and the update count should climb.
3. Optional check: `python tools\linkpeek.py` should print `magic OK` with the position changing while you drive.
4. Start GTA V story mode, then press **F8**. Your car appears where the AC car is; drive with the wheel as usual.
5. **F8** again (or getting out of the car) turns the link off. Reset the car in AC if you fall off the map.

## On-screen messages
- `waiting for Assetto Corsa`: AC stopped sending (closed, paused, loading). The GTA car is frozen; it resumes by itself.
- `Off the map - reset the car in Assetto Corsa`: the AC car is more than 5 m below GTA's ground.
- `the car was teleported`: something (e.g. the trainer) moved the GTA car; the link turned off. F8 to link again.

## Settings (`gta_link.ini`)
- `model`: the GTA car that represents yours (default `sultan`).
- `height_trim`: extra metres to raise (+) or lower (-) the GTA car. The height is matched automatically from both
  cars' wheels; use this only for fine-tuning.
- `frozen`: `1` (default) GTA physics never moves the car, so trees, poles and walls that only GTA has can't
  shove or flip it; wheels may not animate. `0` lets GTA physics act between updates.
- `steer_invert`: `1` if GTA's front wheels turn the opposite way to AC's.
- `steer_scale`: lower it (e.g. `0.5`) if GTA's wheels reach full lock long before yours; raise it if they turn too little.
- `steer_offset`: centre correction (`+` = left), e.g. `0.05` if GTA's wheels point slightly right when you drive straight.
- `engine_audio`: `voltic` (default) keeps the GTA engine running with an electric car's near-silent sound so
  the dashboard works; `off` turns the engine off (silent, dashboard dark).
- `pedals`: `1` (default) passes AC's throttle and brake to GTA (rev counter moves, brake lights); `0` = off.
- `gta_damage`: `0` (default) the GTA car is damage-proof; `1` lets GTA damage it.
- `shm_name`: shared-memory name. Only change if linkpeek shows the data under another name.

## Exporting the full map's collision (CodeWalker)
RPF Explorer: search `.ybn`, sort by **Path**. Select whole blocks (click first row, Shift+click last row), including
`hi@` and `ma@` files (the generator ignores `ma@` and uses `hi@` only where no normal file exists), and
**Export XML** each block into its own folder under `work\xml\`:

| Path starts with | Export into |
|---|---|
| `onigiri\...` (NaturalVision) | `work\xml\mods\` |
| `update\update.rpf\...` | `work\xml\update\` |
| `update\x64\dlcpacks\patchdayNNng\...` | `work\xml\patchdayNNng\` (one folder per patchday) |
| `update\x64\dlcpacks\<any other pack>\...` | `work\xml\dlc\` (all together) |
| `x64a.rpf` ... `x64w.rpf` | `work\xml\base\` |

The generator keeps the newest copy of each file: base < dlc < patchdays (by number) < update < mods.

## Generator: speed, memory and the cache
- Needs `pip install numpy scipy` once (the default `--engine bulk`; `--engine python` is the old slow path).
- The first run reads all XML and saves `work\surfacegen_cache_xml.npz`. Later runs with the same exported files
  reuse it ("using cache") and skip the slow reading, so changing materials, `--step-height`, walls etc. is quick.
  Re-exporting or changing files makes it read again automatically. `--no-cache` forces a fresh read.

## Interiors (tunnels etc.): exporting the map placement files
Tunnels and other interiors keep their collision in their own coordinates; the `.ymap` map files say where they go.
1. RPF Explorer: search `milo` (map files holding interior placements are usually named `..._milo_...`), keep the
   **Map Data** (`.ymap`) rows, and **Export XML** them into `work\ymap\<source>\` with the same source folders as the
   collision (`base`, `update`, `dlc`, `patchdayNNng`, `mods`).
2. Generate with `--ymap-dir ..\..\work\ymap`. The log says how many interiors it placed.
3. Drive through a tunnel to check. (The probe shows the raw cache, without placed interiors.)
