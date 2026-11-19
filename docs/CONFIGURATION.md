# 配置文件

配置路径：`mods/LHolo/config/config.json`，当前格式版本为 **13**。
默认配置由模组首次运行时生成。字段顺序固定，缩进为 4 个空格。

| 分组 | 内容 |
| --- | --- |
| `interface.language` | 默认 `"auto"` 跟随 Minecraft。游戏语言缺少对应翻译时显示英语，配置仍为 `"auto"`。填 `"es_ES"`、`"zh_CN"`、`"en_US"` 等语言代码可手动指定。 |
| `rendering` | 结构边框、纠错填充/描边、穿透显示 |
| `hud.projection` | 普通投影 HUD 开关、内容与外观 |
| `hud.materials` | 材料 HUD 开关、堆叠换算、每页项数与外观 |
| `input` | Alt+滚轮开关和快捷键 |
| `placement` | 知情确认、放置半径、破坏后冷却 |

## 数值与位置

- 后缀 `Percent` 明确表示百分比。透明度 0～100，HUD 缩放 50～200；默认背景不透明度 60%、缩放 100%。
- 材料 `convertToStacks` 默认 `false`，`itemsPerPage` 范围 1～50、默认 5。
- `placement.radius` 范围 1～4，`breakCooldownSeconds` 范围 0～60、默认 10 秒。
- `position.anchor` 使用 `topLeft`、`bottomLeft`、`topRight`、`bottomRight` 四个稳定名称。
- 使用原生 HUD 编辑器拖动后，位置中会增加 `custom: {"x": 0.2, "y": 0.3}`，坐标相对视口归一化到 0～1。不使用自定义位置时省略整个 `custom` 对象，避免保存无效坐标。

## 快捷键

快捷键按组合字符串保存，例如 `"Alt+M"`、`"Ctrl+Left"`、`"Ctrl+Shift+F8"`，`null` 表示未绑定。大小写及分隔符两侧空格不影响读取；保存时统一为 Ctrl、Alt、Shift 的顺序。

支持字母、数字、F1～F24、Left/Right/Up/Down、PageUp/PageDown、Home/End、Insert/Delete、Enter/Tab/Space/Escape、MouseLeft/MouseRight/MouseMiddle/MouseSide1/MouseSide2，以及 Numpad0～Numpad9 等常见按键。少见按键使用 `VK_186` 这种可往返的 Windows 虚拟键码表示。

材料 HUD 上一页和下一页默认均为 `null`。建议在游戏快捷键页面绑定，沿用原有冲突处理及按键捕获规则。

## 旧配置迁移

1. 可以读取版本 12 及更早的平铺配置，将仍有效的语言、渲染、HUD、布局、快捷键和放置参数映射到新分组。
2. 成功应用后写回规范格式。第一次覆盖旧配置前，将原文件逐字节备份为 `config.json.legacy.bak`，以后保存不会覆盖该备份。
3. 新配置不再保存旧单文件最近路径、`saved*` 恢复记录、旧 ImGui `uiScale`、退役的整体透明度或旧放置模式快捷键。任务目录、每文件维度/绝对坐标继续由 `data/tasks.json` 独立持久化。
4. 旧世界轴快捷键名、旧 HUD 标签可在迁移时读取，写入时只使用当前规范字段。

辅助放置模式本身、材料页码、投影运行状态均为会话状态，不写入偏好文件。旧 C++ ImGui 菜单及其可见、输入过渡和缩放状态已移除；单文件恢复快照仍只保留在内存中，不恢复旧配置字段，也不改变配置迁移规则。

## 校验与保存

缺失字段使用默认值，数值在读取边界限制到有效范围。错误类型、非法快捷键、未知位置锚点、损坏的 JSON 或不支持的未来版本会拒绝读取；不会部分应用，也不会被后续保存覆盖。修复配置后重启游戏再加载。

保存先写临时文件并关闭，再通过系统原子替换正式文件，避免直接截断当前配置。读写与迁移集中在 `src/settings/SettingsStore`；游戏状态的应用仍由原调用方负责，菜单和业务模块不感知 JSON 分组。

### 写入进度 HUD

`hud.write` 与现有投影/材料 HUD 独立：`enabled` 默认 true，`position.anchor` 默认 `topCenter`，`scalePercent` 默认 100，`backgroundOpacityPercent` 默认 60。自定义拖动位置沿用 `position.custom` 的归一化 x/y；缩放范围 50～200，透明度范围 0～100。该节缺省时按默认值加载，原有两个 HUD 的配置不变；顶中锚点随面板实测宽度保持居中。布局与外观仍通过游戏内原生 HUD 编辑器保存。


### Native upstream configuration foundation (2026-11-14)

`tools/litematica/import_sources.py` imports the active declarations of the pinned Litematica checkout and the MaLiLib keybinding policies. The actual source has 168 active configuration declarations, of which 167 belong to the exposed/persisted OPTIONS lists, and 78 hotkeys; the earlier count of 175 included seven commented declarations. Fifteen upstream locale dictionaries are merged with MaLiLib and embedded compressed in `src/config/UpstreamData.h`. `--check` verifies reproducibility against the local upstream checkouts. The generated hook inventory in `work/litematica-hooks-map.json` explicitly remains pending semantic review; file/name counts do not demonstrate equivalence.

`config/ConfigRegistry` validates boolean, boolean-with-hotkey, integer, floating-point, color, enum, string and string-list values. Persistence uses Java's `Generic`, `Visuals`, `InfoOverlays`, `Colors` and `Hotkeys` sections and `keys`/`settings` objects in `mods/LHolo/config/litematica.json`. Invalid known values reject the load transactionally; unknown extension fields survive save. An invalid existing document is backed up before the first explicit replacement. Runtime-dependent defaults and the thread-count ceiling are marked in descriptors; they are not claimed to be platform-identical constants.

`app/JavaConfigActions` applies supported effects on the client control frame, publishes the catalog through TaskBindings and owns the generic `input/KeybindMulti` state machine. Arbitrary multi-key sequences retain ordering and support press/release/both, context, extra keys, exclusivity, cancellation and modifier state. The native capture records keys until all are released; Escape cancels. Context/focus loss clears held keys. M+V no longer falls through to the execute key. Keypad multiply/minus open area/placement configuration. The configuration page uses native descriptors and original locale keys; unavailable actions/options are visibly disabled and rejected natively, not accepted as no-op settings.

Currently connected option effects: toolItem, toolItemEnabled, executeRequireHoldingTool, reverseOperationModeDirection, selectionCornersMode, enableAreaSelectionBoxesRendering and enableRendering. The master render toggle is shared by projected mesh submission, selection boxes, verifier markers, HUD and tool mouse activation. It preserves schematic/session data and continues lifecycle/context processing while hidden. M+R toggles this value and persists it on the control frame. Supported key actions are enumerated in JavaConfigActions.cpp; all 78 are retained by the store, while effects without implementation remain disabled. The legacy fixed slots/config.json remain only as a transitional compatibility path and do not establish full MaLiLib parity. The existing layer editor and the remaining screen layouts still require a source-by-source port.

Selection interactions use tested pure helpers in `SelectionInteraction.h`: ray selection of corners/region bodies/manual origins, transactional int32-safe nudging and Java's Expand left-grow/right-reset behaviour. Alt+wheel moves the selected element; block-pick modifiers follow Java's primary Alt and secondary Shift defaults through the keybinding registry. Move requires an explicitly selected destination and publishes it to the interface; cancelled/failed grid writes clear all queued grid targets. Normal selections may export any complete enabled region independently of the selected editor row. Long-distance tool rays stop at unloaded chunks and avoid NaNs for parallel rays on exact voxel boundaries; collision-shape equivalence still needs game validation.

These are implemented foundations and isolated regressions, not a declaration of a complete port or in-game acceptance. No running game DLL is replaced by this workflow.

Boolean-hotkey options use their nested `hotkey.keys/settings` in the same native binding engine as ordinary hotkeys. Capture supports a category plus option name and preserves `enabled` and unrelated fields. Reset restores the boolean and its binding. Imported MaLiLib default settings drive both kinds of bindings. The importer follows explicit OPTIONS/HOTKEY_LIST order; All concatenates Generic, InfoOverlays, Visuals, Colors and Hotkeys as GuiConfigs does. The internal `loadEntireSchematics` declaration is not shown or newly inserted into saved documents; a captured 0.29.1 document now round-trips with exact JSON value equality (245 entries). Boolean text colors and ARGB color swatches are rendered, but pixel parity remains unverified.

Windows key events map to Java storage names for common punctuation, control keys, keypad keys and side mouse buttons. MaLiLib compatibility aliases are normalized when building bindings, leaving the imported JSON unchanged. Unmapped native virtual keys are not captured as nonportable VK strings. Keypad Enter is separated from Return in the window adapter. Keyboard layout equivalence still requires in-game verification.
