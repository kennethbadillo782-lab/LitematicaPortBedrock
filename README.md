# Litematica for Minecraft Bedrock (Windows)

**Litematica-style schematic building for Minecraft Bedrock Edition on Windows.** A native client mod for [LeviLamina](https://github.com/LiteLDev/LeviLamina) that loads Java Edition schematics, projects in-game holograms, and offers building, verification and structure placement tools.

**Looking for Litematica on Bedrock?** This repository is a community-developed **Litematica Bedrock port / Minecraft Bedrock schematic mod** for Windows. It can read Java `.litematic` blueprints and use them in Bedrock without recreating the structure by hand. It is **not the official Java Edition Litematica mod**, and full feature parity with Java Litematica has **not** yet been achieved.

[**Features**](#features) · [**Compatibility**](#compatibility-and-status) · [**Download & install**](#download-and-installation) · [**Build from source**](#build-from-source) · [**Español**](#español) · [**Credits**](#license-and-credits)

> **Attribution:** Based on a **modified version** of [LHolo by MarmieQi](https://github.com/MarmieQi/LHolo) (GPL-3.0). This is not the original LHolo and is not affiliated with its author. See [ATTRIBUTION.md](ATTRIBUTION.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). LeviLamina is a separate dependency and is not distributed in this repository.

## Features

- **Java-to-Bedrock schematics:** load `.litematic`, `.schematic`, `.schem`, `.nbt` and Bedrock `.mcstructure` files.
- **In-game hologram projection:** preview structures in the world and adjust placements; supported tools include rotation, mirroring, and layer-based building assistance.
- **Building assistance:** block/state conversion, placement checks, material information, and projection HUD tools.
- **Schematic management:** multiple files, task/placement controls, structure preview, and a dedicated OreUI interface.
- **World placement:** structure-block-based writing in supported single-player worlds and command-based placement where permissions allow it.
- **Optional Realm Companion:** a Bedrock behavior pack that can queue server-side structure-writing jobs on compatible Realms or servers **when installed and used with operator permissions**. See [Realm Companion documentation](realm_companion/README.md).

Not every option, keybinding, renderer behavior or workflow from Java Litematica is available. For implementation details and current limitations, see [DEVELOPMENT.md](DEVELOPMENT.md), [CHANGELOG.md](CHANGELOG.md), and [Configuration](docs/CONFIGURATION.md).

## Compatibility and status

| Item | Current documented status |
| --- | --- |
| Platform | Minecraft Bedrock Edition **for Windows**; not a mobile, console, or general-purpose behavior-pack port |
| Minecraft version | **1.26.60** tested manually by the author |
| Mod loader | LeviLamina **client 26.60.x** matching the game |
| Native build | C++ / xmake / clang-cl; build and isolated tests documented as passing |
| In-game projection, UI and single-player/Realm placement | Reported manually tested on 1.26.60 in the existing project documentation |
| Java Litematica feature parity | **Partial / ongoing** — do not assume a complete 1:1 implementation |
| Remote paste | Requires the optional companion installed on the server/Realm and operator privileges; block/entity NBT support is limited |

**Experimental software.** Minecraft or LeviLamina updates may break compatibility. Successful isolated tests are not the same as verification of every feature in a live game.

## Download and installation

**No public GitHub Release was available when this README was updated (2026-11-19).** The source code is available here, but a prebuilt, ready-to-install package is not currently advertised as available.

- **[Check GitHub Releases](../../releases)** for future Windows packages.
- **[Read the build instructions](#build-from-source)** if you want to compile from source.
- **[Report an issue](../../issues)** if installation or compatibility fails.

For a future packaged release, the existing installer workflow is:

1. Install the matching Minecraft Bedrock for Windows and [LeviLamina client](https://github.com/LiteLDev/LeviLamina) (for example through LeviLauncher).
2. Download and extract the matching Windows package from Releases, **once one is published**.
3. Close Minecraft, then run `Instalar.ps1` from the extracted release directory:

   ~~~powershell
   powershell -ExecutionPolicy Bypass -File .\Instalar.ps1
   ~~~

4. Launch the game, then open the mod's OreUI menu. The installed native mod currently retains the name `LHolo` in several paths and commands.
5. For Realms/servers, follow [Realm Companion setup](realm_companion/README.md) separately; merely installing the client does not grant server write access.

The installer can locate LeviLauncher instances and backs up files it replaces. The manual package layout uses an `LHolo/` folder under the instance's `mods/` directory.

## Build from source

Windows developer prerequisites: [xmake](https://xmake.io/), Visual Studio Build Tools / clang-cl, and Node.js 20+.

~~~powershell
xmake f -c -y -m release -p windows -a x64 --toolchain=clang-cl
xmake -b LHolo
xmake -b LHoloLogicTests
xmake run LHoloLogicTests
npm --prefix frontend ci
npm --prefix frontend test
node --experimental-vm-modules realm_companion/test.cjs
~~~

See [DEVELOPMENT.md](DEVELOPMENT.md) for the complete development and compatibility workflow, and [CONTRIBUTING.md](CONTRIBUTING.md) before contributing.

## Español

### Litematica para Minecraft Bedrock (Windows)

¿Buscas **Litematica para Bedrock**? Este proyecto adapta herramientas de esquemas estilo Litematica a **Minecraft Bedrock Edition para Windows** mediante un mod nativo de **LeviLamina**. Permite cargar construcciones de Java (incluidos archivos `.litematic`), proyectar hologramas dentro del juego, consultar materiales y usar herramientas de colocación y escritura de estructuras.

**Funciones principales:** formatos `.litematic`, `.schematic`/`.schem`, `.nbt` y `.mcstructure`; conversión de bloques/estados Java → Bedrock; menú OreUI; vistas previas y gestión de proyectos; opciones de pegado en mundos propios y, con un complemento de servidor y permisos de operador, en Realms/servidores compatibles.

**Estado:** desarrollo experimental. La documentación informa pruebas manuales en **Minecraft Bedrock para Windows 1.26.60**, pero la equivalencia funcional **1:1 con Litematica Java todavía es parcial**. No es un mod para Android, iOS o consolas. Por ahora **no hay una release pública**; visita [Releases](../../releases) para comprobar si se publica una descarga.

**Instalación:** requiere una versión compatible de Minecraft Bedrock para Windows y LeviLamina cliente. Cuando exista un paquete de release, habrá que extraerlo, cerrar Minecraft y ejecutar `Instalar.ps1` desde PowerShell. Mientras tanto, los desarrolladores pueden compilar desde el código fuente siguiendo los pasos anteriores.

**Créditos:** basado en una versión modificada de [LHolo de MarmieQi](https://github.com/MarmieQi/LHolo), bajo GPL-3.0. No es el LHolo original ni está afiliado a su creador. Consulta [ATTRIBUTION.md](ATTRIBUTION.md).

## License and credits

- **License:** [GPL-3.0](LICENSE); see the project's [attribution terms and contributor credits](ATTRIBUTION.md).
- **Upstream foundation:** [LHolo — MarmieQi](https://github.com/MarmieQi/LHolo).
- **Loader (external):** [LeviLamina — LiteLDev](https://github.com/LiteLDev/LeviLamina).
- **Java behavior and schematic-format reference (not copied Java source):** [Litematica — maruohon](https://github.com/maruohon/litematica).
- **Mapping data:** [Chunker — HiveGamesOSS](https://github.com/HiveGamesOSS/Chunker), see [third-party notices](THIRD_PARTY_NOTICES.md).

This repository is a community project, not an official Minecraft, Mojang, Microsoft, LeviLamina or Litematica release.
