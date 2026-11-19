# Litematica Port Bedrock

Port de **Litematica** (el mod de esquemas de Java) a **Minecraft Bedrock para Windows**, como mod nativo de cliente para
[LeviLamina](https://github.com/LiteLDev/LeviLamina). Incluye un **Realm Companion** opcional (behavior pack) para pegar
esquemas en Realms y servidores donde no puedes usar comandos ilimitados.

> **Basado en LHolo.** Este proyecto parte de una versión **modificada** de [LHolo](https://github.com/MarmieQi/LHolo)
> de **MarmieQi** (GPL-3.0). No es el LHolo original ni está afiliado a su autor. LeviLamina no se incluye aquí.
> Ver [ATTRIBUTION.md](ATTRIBUTION.md).

*English summary at the bottom.*

## Qué hace

- Carga esquemas `.litematic`, `.schematic`/`.schem`, `.nbt`, `.mcstructure` y proyecta un holograma en el mundo.
- Convierte bloques y estados de Java a Bedrock (tabla basada en Chunker, ver `THIRD_PARTY_NOTICES.md`).
- Menú propio (OreUI) con colocaciones, herramientas, tareas y opciones.
- **Pegado por comandos** en mundos donde no tienes creativo directo (usa `setblock`/`fill` o el Companion).
- **Realm Companion**: el servidor guarda una cola de trabajos, carga chunks con *ticking areas* y escribe los bloques por sí mismo,
  así el cliente puede desconectarse del trabajo en cuanto el servidor lo recibe. Incluye además acciones de administración
  usadas por el mod «Panel del Servidor» (hora, clima, dar objetos, tp…) y un menú de aldeanos.

## Estado real (léelo antes de usarlo)

| Parte | Estado |
|---|---|
| Compilación del mod (xmake + clang-cl) | Compila |
| Tests lógicos (conversión Java→Bedrock, escritura), tests del frontend y del Companion | Pasan |
| Uso en el juego (proyección, menú, pegado en mundo propio y en Realm) | Probado a mano por el autor en 1.26.60 |
| Opciones y atajos de Litematica Java | **Parcial**: solo una parte está conectada |
| Pegado en servidores ajenos | Solo con el Companion instalado y operador |
| NBT de bloques / entidades en el pegado remoto | Limitado (ver `realm_companion/README.md`) |

Es un proyecto personal y puede romperse con actualizaciones del juego o de LeviLamina.

## Requisitos

- Minecraft Bedrock Edition para Windows **1.26.60** (la versión del mod debe coincidir con LeviLamina cliente `26.60.*`).
- [LeviLamina cliente](https://github.com/LiteLDev/LeviLamina) instalado (por ejemplo con LeviLauncher).

## Instalar (usuarios)

1. Descarga el `.zip` de la última *release* y descomprímelo.
2. **Cierra Minecraft.**
3. En PowerShell, dentro de la carpeta descomprimida:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\Instalar.ps1
   ```
   Detecta tus instancias de LeviLauncher; también puedes pasar `-Instancia "ruta"`. Hace copia de seguridad de lo que reemplaza.
   Manual: copia la carpeta `LHolo` a `mods\` de tu instancia.
4. Abre el juego y usa la tecla del menú (o escribe `LHolo` en el chat).
5. Para Realms/servidores: instala el Companion, ver [realm_companion/README.md](realm_companion/README.md).

## Compilar (desarrolladores)

Necesitas [xmake](https://xmake.io), Visual Studio Build Tools / clang-cl y Node.js 20+.

```powershell
xmake f -c -y -m release -p windows -a x64 --toolchain=clang-cl
xmake -b LHolo
xmake -b LHoloLogicTests ; xmake run LHoloLogicTests
npm --prefix frontend ci ; npm --prefix frontend test
node --experimental-vm-modules realm_companion/test.cjs
```

Más detalles en [DEVELOPMENT.md](DEVELOPMENT.md) y [CONTRIBUTING.md](CONTRIBUTING.md). Documentación original de LHolo en
`docs/upstream/`.

## Licencia y créditos

**GPL-3.0** (ver [LICENSE](LICENSE)) **más una condición de atribución**: si modificas, mejoras o redistribuyes este
proyecto, debes dar crédito a este repositorio y a **LHolo de MarmieQi**, conservar los avisos y decir qué cambiaste.
Texto completo en [ATTRIBUTION.md](ATTRIBUTION.md). Terceros en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## English summary

Litematica-style schematic projection and pasting for **Minecraft Bedrock (Windows)**, a native LeviLamina client mod,
with an optional server-side **Realm Companion** behavior pack. **Based on a modified LHolo by MarmieQi (GPL-3.0)**; not
affiliated with the original author; LeviLamina is not included. Install: unzip the release, close Minecraft, run
`Instalar.ps1` (or copy `LHolo/` into your instance's `mods/`). Build commands are above. Licensed GPL-3.0 with an attribution
requirement (see `ATTRIBUTION.md`): if you modify or improve it, credit this project and LHolo. Experimental, partially
implemented, tested by hand on 1.26.60 only.
