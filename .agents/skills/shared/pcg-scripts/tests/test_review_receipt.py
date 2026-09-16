import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from _shared.review_receipt import digest, finite_score, validate_evaluation, validate_receipts


class ReviewReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.graph_path = self.root / 'asset.pcg'
        self.png = self.root / 'front.png'
        self.png.write_bytes(b'\x89PNG\r\n\x1a\nfixture')
        self.graph = {'nodes': [{'id': 'bool', 'type': 'BooleanMesh', 'data': {'triangleBudget': 100}}], 'edges': []}
        self.graph_path.write_text(json.dumps(self.graph))
        self.evaluation = {
            'version': 1, 'fullResolution': True, 'seed': 42,
            'graph': copy.deepcopy(self.graph), 'sourceGraph': copy.deepcopy(self.graph),
            'cook': {'diagnostics_version': 1, 'evaluation_seed': 42,
                     'evaluation_graph': copy.deepcopy(self.graph), 'cook_outcome': 'success',
                     'fallback_used': False, 'execution_acceptable': True,
                     'node_stats': [{'node_id': 'bool', 'node_type': 'BooleanMesh'}],
                     'node_diagnostics': [{'node_id': 'bool', 'node_type': 'BooleanMesh',
                                           'outcome': 'empty', 'fallback_used': False}]}}
        self.receipt = {'version': 1, 'source': {'path': str(self.graph_path), 'sha256': digest(self.graph_path)},
                        'views': [{'viewId': 'front', 'evaluation': self.evaluation, 'camera': {'view': 'front'},
                                   'render': {'width': 1280, 'height': 720},
                                   'screenshot': {'path': str(self.png), 'sha256': digest(self.png)}}]}
        self.features = [{'featureId': 'silhouette', 'status': 'matched', 'evidence': 'Compared front outline'}]

    def validate(self):
        path = self.root / 'receipt.json'
        path.write_text(json.dumps(self.receipt))
        return validate_receipts(path, [str(self.png)], self.features)

    def test_valid_empty_does_not_mean_fallback(self):
        self.assertFalse(self.validate()['visualFidelityIndependentlyMeasured'])

    def test_scalar_nan_inf_and_bool_rejected(self):
        for value in [float('nan'), float('inf'), float('-inf'), True, '0.99']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                finite_score(value)

    def test_legacy_cook_blocked(self):
        self.evaluation['cook'].pop('diagnostics_version')
        with self.assertRaises(ValueError): self.validate()

    def test_degraded_blocked(self):
        self.evaluation['cook']['cook_outcome'] = 'degraded'
        with self.assertRaises(ValueError): self.validate()

    def test_hidden_earlier_loop_fallback_blocked(self):
        self.evaluation['cook']['node_diagnostics'].insert(0, {'node_id': 'bool', 'outcome': 'degraded', 'fallback_used': True})
        with self.assertRaises(ValueError): self.validate()

    def test_missing_boolean_diagnostic_blocked(self):
        self.evaluation['cook']['node_diagnostics'] = []
        with self.assertRaises(ValueError): self.validate()

    def test_zero_iteration_body_needs_no_fabricated_receipt(self):
        self.evaluation['cook']['node_diagnostics'] = []
        self.evaluation['cook']['node_stats'] = []
        self.validate()

    def test_stale_cook_graph_blocked(self):
        self.evaluation['cook']['evaluation_graph']['nodes'][0]['data']['triangleBudget'] = 50
        with self.assertRaises(ValueError): self.validate()

    def test_different_seed_blocked(self):
        self.evaluation['cook']['evaluation_seed'] = 41
        with self.assertRaises(ValueError): self.validate()

    def test_changed_disk_graph_blocked(self):
        self.graph_path.write_text('{}')
        with self.assertRaises(ValueError): self.validate()

    def test_changed_png_blocked(self):
        self.png.write_bytes(b'changed')
        with self.assertRaises(ValueError): self.validate()

    def test_parameter_drift_blocked(self):
        self.evaluation['sourceGraph']['parameters'] = [{'id': 'budget', 'targetNode': 'bool',
            'targetProperty': 'triangleBudget', 'default': 50, 'exposed': True}]
        with self.assertRaises(ValueError): validate_evaluation(self.evaluation)

    def test_partial_quality_blocked(self):
        self.evaluation['fullResolution'] = False
        with self.assertRaises(ValueError): self.validate()

    def test_missing_camera_blocked(self):
        self.receipt['views'][0]['camera'] = None
        with self.assertRaises(ValueError): self.validate()

    def test_scalar_only_review_blocked(self):
        self.features = []
        with self.assertRaises(ValueError): self.validate()

    def test_unresolved_required_feature_blocked(self):
        self.features[0]['status'] = 'mismatch'
        with self.assertRaises(ValueError): self.validate()

    def test_duplicate_feature_ids_blocked(self):
        self.features *= 2
        with self.assertRaises(ValueError): self.validate()

    def test_different_graph_across_views_blocked(self):
        second = copy.deepcopy(self.receipt['views'][0])
        second['evaluation']['seed'] = second['evaluation']['cook']['evaluation_seed'] = 43
        self.receipt['views'].append(second)
        with self.assertRaises(ValueError): self.validate()

    def test_default_handles_compare_equal(self):
        graph = self.evaluation['graph']
        graph['edges'] = [{'id': 'e', 'source': 'bool', 'target': 'bool'}]
        self.evaluation['cook']['evaluation_graph']['edges'] = [
            {'source': 'bool', 'target': 'bool', 'sourceHandle': 'out', 'targetHandle': 'in'}]
        validate_evaluation(self.evaluation)


if __name__ == '__main__':
    unittest.main()
