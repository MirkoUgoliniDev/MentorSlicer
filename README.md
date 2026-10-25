<p align="center">
  <img src="resources/images/MentorSlicer_192px.png" alt="MentorSlicer logo" width="128" height="128">
</p>

<h1 align="center">MentorSlicer</h1>

<p align="center"><b>A Creality Print fork with an AI print advisor, chamber preheat and multi-layer brim</b></p>

MentorSlicer is a fork of [Creality Print](https://github.com/CrealityOfficial/CrealityPrint) 7.3, an open source slicer for FDM printers. It adds the **Print Advisor**: a local service that an MCP client (Claude Code, Codex) uses to read the slicing context, diagnose defects, propose profile changes and control a LAN printer, with every change confirmed by the user inside the app. The LLM stays in the client; the app contains only the domain service.

Differences from Creality Print:
- no Creality account, cloud, analytics or update check: the app works offline and prints over the local network (tested with a Creality K1C);
- the Print Advisor (`src/slic3r/GUI/simple/printAdvisor/`, sidecar in `tools/print-advisor-mcp/`) and engine options such as heat soak and multi-layer brim;
- data folder `%APPDATA%\MentorSlicer` (`MentorSlicer.conf`). On its first start MentorSlicer copies the Creality Print 7.3 Alpha data folder once and leaves the original untouched;
- G-code and 3MF files stay compatible with Creality Print (same G-code header and 3MF application tag).

Updates are made by rebuilding the fork; there are no binary releases.

MentorSlicer is not affiliated with or endorsed by Creality.

# What's new in MentorSlicer

## 🔥 Chamber preheat
Printers like the K1C have a chamber temperature sensor but no chamber heater: the chamber only warms up from the hot bed. MentorSlicer can preheat the chamber **before** you send the print, so ASA and ABS start in a warm enclosure instead of a cold one.

- **One button on the Device page**: the "Temp camera" card has a **Preriscalda / Stop** button and an optional **°C** field for the chamber target (for example 35 °C). While it runs, the card shows the elapsed time and the target (`Preriscaldo 12:30 → 35°`).
- **From the AI assistant**: the Print Advisor tool `preheat_chamber` does the same, with a confirmation in the app.
- **Safe by design**: the bed is set to the printer maximum (at most 100 °C) and is **switched off automatically** when the target is reached, when the chamber stops rising (plateau), after `max_minutes` (5–120, default 60), or when the Print Advisor is stopped (if recent data shows the printer idle; otherwise you get a notification). If the printer goes offline during a preheat, the bed is switched off as soon as it is reachable and idle again. Only one preheat at a time, and it stops by itself when a print starts.
- **Preflight warning**: for ASA/ABS, the pre-print check warns when the chamber is still cold (below 30 °C) and suggests a preheat and a longer soak.

```mermaid
stateDiagram-v2
    direction LR
    [*] --> Heating: Preheat button or preheat_chamber (confirmed)
    Heating: Bed at printer maximum, chamber rising
    Heating --> BedOff: chamber target reached
    Heating --> BedOff: plateau (less than 1 °C in 10 min)
    Heating --> BedOff: max_minutes elapsed
    Heating --> BedOff: Stop button
    Heating --> Waiting: printer offline
    Waiting: Waiting for the printer (up to 12 h)
    Waiting --> BedOff: back online and idle
    Heating --> PrintStarted: a print starts
    PrintStarted: Monitor stops, the print takes over the bed
    BedOff: Bed switched off automatically
    BedOff --> [*]
    PrintStarted --> [*]
```

Related engine options (filament profile): **Heat soak time** and **Heat soak bed temperature** add a soak block before the start G-code (exhaust and side fans off, wait for the bed, dwell), so the chamber can settle after the printer's own preparation.

## 🧱 Multi-layer brim with its own gap
For warping-prone materials a single-layer brim often is not enough. MentorSlicer can repeat the brim on the layers above the first one, and keep those upper brim layers at a **different distance from the part**:

- **Brim layers** (`brim_layers`, process or per object): how many layers the brim has. The first-layer brim is repeated on the following layers, with the flow scaled to the layer height.
- **Brim-object gap on upper brim layers** (`brim_layers_object_gap`): the gap between the brim and the part on layers 2 and above. Layer 1 keeps the normal **Brim-object gap**, so it still holds the part firmly on the plate; a small extra gap on the upper layers (for example 0.1–0.3 mm) stops the repeated brim from fusing to the part wall, so it peels off cleanly. Lines that would come closer than this gap are removed or trimmed, never moved; other objects on the plate are kept clear as well. With a value not larger than the brim-object gap, the upper layers simply copy layer 1.

![Side section of a three-layer brim: layer 1 touches the part, layers 2 and 3 keep a small gap](doc/images/brim-layers.svg)

Tested on a 130 mm ASA part on a Creality K1C (with glue stick on smooth PEI): see the ASA notes in [CLAUDE.md](CLAUDE.md).

## 🤖 Print Advisor for AI assistants
The Print Advisor lets Claude Code or Codex read the slicing context, diagnose defects, propose profile changes and control the printer over the LAN, with every change confirmed inside MentorSlicer. The MCP server ships as `mentorslicer-mcp.exe` next to `MentorSlicer.exe`, so **no Node.js installation is needed**: open **AI Integration → Print Advisor MCP…**, copy the command and press **Enable**. Details in [tools/print-advisor-mcp/README.md](tools/print-advisor-mcp/README.md).

```mermaid
flowchart LR
    AI["Claude Code / Codex<br/>(the LLM)"] -- "MCP over stdio" --> EXE["mentorslicer-mcp.exe<br/>tool schemas, guides"]
    EXE -- "HTTP on 127.0.0.1<br/>+ session token" --> APP["MentorSlicer<br/>Print Advisor service"]
    APP -- "confirmation dialog" --> USER(["You approve or cancel"])
    APP -- "LAN WebSocket" --> PRN["Printer<br/>(e.g. Creality K1C)"]
```

## 📚 Third-party libraries under control
**Help → Third-party libraries** shows for each library what it is used for, the version in use, the latest release and whether it should be updated (or why it is kept on purpose). The **Check for updates** button asks GitHub, GitLab and NuGet only when you press it.

# Main features (inherited from Creality Print)
- Auto calibrations for all printers
- Sandwich(inner-outer-inner) mode - an improved version of the `External perimeters first` mode
- Polyholes conversion support [SuperSlicer Wiki: Polyholes](https://github.com/supermerill/SuperSlicer/wiki/Polyholes)
- Klipper support
- More granular controls


# How to install
**Windows**: 
1.  Build it (see "How to compile"); `tools\publish-stable.ps1` copies a tested build to `C:\MentorSlicer` with a desktop shortcut.
    - *If you have troubles to run the build, you might need to install following runtimes:*
      - [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)
          - [Details of this runtime](https://aka.ms/webview2)
          - [Alternative Download Link Hosted by Microsoft](https://go.microsoft.com/fwlink/p/?LinkId=2124703)
      - [vcredist2019_x64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)
          -  [Alternative Download Link Hosted by Microsoft](https://aka.ms/vs/17/release/vc_redist.x64.exe)
          -  This file may already be available on your computer if you've installed visual studio.  Check the following location: `%VCINSTALLDIR%Redist\MSVC\v142`

**Mac**:
1. Download the DMG for your computer: `arm64` version for Apple Silicon and `x86_64` for Intel CPU.  
2. Drag CrealityPrint.app to Application folder. 
3. *If you want to run a build from a PR, you also need following instructions below*  
    <details quarantine>
    - Option 1 (You only need to do this once. After that the app can be opened normally.):
      - Step 1: Hold _cmd_ and right click the app, from the context menu choose **Open**.
      - Step 2: A warning window will pop up, click _Open_  
      
    - Option 2:  
      Execute this command in terminal: `xattr -dr com.apple.quarantine /Applications/CrealityPrint.app`
      ```console
          softfever@mac:~$ xattr -dr com.apple.quarantine /Applications/CrealityPrint.app
      ```
    </details>
    
**Linux(Ubuntu)**:
 1. If you run into trouble to execute it, try this command in terminal:  
    `chmod +x /path_to_appimage/CrealityPrint_Linux.AppImage`
    
# Command line slicing

MentorSlicer can run slicing jobs from the command line without opening the normal GUI workflow. Use the `--slice` action with one or more input model/project files.

```console
MentorSlicer.exe --slice 0 --outputdir "C:\output" "C:\models\project.3mf"
```

`--slice` selects which plate to slice:

- `--slice 0`: slice all plates.
- `--slice 1`: slice plate 1.
- `--slice 2`: slice plate 2, and so on.

Useful options:

- `--outputdir <dir>`: directory for generated G-code and CLI result files.
- `--load_settings "<setting1.json;setting2.json>"`: load process/printer settings. This is usually needed when slicing STL/OBJ files that do not already contain project settings.
- `--load_filaments "<filament1.json;filament2.json>"`: load filament settings.
- `--debug <level>`: set log level. `0` is fatal only, `5` is trace.
- `--logfile <file>`: write debug logs to a file.

Examples:

Slice all plates from a 3MF project:

```console
MentorSlicer.exe --slice 0 --outputdir "C:\output" "C:\models\example.3mf"
```

Slice only the first plate and save a log:

```console
MentorSlicer.exe --slice 1 --outputdir "C:\output" --logfile "C:\output\slice.log" "C:\models\example.3mf"
```

Slice an STL with external printer/process and filament presets:

```console
MentorSlicer.exe --slice 0 --outputdir "C:\output" --load_settings "C:\presets\printer.json;C:\presets\process.json" --load_filaments "C:\presets\pla.json" "C:\models\part.stl"
```

Run `MentorSlicer.exe --help` to see the full list of supported command line options.

# How to compile
- Windows 64-bit  
  - Tools needed: Visual Studio 2022 Build Tools (C++ workload), CMake, git.
  - Build the dependencies once with `build_release_vs2022.bat deps`, then build the app with `build_exe_only.bat` (`build\src\Release\MentorSlicer.exe` only) or `build_release_vs2022.bat` (full build with install).
  - See `CLAUDE.md` for the VS Code tasks, the build pitfalls and the Print Advisor notes.

- Mac 64-bit  
  - Tools needed: Xcode, Cmake, git, gettext, libtool, automake, autoconf, texinfo
      - You can install most of them by running `brew install cmake gettext libtool automake autoconf texinfo`
  - run `build_release_macos.sh`
  - To build and debug in XCode:
      - run `XCode.app`
      - open ``build_`arch`/CrealityPrint.xcodeproj``
      - menu bar: Product => Scheme => CrealityPrint
      - menu bar: Product => Scheme => Edit Scheme...
          - Run => Info tab => Build Configuration: `RelWithDebInfo`
          - Run => Options tab => Document Versions: uncheck `Allow debugging when browsing versions`
      - menu bar: Product => Run

- Ubuntu 
  - Dependencies **Will be auto installed with the shell script**: `libmspack-dev libgstreamerd-3-dev libsecret-1-dev libwebkit2gtk-4.0-dev libosmesa6-dev libssl-dev libcurl4-openssl-dev eglexternalplatform-dev libudev-dev libdbus-1-dev extra-cmake-modules libgtk2.0-dev libglew-dev libudev-dev libdbus-1-dev cmake git texinfo`
  - run 'sudo ./BuildLinux.sh -u'
  - run './BuildLinux.sh -dsir'


# Note: 
If you're running Klipper, it's recommended to add the following configuration to your `printer.cfg` file.
```
# Enable object exclusion
[exclude_object]

# Enable arcs support
[gcode_arcs]
resolution: 0.1
```

# Supports
**Creality Print** is an open-source project, and I'm deeply grateful to all my sponsors and backers.   
Their generous support enables me to purchase filaments and other essential 3D printing materials for the project.   
Thank you! :)



## Some background
MentorSlicer is forked from [Creality Print](https://github.com/CrealityOfficial/CrealityPrint) by Creality.  
Creality Print is originally forked from [Orca Slicer](https://github.com/SoftFever/OrcaSlicer) by SoftFever
Orca Slicer is forked from [Bambu Studio](https://github.com/bambulab/BambuStudio) by Bambu
Bambu Studio is forked from [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community. 
Creality Print incorporates a lot of features from SuperSlicer by @supermerill
Creality Print's logo is designed by community member Justin Levine(@freejstnalxndr)  


# License
MentorSlicer is licensed under the GNU Affero General Public License, version 3. MentorSlicer is based on Creality Print by Creality.

Creality Print is licensed under the GNU Affero General Public License, version 3. Creality Print is based on Orca Slicer by Orca Slicer.

Orca Slicer is licensed under the GNU Affero General Public License, version 3. Creality Print is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Creality Print includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The bambu networking plugin is based on non-free libraries from Bambulab. It is optional to the Creality Print and provides extended functionalities for Bambulab printer users.

test
