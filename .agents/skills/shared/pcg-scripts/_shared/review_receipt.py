"""Evidence consistency checks, not a pixel scorer or a tamper-proof attestation."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any


def finite_score(value: Any) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Scores must be finite numbers')
    return max(0.0, min(1.0, float(value)))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def graph_inputs(graph: dict) -> dict:
    """Project the same node/edge inputs as native CookDiagnostics.

    Flattened subgraphs must be captured as a flattened effective graph. Do not
    guess their expansion here; a mismatch is a blocker, never a silent pass.
    """
    nodes = [{key: node.get(key, {} if key == 'data' else '')
              for key in ('id', 'type', 'data')} for node in graph['nodes']]
    if len({node['id'] for node in nodes}) != len(nodes):
        raise ValueError('Duplicate graph node IDs')
    edges = [{
        'source': edge['source'], 'target': edge['target'],
        'sourceHandle': edge.get('sourceHandle') or 'out',
        'targetHandle': edge.get('targetHandle') or 'in',
    } for edge in graph['edges']]
    return {'nodes': sorted(nodes, key=lambda n: n['id']),
            'edges': sorted(edges, key=lambda e: tuple(e[k] for k in sorted(e)))}


def validate_evaluation(evidence: dict) -> None:
    if evidence.get('version') != 1 or evidence.get('fullResolution') is not True:
        raise ValueError('Missing evaluation evidence or not full resolution')
    graph, source, cook = evidence['graph'], evidence['sourceGraph'], evidence['cook']
    if (cook.get('diagnostics_version') != 1 or cook.get('cook_outcome') != 'success'
            or cook.get('execution_acceptable') is not True or cook.get('fallback_used') is not False):
        raise ValueError('Unverified, failed, or degraded cook cannot pass visual acceptance')
    if cook.get('evaluation_seed') != evidence.get('seed'):
        raise ValueError('Cook seed does not match evaluation')
    if graph_inputs(graph) != graph_inputs(cook['evaluation_graph']):
        raise ValueError('Stale cook: native effective graph differs from captured graph')
    diagnostics = cook.get('node_diagnostics')
    if not isinstance(diagnostics, list):
        raise ValueError('Missing native node diagnostics')
    stats = cook.get('node_stats')
    if not isinstance(stats, list):
        raise ValueError('Missing execution node statistics')
    # Zero-iteration ForEach bodies intentionally have no node output.
    executed = {item.get('node_id') for item in stats}
    expected = {n['id'] for n in cook['evaluation_graph']['nodes']
                if n['type'] == 'BooleanMesh' and n['id'] in executed}
    seen = set()
    for item in diagnostics:
        if (not isinstance(item, dict) or item.get('fallback_used') is not False
                or item.get('outcome') not in ('success', 'noop', 'empty')):
            raise ValueError('A Boolean failure/fallback is not acceptable, including earlier loop iterations')
        if item.get('node_type') == 'BooleanMesh':
            seen.add(item.get('node_id'))
    if expected - seen:
        raise ValueError(f'Boolean nodes lack diagnostics: {sorted(expected - seen)}')
    for document in (source, graph):
        nodes = {node['id']: node for node in document['nodes']}
        targets = set()
        for parameter in document.get('parameters', []):
            if not parameter.get('exposed') or not parameter.get('targetNode') or not parameter.get('targetProperty'):
                continue
            target = (parameter['targetNode'], parameter['targetProperty'])
            if target in targets:
                raise ValueError('Multiple exposed parameters target the same property')
            targets.add(target)
            if target[0] not in nodes:
                raise ValueError('Parameter targets a missing node')
            data = nodes[target[0]].get('data', {})
            if target[1] in data and data[target[1]] != parameter['default']:
                raise ValueError(f'Parameter default/baked drift: {parameter["id"]}')


def validate_receipts(path: Path, render_paths: list[str], feature_reviews: Any,
                      view_evidence: list[dict] | None = None) -> dict:
    """Validate artifacts at acceptance time, not just when they were captured."""
    path = path.expanduser().resolve()
    receipt = json.loads(path.read_text(encoding='utf-8'))
    if receipt.get('version') != 1:
        raise ValueError('Unsupported review receipt version')
    graph_path = Path(receipt['source']['path'])
    if digest(graph_path) != receipt['source']['sha256']:
        raise ValueError('Graph changed on disk after capture; recook and recapture')
    source = json.loads(graph_path.read_text(encoding='utf-8'))
    views = receipt.get('views')
    if not isinstance(views, list) or not views:
        raise ValueError('Receipt contains no captured views')
    by_path = {}
    baseline = None
    for view in views:
        validate_evaluation(view['evaluation'])
        if view['evaluation']['sourceGraph'] != source:
            raise ValueError('Captured editor source differs from the graph on disk')
        current = (view['evaluation']['graph'], view['evaluation']['seed'])
        if baseline is not None and current != baseline:
            raise ValueError('Required views were captured with different effective parameters or graphs')
        baseline = current
        screenshot = Path(view['screenshot']['path']).resolve()
        if digest(screenshot) != view['screenshot']['sha256']:
            raise ValueError('Screenshot changed after capture')
        if not screenshot.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Capture is not a PNG')
        if not view.get('camera') or not view.get('render'):
            raise ValueError('Missing camera/render settings')
        if str(screenshot) in by_path:
            raise ValueError('Duplicate capture path')
        by_path[str(screenshot)] = view
    if len(set(str(Path(p).expanduser().resolve()) for p in render_paths)) != len(render_paths):
        raise ValueError('Required views cannot reuse the same screenshot')
    for item in view_evidence or []:
        captured = by_path.get(str(Path(item['renderScreenshot']).expanduser().resolve()))
        if captured is None or item.get('cameraReceipt') != captured['camera']:
            raise ValueError('Review cameraReceipt does not match the captured camera')
    for render in render_paths:
        if str(Path(render).expanduser().resolve()) not in by_path:
            raise ValueError(f'Review render is not bound to this receipt: {render}')
    if not render_paths:
        raise ValueError('No review render supplied')
    if not isinstance(feature_reviews, list) or not feature_reviews:
        raise ValueError('Visual continue requires named --feature-reviews-json, not only a scalar score')
    features = set()
    required_count = 0
    for feature in feature_reviews:
        if not isinstance(feature, dict):
            raise ValueError('Each feature review must be an object')
        name = str(feature.get('featureId', '')).strip()
        notes = str(feature.get('evidence', '')).strip()
        status = feature.get('status')
        if not name or name in features or not notes or status not in ('matched', 'mismatch', 'not-applicable'):
            raise ValueError('Features need unique featureId, status, and non-empty evidence')
        features.add(name)
        if feature.get('required', True) is not False:
            required_count += 1
            if status != 'matched':
                raise ValueError(f'Required feature is unresolved: {name}')
    if not required_count:
        raise ValueError('At least one required feature must be reviewed')
    # Small durable link; the receipt retains the actual graph and runtime data.
    return {'path': str(path), 'sha256': digest(path), 'source': receipt['source'],
            'captureCount': len(views), 'scoreSource': 'agent-asserted',
            'visualFidelityIndependentlyMeasured': False}
