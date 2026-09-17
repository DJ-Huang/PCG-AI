#!/usr/bin/env python3
"""Offline JSON Schema/ordinary graph conformance tests for issue #22.

The shared corpus distinguishes structural JSON Schema checks from semantic
checks (ID uniqueness, primary-output references, cross-field constraints, etc.)
that are exercised by test-custom-function-contract.mjs. No JavaScript is run.
"""
from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
except ImportError:
    raise SystemExit('Install the test dependency: python -m pip install "jsonschema>=4,<5"')

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / 'schema/custom-function-schema-v1.json').read_text(encoding='utf-8'))
GRAPH_SCHEMA = json.loads((ROOT / 'schema/graph-schema-v2.json').read_text(encoding='utf-8'))
FIXTURE = json.loads((ROOT / 'schema/fixtures/custom-function-v1.pcg').read_text(encoding='utf-8'))
CASES = json.loads((ROOT / 'schema/fixtures/custom-function-cases.json').read_text(encoding='utf-8'))


def contract() -> dict:
    return copy.deepcopy(FIXTURE['nodes'][0]['data']['customFunction'])


class CustomFunctionSchemaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.validator = Draft202012Validator(SCHEMA)
        self.graph_validator = Draft202012Validator(GRAPH_SCHEMA)

    def test_meta_schema(self) -> None:
        Draft202012Validator.check_schema(SCHEMA)

    def test_fixture_is_an_ordinary_zero_edge_graph(self) -> None:
        self.graph_validator.validate(FIXTURE)
        self.validator.validate(contract())
        self.assertEqual(FIXTURE['edges'], [])
        self.assertEqual(contract()['inputs'], [])
        self.assertGreater(len(contract()['outputs']), 1)

    def test_normal_pin_types_not_a_second_geometry_type_system(self) -> None:
        self.assertCountEqual(SCHEMA['$defs']['pinType']['enum'], GRAPH_SCHEMA['$defs']['pinType']['enum'])

    def test_shared_structural_and_semantic_corpus(self) -> None:
        for case in CASES:
            with self.subTest(case=case['name']):
                value = contract()
                for change in case['changes']:
                    parent = value
                    for key in change['path'][:-1]:
                        parent = parent[key]
                    key = change['path'][-1]
                    if change.get('remove'):
                        del parent[key]
                    else:
                        parent[key] = copy.deepcopy(change['value'])
                errors = list(self.validator.iter_errors(value))
                self.assertEqual(not errors, case['schemaValid'], '\n'.join(error.message for error in errors))
                # Semantic-invalid/structural-valid cases are intentional: JSON
                # Schema alone cannot assert uniqueness by a specific ID field.
                self.assertFalse(case['contractValid'] and not case['schemaValid'])

    def test_typed_fan_out_uses_existing_edge_fields(self) -> None:
        graph = copy.deepcopy(FIXTURE)
        for index in range(2):
            graph['nodes'].append({'id': f'sink{index}', 'type': 'Output', 'position': {'x': 300, 'y': index * 100}, 'data': {}})
            graph['edges'].append({'id': f'e{index}', 'source': 'script1', 'target': f'sink{index}', 'sourceHandle': 'body', 'targetHandle': 'in', 'sourcePinType': 'SpatialGeometry', 'targetPinType': 'Any'})
        self.graph_validator.validate(graph)
        self.assertEqual(graph, json.loads(json.dumps(graph, ensure_ascii=False)))
        renamed = copy.deepcopy(graph)
        c = renamed['nodes'][0]['data']['customFunction']
        c['outputs'].reverse()
        next(port for port in c['outputs'] if port['id'] == 'body')['label'] = 'Renamed body'
        self.validator.validate(c)
        self.graph_validator.validate(renamed)
        self.assertEqual(renamed['edges'], graph['edges'])

    def test_inline_subgraph_retains_ordinary_single_output_boundary(self) -> None:
        graph = copy.deepcopy(FIXTURE)
        definition = {'id': 'script-asset', 'name': 'Script asset', 'inputs': [], 'outputs': [{'id': 'out', 'name': 'Result', 'pinType': 'SpatialGeometry'}], 'nodes': copy.deepcopy(graph['nodes']), 'edges': []}
        definition['nodes'].append({'id': 'output', 'type': 'Output', 'position': {'x': 400, 'y': 100}, 'data': {}})
        definition['edges'].append({'id': 'inside', 'source': 'script1', 'target': 'output', 'sourceHandle': 'body', 'targetHandle': 'in'})
        graph['nodes'] = [
            {'id': 'instance-a', 'type': 'Subgraph', 'position': {'x': 0, 'y': 0}, 'data': {'subgraphId': 'script-asset'}},
            {'id': 'instance-b', 'type': 'Subgraph', 'position': {'x': 0, 'y': 100}, 'data': {'subgraphId': 'script-asset'}},
        ]
        graph['subgraphs'] = [definition]
        self.graph_validator.validate(graph)
        reopened = json.loads(json.dumps(graph, ensure_ascii=False))
        self.validator.validate(reopened['subgraphs'][0]['nodes'][0]['data']['customFunction'])
        self.assertEqual(graph, reopened)
        self.assertEqual(len(reopened['subgraphs']), 1)
        self.assertEqual(reopened['nodes'][0]['data']['subgraphId'], reopened['nodes'][1]['data']['subgraphId'])

    def test_script_source_is_opaque_data_including_infinite_loops(self) -> None:
        for source in ['while (true) {}', 'not JavaScript', '\ufeff// 中文\r\nexport function main() {}\r\n', '']:
            with self.subTest(source=source):
                value = contract()
                value['source'] = source
                self.validator.validate(value)
                reopened = json.loads(json.dumps(value, ensure_ascii=False))
                self.assertEqual(reopened['source'], source)

    def test_no_external_schema_references_are_needed_for_inspection(self) -> None:
        def walk(value: object) -> None:
            if isinstance(value, dict):
                if '$ref' in value:
                    self.assertTrue(value['$ref'].startswith('#/'), value['$ref'])
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)
        walk(SCHEMA)


if __name__ == '__main__':
    print(f'Custom Function JSON Schema: {len(CASES)} shared conformance cases', flush=True)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CustomFunctionSchemaTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
