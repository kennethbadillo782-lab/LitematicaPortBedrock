import unittest
import import_sources as source


class ImportSourceTests(unittest.TestCase):
    def test_comments_are_not_options(self):
        text='"https://example.test/a" // removed\n/* block */ ConfigBoolean("live", true)'
        self.assertEqual(source.uncomment(text), '"https://example.test/a"  \n  ConfigBoolean("live", true)')

    def test_constructor_arguments_respect_strings_and_nesting(self):
        self.assertEqual(source.arguments('"a,b", Factory.of(1, 2), "x\\\"y,z", true'),
                         ['"a,b"', 'Factory.of(1, 2)', '"x\\\"y,z"', 'true'])

    def test_complete_active_catalog_and_native_presets(self):
        data=source.catalog()
        options={o['name']:o for o in data['options']}
        self.assertEqual(len(options),168)
        self.assertEqual(data['options'][0]['name'],'placementManagerThreadCount')
        self.assertEqual([o['name'] for o in data['options'] if o['group']=='Visuals'][:3],['enableRendering','enableSchematicRendering','enableAreaSelectionBoxesRendering'])
        self.assertNotIn('materialListContainerScan',options)
        self.assertFalse(options['loadEntireSchematics']['listed'])
        self.assertEqual(sum(o['listed'] for o in data['options']),167)
        self.assertEqual(options['commandLimitPerTick']['default'],8)
        self.assertEqual((options['commandLimitPerTick']['min'],options['commandLimitPerTick']['max']),(1,256))
        self.assertEqual(options['selectionCornersMode']['choices'],['corners','expand'])
        self.assertEqual(options['enableRendering']['settings']['activate_on'],'PRESS')
        self.assertTrue(options['enableRendering']['settings']['cancel'])
        hotkeys={k['name']:k for k in data['hotkeys']}
        self.assertEqual(len(hotkeys),78)
        self.assertEqual(hotkeys['openGuiMainMenu']['settings']['activate_on'],'RELEASE')
        self.assertTrue(hotkeys['openGuiMainMenu']['settings']['exclusive'])
        self.assertEqual(hotkeys['openGuiSchematicVerifier']['keys'],'M,V')
        self.assertIn('Principal',data['languages']['es_es'][hotkeys['openGuiMainMenu']['labelKey']])


if __name__=='__main__': unittest.main()
