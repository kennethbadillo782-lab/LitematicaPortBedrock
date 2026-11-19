# Litematica Realm Companion 0.12.0

Behavior pack de servidor para Realms/servidores. Usa `@minecraft/server` **2.11.0** y `@minecraft/server-ui` **2.3.0**
(estables, sin experimentos). Pensado para Minecraft 1.26.60.

## Instalar

1. Copia `realm_companion/` a `behavior_packs/Litematica_Realm_Companion_BP/` (o ejecuta `Instalar.ps1` del release para el
   caso local), o empaquétalo como `.mcpack` (zip con `manifest.json` y `scripts/` en la raíz) e impórtalo.
2. Actívalo en el mundo/Realm (Paquetes de comportamiento).
3. Entra como **operador** y escribe `/litematica:hello`: debe responder con la versión 0.12.0.
4. El mod cliente saluda solo al conectar; no hace falta escribir nada más. Usa la herramienta de pegado del menú.

## Comandos (`/litematica:*`)

`hello`, `write`, `result`, `batch`, `chunk`, `ack`, `job`, `jobstatus`, `admin`, `fill`, `cancel`. Todos exigen operador.
`/litematica:fill ~ ~ ~ ~2 ~2 ~2 minecraft:stone` rellena directamente; `/litematica:cancel` vacía tu cola.

## Protocolo

- **LP1/LP2**: lotes de `setblock` con confirmación por consulta de resultados.
- **LP3** (trabajos): el cliente sube el trabajo completo en fragmentos de 400 caracteres hex (cabecera u16 + JSON, luego
  grupos de chunks con cuboides de 7 bytes). El servidor lo encola por jugador, abre *ticking areas* y escribe con
  `fillBlocks`/`setPermutation`; avisa en el chat al terminar.
- Los argumentos String no admiten comas y los comandos de ~980 caracteres cortaron la conexión NetherNet, de ahí los límites.
- `admin`: acciones del mod «Panel del Servidor» (hora, clima, dar, curar, tp, habilidades, modo de juego, reglas, aldeanos).
  No ejecuta comandos arbitrarios ni evalúa código recibido.

## Límites

- Solo escribe en chunks cargados/ticking; los fallos se detienen y se pueden reintentar.
- El pegado remoto transmite bloques y estados, no NBT de bloques ni entidades.
- No cambia permisos, reglas de trucos ni logros.

## Desarrollo

`node --experimental-vm-modules realm_companion/test.cjs`
