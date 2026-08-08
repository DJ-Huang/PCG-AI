#!/usr/bin/env python3
"""Deterministically relayout a PCG graph and each inline Subgraph definition.

The operation is intentionally limited to node ``position.x/y``.  Node data,
ids, edges, parameters, and Subgraph interfaces are left untouched.

The layout is Houdini-style: topological depth becomes the vertical row,
independent source components become horizontal lanes, multi-input joins are
centered over their source lanes, and same-row collisions are separated by a
safe title spacing.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


DEFAULT_ROW_STEP = 160
DEFAULT_COL_STEP = 360
DEFAULT_ORIGIN_X = 200
DEFAULT_ORIGIN_Y = 0


class LayoutError(ValueError):
    """Raised when the graph cannot be safely laid out as a DAG."""


def _as_number(value: object, fallback: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return fallback


def _clean_number(value: float) -> int | float:
    rounded = round(value)
    if abs(value - rounded) < 1e-6:
        return int(rounded)
    return round(value, 4)


def _original_position(node: dict) -> tuple[float, float, str]:
    position = node.get("position")
    if not isinstance(position, dict):
        position = {}
    return (
        _as_number(position.get("x")),
        _as_number(position.get("y")),
        str(node.get("id", "")),
    )


def _validate_scope_nodes(nodes: object, scope: str) -> tuple[list[dict], dict[str, dict]]:
    if not isinstance(nodes, list):
        raise LayoutError(f"{scope}: nodes must be an array")
    typed_nodes = [node for node in nodes if isinstance(node, dict)]
    if len(typed_nodes) != len(nodes):
        raise LayoutError(f"{scope}: every node must be an object")

    by_id: dict[str, dict] = {}
    for node in typed_nodes:
        node_id = node.get("id")
        if not isinstance(node_id, str) or not node_id:
            raise LayoutError(f"{scope}: every node needs a non-empty string id")
        if node_id in by_id:
            raise LayoutError(f"{scope}: duplicate node id {node_id!r}")
        by_id[node_id] = node
    return typed_nodes, by_id


def _build_adjacency(
    edges: object,
    by_id: dict[str, dict],
    scope: str,
) -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    if not isinstance(edges, list):
        raise LayoutError(f"{scope}: edges must be an array")

    incoming = {node_id: [] for node_id in by_id}
    outgoing = {node_id: [] for node_id in by_id}
    for edge in edges:
        if not isinstance(edge, dict):
            raise LayoutError(f"{scope}: every edge must be an object")
        source = edge.get("source")
        target = edge.get("target")
        if source not in by_id or target not in by_id:
            raise LayoutError(
                f"{scope}: edge {edge.get('id', '?')!r} references an unknown node"
            )
        outgoing[source].append(target)
        incoming[target].append(source)
    return incoming, outgoing


def _topological_order(
    nodes: list[dict],
    incoming: dict[str, list[str]],
    outgoing: dict[str, list[str]],
) -> list[str]:
    """Return a stable topological order, rejecting data-flow cycles."""

    by_id = {node["id"]: node for node in nodes}
    remaining = {node_id: len(preds) for node_id, preds in incoming.items()}
    ready = sorted(
        (node_id for node_id, degree in remaining.items() if degree == 0),
        key=lambda node_id: _original_position(by_id[node_id]),
    )
    order: list[str] = []
    while ready:
        node_id = ready.pop(0)
        order.append(node_id)
        for target in outgoing[node_id]:
            remaining[target] -= 1
            if remaining[target] == 0:
                ready.append(target)
                ready.sort(key=lambda item: _original_position(by_id[item]))

    if len(order) != len(nodes):
        cyclic = sorted(
            (node_id for node_id, degree in remaining.items() if degree > 0),
            key=lambda node_id: _original_position(by_id[node_id]),
        )
        raise LayoutError(
            "graph contains a data-flow cycle; refusing to guess a layout for: "
            + ", ".join(cyclic[:8])
        )
    return order


def _layout_scope(
    nodes: object,
    edges: object,
    scope: str,
    *,
    row_step: int,
    col_step: int,
    origin_x: int,
    origin_y: int,
) -> dict[str, int | str]:
    typed_nodes, by_id = _validate_scope_nodes(nodes, scope)
    incoming, outgoing = _build_adjacency(edges, by_id, scope)
    order = _topological_order(typed_nodes, incoming, outgoing)

    source_roots: dict[str, set[str]] = {}
    depth: dict[str, int] = {}
    for node_id in order:
        predecessors = incoming[node_id]
        if not predecessors:
            source_roots[node_id] = {node_id}
            depth[node_id] = 0
            continue

        roots: set[str] = set()
        node_depth = 0
        for predecessor in predecessors:
            roots.update(source_roots[predecessor])
            node_depth = max(node_depth, depth[predecessor] + 1)
        source_roots[node_id] = roots
        depth[node_id] = node_depth

    source_ids = [node_id for node_id in order if not incoming[node_id]]
    source_ids.sort(key=lambda node_id: _original_position(by_id[node_id]))
    source_lane_x = {
        node_id: origin_x + index * col_step
        for index, node_id in enumerate(source_ids)
    }

    raw_x: dict[str, float] = {}
    raw_y: dict[str, float] = {}
    for node_id in order:
        roots = source_roots[node_id]
        if roots:
            raw_x[node_id] = sum(source_lane_x[root] for root in roots) / len(roots)
        else:
            raw_x[node_id] = origin_x
        raw_y[node_id] = origin_y + depth[node_id] * row_step

    # Resolve only same-row collisions.  The normal case is one node per lane;
    # this small adjustment provides sub-lanes for true fan-outs without
    # changing the graph's topology.
    assigned_x: dict[str, float] = {}
    by_row: dict[int, list[str]] = defaultdict(list)
    for node_id in order:
        by_row[depth[node_id]].append(node_id)
    for row in sorted(by_row):
        row_nodes = sorted(
            by_row[row],
            key=lambda node_id: (
                raw_x[node_id],
                _original_position(by_id[node_id]),
            ),
        )
        previous_x: float | None = None
        for node_id in row_nodes:
            x = raw_x[node_id]
            if previous_x is not None and x < previous_x + col_step:
                x = previous_x + col_step
            assigned_x[node_id] = x
            previous_x = x

    moved = 0
    for node in typed_nodes:
        node_id = node["id"]
        old_position = node.get("position")
        new_position = dict(old_position) if isinstance(old_position, dict) else {}
        new_position["x"] = _clean_number(assigned_x[node_id])
        new_position["y"] = _clean_number(raw_y[node_id])
        if old_position != new_position:
            moved += 1
        node["position"] = new_position

    return {
        "scope": scope,
        "nodes": len(typed_nodes),
        "sources": len(source_ids),
        "rows": len(by_row),
        "max_depth": max(depth.values(), default=0),
        "multi_input_nodes": sum(1 for node_id in order if len(incoming[node_id]) > 1),
        "moved": moved,
    }


def _selected_scopes(
    graph: dict,
    selected_subgraphs: list[str] | None,
    skip_root: bool,
) -> list[tuple[str, object, object]]:
    scopes: list[tuple[str, object, object]] = []
    if not skip_root:
        scopes.append(("root", graph.get("nodes", []), graph.get("edges", [])))

    definitions = graph.get("subgraphs") or []
    if not isinstance(definitions, list):
        raise LayoutError("subgraphs must be an array")
    definitions_by_id = {
        definition.get("id"): definition
        for definition in definitions
        if isinstance(definition, dict) and isinstance(definition.get("id"), str)
    }
    if selected_subgraphs:
        missing = [sid for sid in selected_subgraphs if sid not in definitions_by_id]
        if missing:
            raise LayoutError("unknown Subgraph id(s): " + ", ".join(missing))
        definitions = [definitions_by_id[sid] for sid in selected_subgraphs]

    for definition in definitions:
        if not isinstance(definition, dict):
            raise LayoutError("every Subgraph definition must be an object")
        subgraph_id = definition.get("id")
        if not isinstance(subgraph_id, str) or not subgraph_id:
            raise LayoutError("every Subgraph definition needs a non-empty string id")
        scopes.append(
            (
                f"subgraph:{subgraph_id}",
                definition.get("nodes", []),
                definition.get("edges", []),
            )
        )
    return scopes


def _write_graph(graph: dict, input_path: Path, output_path: Path | None, in_place: bool) -> Path | None:
    if not output_path and not in_place:
        return None

    destination = input_path if in_place else output_path
    assert destination is not None
    if destination.parent and not destination.parent.is_dir():
        raise LayoutError(f"output directory does not exist: {destination.parent}")

    serialized = json.dumps(graph, ensure_ascii=False, indent=2) + "\n"
    if in_place:
        temporary = destination.with_name(destination.name + ".layout.tmp")
        temporary.write_text(serialized, encoding="utf-8")
        temporary.replace(destination)
    else:
        destination.write_text(serialized, encoding="utf-8")
    return destination


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Relayout the root graph and inline Subgraph definitions; "
            "only node position.x/y is changed."
        )
    )
    parser.add_argument("graph", type=Path, help="input .pcg JSON file")
    parser.add_argument("--out", type=Path, help="write the relaid graph to a new path")
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="replace the input graph atomically after the layout pass",
    )
    parser.add_argument(
        "--subgraph",
        action="append",
        dest="subgraphs",
        help="layout only this inline Subgraph definition; repeatable",
    )
    parser.add_argument(
        "--skip-root",
        action="store_true",
        help="do not relayout root nodes (useful with --subgraph)",
    )
    parser.add_argument("--row-step", type=int, default=DEFAULT_ROW_STEP)
    parser.add_argument("--col-step", type=int, default=DEFAULT_COL_STEP)
    parser.add_argument("--origin-x", type=int, default=DEFAULT_ORIGIN_X)
    parser.add_argument("--origin-y", type=int, default=DEFAULT_ORIGIN_Y)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    if args.out and args.in_place:
        parser.error("use either --out or --in-place, not both")
    if args.row_step <= 0 or args.col_step < 320:
        parser.error("--row-step must be positive and --col-step must be at least 320")
    if not args.graph.is_file():
        print(f"ERROR: file not found: {args.graph}", file=sys.stderr)
        return 1

    try:
        graph = json.loads(args.graph.read_text(encoding="utf-8"))
        if not isinstance(graph, dict):
            raise LayoutError("graph root must be a JSON object")
        scopes = _selected_scopes(graph, args.subgraphs, args.skip_root)
        reports = [
            _layout_scope(
                nodes,
                edges,
                scope,
                row_step=args.row_step,
                col_step=args.col_step,
                origin_x=args.origin_x,
                origin_y=args.origin_y,
            )
            for scope, nodes, edges in scopes
        ]
        destination = _write_graph(graph, args.graph, args.out, args.in_place)
    except (OSError, json.JSONDecodeError, LayoutError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    total_nodes = sum(int(report["nodes"]) for report in reports)
    total_moved = sum(int(report["moved"]) for report in reports)
    output = str(destination) if destination else "dry-run"
    print(
        f"layout_pcg.py: PASS | scopes={len(reports)} | nodes={total_nodes} | "
        f"moved={total_moved} | output={output}"
    )
    for report in reports:
        print(
            f"  {report['scope']}: nodes={report['nodes']} sources={report['sources']} "
            f"rows={report['rows']} maxDepth={report['max_depth']} "
            f"multiInput={report['multi_input_nodes']} moved={report['moved']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
