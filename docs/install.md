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
- `gta_damage`: `0` (default) the GTA car is damage-proof; `1` lets GTA damage it.
- `shm_name`: shared-memory name. Only change if linkpeek shows the data under another name.
