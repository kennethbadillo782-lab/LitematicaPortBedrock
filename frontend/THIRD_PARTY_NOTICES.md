Litematica UI port
==================

Screen layout and control order are derived from Litematica by masa and its sakura-ryoko maintainers, LGPL-3.0-or-later. Source: https://github.com/sakura-ryoko/litematica (eb15b39).

frontend/src/styles/litematica-atlas.css embeds the unmodified textures/gui/gui_widgets.png from that source. The corresponding LGPL license is in LITEMATICA-LICENSE.txt. No Minecraft Java binary or game font is included.

Native configuration descriptors, keybinding behaviour and translation resources are derived from Litematica (eb15b39) and MaLiLib (sakura-ryoko/malilib), LGPL-3.0-or-later. src/config/UpstreamData.h contains a compressed, reproducibly generated catalog and the original translation keys from both projects. See tools/litematica/import_sources.py.
