# Litematica Java for Minecraft Bedrock (Windows) — Native Port

**A working native port of the Java Edition Litematica experience to Minecraft Bedrock for Windows, with core features tested in-game by the project maintainer.** The goal is to recreate the original Litematica features, controls and workflows as faithfully as possible in Bedrock — not simply to offer an unrelated schematic viewer. Built as a native [LeviLamina](https://github.com/LiteLDev/LeviLamina) client mod, it loads Java Edition schematics, projects holograms directly into the game, and provides building and structure placement tools.

**Looking for Litematica on Bedrock?** This is a functional, community-developed **Litematica Bedrock port / Minecraft Bedrock schematic mod** for Windows — **not just a mockup or an untested concept**. The maintainer has used and tested the essential workflow inside Minecraft Bedrock: loading schematics, displaying projections, using the in-game interface, and writing structures. Java `.litematic` blueprints work directly in Bedrock. **The core mod works today on the documented supported version.** This is a Bedrock-native reimplementation of the Java Litematica experience, not the original Java mod running unchanged. The currently implemented core workflows operate inside Bedrock; additional features are being ported toward full 1:1 feature and behavior parity.

[**Features**](#features) · [**Compatibility**](#compatibility-and-status) · [**Download & install**](#download-and-installation) · [**Build from source**](#build-from-source) · [**Español**](#español) · [**Credits**](#license-and-credits)

> **Attribution:** Based on a **modified version** of [LHolo by MarmieQi](https://github.com/MarmieQi/LHolo) (GPL-3.0). This is not the original LHolo and is not affiliated with its author. See [ATTRIBUTION.md](ATTRIBUTION.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). LeviLamina is a separate dependency and is not distributed in this repository.

## Java Litematica experience, running natively on Bedrock

This project is designed as a **faithful Java Litematica-to-Bedrock port**: the user-facing goal is to preserve the familiar schematic formats, hologram projections, placement workflow, and building tools of Java Litematica while adapting the underlying implementation to the Bedrock engine. **The essential workflow is already functional and has been tested in-game by KeNnoTh.** The term **1:1** describes the project's fidelity target, **not a claim that every original Java feature or shortcut has already been verified or implemented**.

## Features

- **Java-to-Bedrock schematics:** load `.litematic`, `.schematic`, `.schem`, `.nbt` and Bedrock `.mcstructure` files.
- **In-game hologram projection:** preview structures in the world and adjust placements; supported tools include rotation, mirroring, and layer-based building assistance.
- **Building assistance:** block/state conversion, placement checks, material information, and projection HUD tools.
- **Schematic management:** multiple files, task/placement controls, structure preview, and a dedicated OreUI interface.
- **World placement:** structure-block-based writing in supported single-player worlds and command-based placement where permissions allow it.
- **Optional Realm Companion:** a Bedrock behavior pack that can queue server-side structure-writing jobs on compatible Realms or servers **when installed and used with operator permissions**. See [Realm Companion documentation](realm_companion/README.md).

**What is working vs. what is still being ported:** The core schematic workflow is functioning in-game according to hands-on testing by KeNnoTh. Some advanced Java Litematica settings, shortcuts, screens, and tools are still being integrated. Those remaining parity tasks **do not mean that schematic loading, projection, the menu, or basic building/placement are untested or unusable**. See [DEVELOPMENT.md](DEVELOPMENT.md), [CHANGELOG.md](CHANGELOG.md), and [Configuration](docs/CONFIGURATION.md) for implementation-level detail.

## Compatibility and status

| Item | Current documented status |
| --- | --- |
| Platform | Minecraft Bedrock Edition **for Windows**; not a mobile, console, or general-purpose behavior-pack port |
| Minecraft version | **1.26.60** — tested in the running game by the maintainer |
| Mod loader | LeviLamina **client 26.60.x** matching the game |
| Native build | C++ / xmake / clang-cl; builds and isolated tests pass; **core functionality has also been exercised in-game** |
| In-game core features | **Working and hands-on tested by the maintainer:** schematic loading, hologram projection, OreUI menu, and essential building/structure-writing workflows on the supported Bedrock version |
| Java Litematica feature parity | **Core functionality works; advanced parity work continues.** Not all Java Litematica features are implemented yet |
| Remote paste | Requires the optional companion installed on the server/Realm and operator privileges; block/entity NBT support is limited |

**Project status: functional, actively developed.** This mod has been tested in an actual Minecraft Bedrock session by its maintainer; it is **not a code-only prototype**. Further development focuses on expanding Java Litematica feature parity and compatibility. New Minecraft/LeviLamina updates can require a matching mod build. Individual advanced features not explicitly listed as tested should not be assumed verified.

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

¿Buscas **el Litematica de Java funcionando en Bedrock**? Este proyecto es un **port nativo que busca reproducir fielmente la experiencia de Litematica Java en Minecraft Bedrock para Windows**, incluyendo sus esquemas, hologramas, controles y herramientas de construcción. No pretende ser un visor de esquemas distinto: busca que el jugador obtenga el mismo resultado y flujo de trabajo que en Java, adaptado al motor Bedrock mediante **LeviLamina**. Permite cargar construcciones de Java (incluidos archivos `.litematic`), proyectar hologramas dentro del juego, consultar materiales y usar herramientas de colocación y escritura de estructuras.

**Funciones principales:** formatos `.litematic`, `.schematic`/`.schem`, `.nbt` y `.mcstructure`; conversión de bloques/estados Java → Bedrock; menú OreUI; vistas previas y gestión de proyectos; opciones de pegado en mundos propios y, con un complemento de servidor y permisos de operador, en Realms/servidores compatibles.

**Estado: funcional y en desarrollo activo.** **KeNnoTh ha probado personalmente el mod dentro de Minecraft Bedrock para Windows 1.26.60 y confirma que las funciones esenciales funcionan:** cargar esquemas, proyectar hologramas, utilizar el menú y las herramientas básicas de construcción y escritura. **No es una demostración sin probar ni un proyecto que solo compile.** **La meta es una equivalencia 1:1 en funciones y comportamiento con Litematica Java**; las funciones esenciales ya están operativas, aunque todavía quedan funciones avanzadas y ajustes por portar. Por eso, «port fiel» describe la intención y la implementación actual, mientras que «1:1 completo» sigue siendo una meta, no una afirmación de paridad total comprobada. No está diseñado para Android, iOS ni consolas. **Funcionar dentro del juego no significa que exista ya una descarga pública preparada:** consulta [Releases](../../releases).

**Instalación:** requiere una versión compatible de Minecraft Bedrock para Windows y LeviLamina cliente. Cuando exista un paquete de release, habrá que extraerlo, cerrar Minecraft y ejecutar `Instalar.ps1` desde PowerShell. Mientras tanto, los desarrolladores pueden compilar desde el código fuente siguiendo los pasos anteriores.

**Créditos:** basado en una versión modificada de [LHolo de MarmieQi](https://github.com/MarmieQi/LHolo), bajo GPL-3.0. No es el LHolo original ni está afiliado a su creador. Consulta [ATTRIBUTION.md](ATTRIBUTION.md).

## License and credits

- **License:** [GPL-3.0](LICENSE); see the project's [attribution terms and contributor credits](ATTRIBUTION.md).
- **Upstream foundation:** [LHolo — MarmieQi](https://github.com/MarmieQi/LHolo).
- **Loader (external):** [LeviLamina — LiteLDev](https://github.com/LiteLDev/LeviLamina).
- **Java behavior and schematic-format reference (not copied Java source):** [Litematica — maruohon](https://github.com/maruohon/litematica).
- **Mapping data:** [Chunker — HiveGamesOSS](https://github.com/HiveGamesOSS/Chunker), see [third-party notices](THIRD_PARTY_NOTICES.md).

This repository is a community project, not an official Minecraft, Mojang, Microsoft, LeviLamina or Litematica release.
