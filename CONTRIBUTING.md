# Contribuir

Se aceptan forks, mejoras y correcciones. Condiciones:

1. Licencia **GPL-3.0** + condición de atribución de [ATTRIBUTION.md](ATTRIBUTION.md): conserva los créditos (este proyecto y
   LHolo de MarmieQi) y di qué cambiaste.
2. No subas el juego, LeviLamina, DLL compiladas, grabaciones ni credenciales (el `.gitignore` ya excluye `*.dll`).
3. Antes de abrir un PR, ejecuta los tests de la sección «Compilar» del README.
4. Cambios del protocolo del Companion (`realm_companion/`): sube la versión en `manifest.json` y en
   `src/structure/write/command/CompanionHandshake.h` a la vez, y actualiza `test.cjs`.
5. Indica en el PR qué probaste en el juego y qué no.

Estructura: `src/` (C++ del mod), `frontend/` (menú OreUI en React), `tools/` (empaquetado y conversión), `tests/`,
`realm_companion/` (behavior pack del servidor), `docs/`.
