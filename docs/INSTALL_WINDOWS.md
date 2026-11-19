# Install Litematica Port Bedrock — Windows PC

This is the **player installation guide** included with the downloadable Windows release. You do **not** need Python, Node.js, xmake, or Visual Studio to install a published binary package.

## Requirements

- Minecraft Bedrock Edition for **Windows PC 1.26.60**, matching the version tested by the maintainer.
- **LeviLamina client 26.60.x** installed in a compatible LeviLauncher instance.
- A Windows x64 computer. This build does not run on Android, iOS, consoles, or vanilla Bedrock without LeviLamina.

## Install (recommended)

1. Download **LHolo-client-windows-x64.zip** from the [official repository's Releases](https://github.com/kennethbadillo782-lab/LitematicaPortBedrock/releases). If no release is shown, no prebuilt public version has been published yet; downloading "Source code (zip)" is **not** the same as downloading an installable mod.
2. Extract the ZIP to a normal folder. Keep `Instalar.ps1` next to the included `LHolo/` and `Litematica_Realm_Companion_BP/` folders.
3. **Close Minecraft** before installing.
4. Open PowerShell in the extracted folder, then run:

   ~~~powershell
   powershell -ExecutionPolicy Bypass -File .\Instalar.ps1
   ~~~

5. Select your LeviLauncher instance if prompted. The installer copies the mod and backs up any existing `LHolo` mod folder. It does not shut down Minecraft or modify your own config/data.
6. Open Minecraft using that LeviLauncher instance, then access the Litematica Port menu. Some internal names still say `LHolo` for compatibility.

To specify the instance yourself:

~~~powershell
powershell -ExecutionPolicy Bypass -File .\Instalar.ps1 -Instancia "C:\path\to\LeviLauncher\Instances\YourInstance"
~~~

To install **only the client mod**, without copying the optional companion:

~~~powershell
powershell -ExecutionPolicy Bypass -File .\Instalar.ps1 -SinCompanion
~~~

## Manual installation

With Minecraft closed, copy the included `LHolo/` directory to `mods/LHolo/` in your compatible LeviLauncher instance. Keep the full directory structure, including `LHolo.dll`, `manifest.json`, and `resource_packs/`. Do **not** copy `LHolo.dll` alone.

## Optional Realm Companion

The ZIP also includes `Litematica_Realm_Companion_BP/`. This is an **optional behavior pack** for supported worlds/Realms or servers. Merely copying it does **not** activate it in an existing world or give players server permissions.

To use server-side structure writing, install/activate the behavior pack in the target world/Realm using the world's behavior-pack settings, and ensure you have **operator permissions**. See `Litematica_Realm_Companion_BP/README.md` for details and limitations. If you only need local holograms, you can skip the Companion.

## Troubleshooting

- **Installer cannot find `LHolo/LHolo.dll`:** you downloaded source code instead of the release ZIP, did not extract the ZIP, or ran the script outside the extracted folder.
- **No LeviLauncher instances found:** first create/configure an instance, or use `-Instancia` with the correct instance directory.
- **Mod does not load:** confirm Minecraft and LeviLamina versions match the release. Updates to Minecraft can require a compatible mod build.
- **Realm placement does not work:** verify the Companion is enabled on that server/world and your account has operator permissions; remote block/entity NBT support is limited.

This project is based on a modified [LHolo by MarmieQi](https://github.com/MarmieQi/LHolo). See `ATTRIBUTION.md`, `THIRD_PARTY_NOTICES.md`, and `LHolo/LICENSE` in this ZIP.

---

# Instalación de Litematica Port Bedrock — PC Windows

Esta guía se incluye dentro de la descarga para jugadores. **No necesitas compilar código ni instalar herramientas de desarrollo** si descargas el ZIP de una release.

## Requisitos

- Minecraft Bedrock **para Windows 1.26.60**.
- **LeviLamina cliente 26.60.x** instalado en una instancia compatible de LeviLauncher.
- Una PC Windows x64. No es una versión para móviles ni consolas.

## Instalación rápida

1. Descarga **LHolo-client-windows-x64.zip** desde [Releases del repositorio oficial](https://github.com/kennethbadillo782-lab/LitematicaPortBedrock/releases). Si todavía no existe una release, no hay una descarga precompilada pública: el botón **Source code (zip)** no sirve como instalador.
2. Extrae todo el contenido. Debes encontrar `Instalar.ps1`, la carpeta `LHolo/` y la carpeta opcional `Litematica_Realm_Companion_BP/`.
3. **Cierra Minecraft** antes de continuar.
4. Abre PowerShell dentro de la carpeta extraída y ejecuta:

   ~~~powershell
   powershell -ExecutionPolicy Bypass -File .\Instalar.ps1
   ~~~

5. Selecciona tu instancia de LeviLauncher si aparecen varias. El instalador guarda una copia de seguridad del mod anterior, si existe, y conserva tus archivos personales de configuración y datos.
6. Abre Minecraft mediante esa instancia. Algunas rutas y comandos internos todavía usan el nombre `LHolo`.

Si el instalador no detecta tu instancia, utiliza `-Instancia "ruta\de\tu\instancia"`. Para instalar solamente el mod cliente, añade `-SinCompanion`.

**Instalación manual:** con el juego cerrado, copia la carpeta `LHolo/` completa a la ruta `mods/LHolo/` de la instancia. No copies únicamente el archivo DLL; el mod también necesita su paquete de interfaz.

**Realm Companion (opcional):** la carpeta `Litematica_Realm_Companion_BP/` contiene un paquete de comportamiento que se debe activar en el mundo o Realm para usar la escritura del lado del servidor. **Requiere permisos de operador.** Copiar el paquete no lo activa automáticamente. Consulta el `README.md` del Companion.

**Si algo falla:** comprueba las versiones de Minecraft y LeviLamina, confirma que descargaste la release completa (no el código fuente) y revisa que hayas extraído todas las carpetas.

Consulta `ATTRIBUTION.md` para los créditos de LHolo y los proyectos originales.
