"""Import active upstream declarations and translations, never infer parity from names.

Inputs are the pinned sibling Litematica and work/malilib source checkouts.
Generated data is LGPL-3.0-or-later, like the upstream declarations/resources.
"""
from pathlib import Path
import argparse
import json
import re
import zlib

ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = ROOT.parent
JAVA = WORKSPACE / 'Litematica/src/main/java'
MALILIB = WORKSPACE / 'work/malilib/src/main/java'


def uncomment(text):
    return re.sub(r'("(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/)',
                  lambda m: m[0] if m[0].startswith('"') else ' ', text, flags=re.S)


def arguments(text):
    result, start, depth, quoted, escaped = [], 0, 0, False, False
    for i, c in enumerate(text):
        if quoted:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == '"': quoted = False
        elif c == '"': quoted = True
        elif c in '([{': depth += 1
        elif c in ')]}': depth -= 1
        elif c == ',' and depth == 0:
            result.append(text[start:i].strip()); start = i + 1
    return result + [text[start:].strip()]


def literal(value):
    return json.loads(re.sub(r'(?<=\d)[fFdD]$', '', value))


def catalog():
    path = JAVA / 'fi/dy/masa/litematica/config/Configs.java'
    source = uncomment(path.read_text(encoding='utf-8'))
    enum_sources = {p.stem: p for root in (JAVA, MALILIB) for p in root.rglob('*.java')}
    options = []
    groups = {'GENERIC_KEY': 'Generic', 'VISUALS_KEY': 'Visuals',
              'INFO_OVERLAYS_KEY': 'InfoOverlays', 'COLORS_KEY': 'Colors'}
    pattern = r'public static final (Config\w+)\s+(\w+)\s*=\s*new \1\((.*?)\)\.apply\((\w+)\)'
    for match in re.finditer(pattern, source, re.S):
        kind, symbol, raw, group_key = match.groups()
        args = arguments(raw)
        name = literal(args[0])
        group = groups[group_key]
        prefix = 'litematica.config.' + {'Generic': 'generic', 'Visuals': 'visuals',
                                       'InfoOverlays': 'info_overlays', 'Colors': 'colors'}[group]
        # Upstream spells InfoOverlays' prefix explicitly; derive rather than assume it.
        prefix_match = re.search(r'String\s+' + group_key + r'\s*=\s*Reference.MOD_ID\s*\+\s*"([^"]+)"', source)
        if prefix_match: prefix = 'litematica' + prefix_match[1]
        item = dict(name=name, symbol=symbol, group=group, type=kind,
                    commentKey=prefix + '.comment.' + name,
                    labelKey=prefix + '.name.' + name, sourceExpression=raw.strip())
        default = args[1]
        if kind == 'ConfigOptionList':
            if default.startswith('new i18nConfig'):
                item['runtimeDefault'] = 'language'; item['default'] = 'en_us'; item['choices'] = []
            else:
                enum, constant = default.split('.')
                enum_text = uncomment(enum_sources[enum].read_text(encoding='utf-8'))
                values = re.findall(r'^\s*(\w+)\s*\(\s*"([^"]+)"', enum_text, re.M)
                choices = dict(values)
                if constant not in choices: raise ValueError(f'Unknown enum value {default}')
                item['default'], item['choices'] = choices[constant], list(choices.values())
        elif kind == 'ConfigStringList': item['default'] = []
        elif 'getDefaultBaseSchematicDirectory' in default:
            item['default'] = ''; item['runtimeDefault'] = 'schematicDirectory'
        else: item['default'] = literal(default)
        if kind == 'ConfigBooleanHotkeyed': item['hotkeyDefault'] = literal(args[2])
        if kind in ('ConfigInteger', 'ConfigFloat', 'ConfigDouble'):
            if len(args) >= 4:
                if 'MIN_PLATFORM_THREADS' in args[2]:
                    item['min'] = 2; item['runtimeMax'] = 'max(2, processors/4)'
                else: item['min'], item['max'] = literal(args[2]), literal(args[3])
            elif kind == 'ConfigInteger': item['min'], item['max'] = -2147483648, 2147483647
        options.append(item)
    key_source = uncomment((JAVA / 'fi/dy/masa/litematica/config/Hotkeys.java').read_text(encoding='utf-8'))
    settings_source = uncomment((MALILIB / 'fi/dy/masa/malilib/hotkeys/KeybindSettings.java').read_text(encoding='utf-8'))
    presets = {}
    for match in re.finditer(r'KeybindSettings\s+(\w+)\s*=\s*new KeybindSettings\(([^;]+)\);', settings_source):
        args = arguments(match[2])
        presets[match[1]] = dict(context=args[0].split('.')[-1], activate_on=args[1].split('.')[-1],
            allow_extra_keys=literal(args[2]), order_sensitive=literal(args[3]),
            exclusive=literal(args[4]), cancel=literal(args[5]), allow_empty=literal(args[6]) if len(args)>6 else False)
    for item in options:
        if item['type'] == 'ConfigBooleanHotkeyed': item['settings'] = presets['DEFAULT']
    hotkeys = []
    for match in re.finditer(r'ConfigHotkey\s+(\w+)\s*=\s*new ConfigHotkey\((.*?)\)\.apply\(HOTKEYS_KEY\)', key_source, re.S):
        args = arguments(match[2]); name = literal(args[0])
        preset = args[2].split('.')[-1] if len(args)>2 else 'DEFAULT'
        hotkeys.append(dict(name=name, symbol=match[1], keys=literal(args[1]), settings=presets[preset],
                            labelKey='litematica.config.hotkeys.name.'+name, commentKey='litematica.config.hotkeys.comment.'+name))
    # GuiConfigs uses explicit OPTIONS/HOTKEY_LIST order, not declaration order.
    by_symbol = {item['symbol']: item for item in options}
    listed = []
    for match in re.finditer(r'\bOPTIONS\s*=\s*ImmutableList\.of\((.*?)\);', source, re.S):
        listed.extend(by_symbol[symbol.strip()] for symbol in arguments(match[1]) if symbol.strip())
    unlisted = set(by_symbol)-{item['symbol'] for item in listed}
    if unlisted != {'LOAD_ENTIRE_SCHEMATICS'} or len(listed) != len(options)-1:
        raise ValueError('Active option lists and declarations differ; review source ordering')
    for item in options: item['listed'] = item['symbol'] not in unlisted
    options = [item for group in ('Generic','InfoOverlays','Visuals','Colors') for item in listed if item['group']==group] + [by_symbol[name] for name in sorted(unlisted)]
    key_list = re.search(r'\bHOTKEY_LIST\s*=\s*ImmutableList\.of\((.*?)\);', key_source, re.S)
    by_symbol = {item['symbol']: item for item in hotkeys}
    hotkeys = [by_symbol[symbol.strip()] for symbol in arguments(key_list[1]) if symbol.strip()]
    if len({item['symbol'] for item in hotkeys}) != len(by_symbol):
        raise ValueError('Active hotkey list and declarations differ')
    if len(options) != 168 or len(hotkeys) != 78:
        raise ValueError(f'Upstream inventory changed: {len(options)} options, {len(hotkeys)} hotkeys; review before updating')
    languages = {}
    resources = [(WORKSPACE / 'work/malilib/src/main/resources/assets/malilib/lang'),
                 WORKSPACE / 'Litematica/src/main/resources/assets/litematica/lang']
    for directory in resources:
        for file in sorted(directory.glob('*.json')):
            languages.setdefault(file.stem, {}).update(json.loads(file.read_text(encoding='utf-8')))
    return dict(upstream='Litematica eb15b39; MaLiLib source checkout', options=options, hotkeys=hotkeys, languages=languages)


def generated_header(data):
    packed = zlib.compress(json.dumps(data, ensure_ascii=False, separators=(',', ':')).encode('utf-8'), 9)
    plain = json.dumps(data, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    rows = [','.join(str(v) for v in packed[i:i+32]) for i in range(0, len(packed), 32)]
    return ('#pragma once\n// Generated from LGPL-3.0-or-later Litematica/MaLiLib sources. Do not hand edit.\n'
            '// Regenerate with python tools/litematica/import_sources.py\n'
            '#include <cstddef>\nnamespace lholo::config::upstream {\n'
            f'inline constexpr std::size_t plainSize={len(plain)};\n'
            'inline constexpr unsigned char compressed[]={\n'+',\n'.join(rows)+'\n};\n}\n')


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--check', action='store_true'); args = parser.parse_args()
    data = catalog(); destination = ROOT / 'src/config/UpstreamData.h'; header = generated_header(data)
    if args.check:
        if not destination.exists() or destination.read_text(encoding='utf-8') != header:
            raise SystemExit('Upstream catalog/resources differ; regenerate and review')
    else:
        destination.parent.mkdir(parents=True, exist_ok=True); destination.write_text(header, encoding='utf-8')
        inventory = WORKSPACE / 'work/litematica-source-catalog.json'
        inventory.write_text(json.dumps({k:v for k,v in data.items() if k!='languages'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        # This is an inventory for review, NOT a claim that similarly named native hooks are equivalent.
        mappings = []
        mixin_root = JAVA / 'fi/dy/masa/litematica/mixin'
        for file in sorted(mixin_root.rglob('*.java')):
            text = uncomment(file.read_text(encoding='utf-8'))
            targets = re.findall(r'@Mixin\s*\((.*?)\)', text, re.S)
            methods = re.findall(r'method\s*=\s*"([^"]+)"', text)
            mappings.append(dict(source=str(file.relative_to(JAVA)).replace('\\','/'),
                targets=targets, methods=methods, status='pending semantic hook review',
                nativeEvidence=[], gameVerified=False))
        (WORKSPACE/'work/litematica-hooks-map.json').write_text(json.dumps(mappings,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f"Verified {len(data['options'])} active options, {len(data['hotkeys'])} hotkeys, {len(data['languages'])} upstream locales")


if __name__ == '__main__': main()
