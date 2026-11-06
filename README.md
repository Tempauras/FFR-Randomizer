# FFR Randomizer

A seeded randomizer for Final Fantasy Resonance on PC: a UE4SS C++ mod (`CppMods/FFRRandomizer`) with an in game panel built as a pak for `Content/Paks/~mods` (`Source/FFRSRandomizerUI`)

As of now, shuffle things mostly keeping them in their own lines.

## The Save

Each save keeps its own seed saved in it's own saved data, in event flags 1984 to 2047, which the game never (allegedly) uses. A save without one gets a random seed the first time it loads. Press F2 in game to open the panel: it shows the seed, takes a new one (1 to 8 hexadecimal digits) and holds the Options.

## Randomization

As of now, each randomization is separated. Shops randomize with other shops, boss with other boss etc. The only thing that this doesn't apply to is steal and items randomization being able to be made so they randomize together.


## Battle scaling

Two type of scaling:
- Replaced stat -> The new group just take the old group stat divided between themselves while trying to keep ratio if the number of units were different.
- Balanced -> Balanced scaling rescale the new monsters depending on stat calculated by the avg of monster of their level and trying to keep their 'threat level'. The threat level of a monster group is calculated by using the damage formula as well as the TTK of a unequipped and visionless party. On avg the monster will be slightly stronger than what the group should be though the payout will be higher to compensate.

The actual math can be found in `CppMods/FFRRandomizer/ScalingModel.cpp`.

## Releases

You can download the already built dll as well as UE4SS and the ~mods folder and the 3 .pak .ucas .utoc from [Release](https://github.com/Tempauras/FFR-Randomizer/releases).

## Building

There are two separate pieces. You only need to rebuild the one you changed.

| Piece | What it is | Installed to |
|---|---|---|
| DLL | the randomizer itself | `FFRS/Binaries/Win64/ue4ss/Mods/FFRRandomizer/dlls/main.dll` |
| Seed panel | the F2 panel (`FFRRandomizer.pak/.ucas/.utoc`) | `FFRS/Content/Paks/~mods` |

### DLL

Needs Visual Studio with the C++ desktop workload, CMake 3.22 or newer, and the Rust toolchain (RE-UE4SS builds a few helpers with it).

1. Get the RE-UE4SS submodule, once: `git submodule update --init --recursive`
2. Configure, once, from `CppMods`: `cmake -S . -B build -G "Visual Studio 18 2026" -A x64` (any recent Visual Studio generator works)
3. Build: `cmake --build build --config Game__Shipping__Win64 --target FFRRandomizer`
4. With the game closed, copy `CppMods/build/FFRRandomizer/Game__Shipping__Win64/FFRRandomizer.dll` to the game's `FFRS/Binaries/Win64/ue4ss/Mods/FFRRandomizer/dlls/main.dll`.

### Seed panel

Needs Unreal Engine 5.6.

- **The easy way:** run `Tools/BuildUI.bat` (double-click it). It builds the panel and installs it into the game, then waits for a key so you can read the result.
- **From a terminal:** `powershell -ExecutionPolicy Bypass -File Tools/BuildUI.ps1 -Install`. Leave out `-Install` to only build into `Dist/FFRRandomizer`.

The first run asks for your Unreal Engine 5.6 folder and your game folder, and remembers them in `Tools/BuildPaths.local.json` (you can also pass `-EngineDir` and `-GameDir`). The script builds the editor module, generates `WBP_SeedPanel` with the `BuildFFRRandomizerUI` commandlet, cooks and packs it into `Dist/FFRRandomizer`, and with `-Install` copies it to `Content/Paks/~mods`, removing an older copy from `Content/Paks/LogicMods`. Close the game first.

## Raison d'être

Made this for multiple reason, first to try to make a randomizer because I never tried to make one, second because I really enjoyed this game and this give it more replayability, and finally as a way to brush up on Cpp skills. The code is mostly human made, if that matters, but AI has been used in the workflow. AI has done most of the panel and UI generation as I did not want to do all the blueprints by myself, as well as the mob scaling/generating as I am not a game designer, and have never pretended to be.

## Thanks

Thanks to Kanaruu and his [FFR Modding Tools](https://www.nexusmods.com/finalfantasyresonance/mods/1) on Nexusmod for making FFR Modding Tools and predumping the game files, saved a lot of time

## Disclaimer

Final Fantasy Resonance and all related names, characters and assets belong to their respective rights holders. This is an unofficial fan project, not affiliated with or endorsed by them. It does not include any game files: you need your own copy of the game.

The mod is provided as is. It writes its seed and options into your save, so back up your saves before using it.