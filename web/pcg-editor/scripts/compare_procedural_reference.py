#!/usr/bin/env python3
"""Six-view, pixel-space acceptance for GLB → procedural reconstruction.

The two input directories must contain matching ``<view>.png`` files. Alpha is
used as the silhouette when present; opaque captures fall back to a background
colour estimated from the four corners. The report deliberately keeps geometry
and appearance metrics separate.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


DEFAULT_VIEWS = (
    "azimuth-000",
    "azimuth-p035",
    "azimuth-m035",
    "azimuth-p090",
    "azimuth-p145",
    "azimuth-p180",
)


def load_rgba(path: Path) -> np.ndarray:
    if not path.is_file():
        raise FileNotFoundError(f"capture is missing: {path}")
    with Image.open(path) as image:
        return np.asarray(image.convert("RGBA"), dtype=np.uint8)


def silhouette(pixels: np.ndarray, threshold: int) -> tuple[np.ndarray, str, list[int]]:
    alpha = pixels[:, :, 3]
    if np.any(alpha < 250):
        return alpha > 8, "alpha", [0, 0, 0]
    rgb = pixels[:, :, :3].astype(np.int16)
    corners = np.stack((rgb[0, 0], rgb[0, -1], rgb[-1, 0], rgb[-1, -1]))
    background = np.median(corners, axis=0).astype(np.int16)
    mask = np.max(np.abs(rgb - background[None, None, :]), axis=2) > threshold
    return mask, "corner-background", background.astype(int).tolist()


def boundary(mask: np.ndarray) -> np.ndarray:
    kernel = np.ones((3, 3), dtype=np.uint8)
    eroded = cv2.erode(mask.astype(np.uint8), kernel, iterations=1).astype(bool)
    return mask & ~eroded


def boundary_distance(mask_a: np.ndarray, mask_b: np.ndarray) -> tuple[float, float, float, float]:
    edge_a = boundary(mask_a)
    edge_b = boundary(mask_b)
    if not np.any(edge_a) or not np.any(edge_b):
        return float("inf"), float("inf"), float("inf"), float("inf")
    distance_to_a = cv2.distanceTransform((~edge_a).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    distance_to_b = cv2.distanceTransform((~edge_b).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    a_to_b = distance_to_b[edge_a]
    b_to_a = distance_to_a[edge_b]
    symmetric = np.concatenate((a_to_b, b_to_a))
    return (
        float(np.mean(symmetric)),
        float(np.percentile(symmetric, 95)),
        float(np.mean(a_to_b)),
        float(np.mean(b_to_a)),
    )


def surface_noise(pixels: np.ndarray, mask: np.ndarray) -> float:
    rgb = pixels[:, :, :3].astype(np.float32)
    luma = rgb[:, :, 0] * 0.2126 + rgb[:, :, 1] * 0.7152 + rgb[:, :, 2] * 0.0722
    laplacian = np.abs(cv2.Laplacian(luma, cv2.CV_32F, ksize=1))
    return float(np.mean(laplacian[mask])) if np.any(mask) else 0.0


def write_overlay(path: Path, reference: np.ndarray, candidate: np.ndarray,
                  mask_reference: np.ndarray, mask_candidate: np.ndarray) -> None:
    height, width = mask_reference.shape
    overlay = np.zeros((height, width, 3), dtype=np.uint8)
    both = mask_reference & mask_candidate
    overlay[both] = (235, 235, 235)
    overlay[mask_reference & ~mask_candidate] = (255, 70, 70)
    overlay[mask_candidate & ~mask_reference] = (40, 220, 255)
    separator = np.full((height, 4, 3), 24, dtype=np.uint8)
    sheet = np.concatenate((reference[:, :, :3], separator, candidate[:, :, :3], separator, overlay), axis=1)
    Image.fromarray(sheet, mode="RGB").save(path)


def compare_view(reference_path: Path, candidate_path: Path, threshold: int,
                 overlay_path: Path | None) -> dict[str, object]:
    reference = load_rgba(reference_path)
    candidate = load_rgba(candidate_path)
    if reference.shape != candidate.shape:
        raise ValueError(
            f"capture dimensions differ: {reference_path.name} {reference.shape[1]}x{reference.shape[0]} "
            f"vs {candidate_path.name} {candidate.shape[1]}x{candidate.shape[0]}"
        )
    mask_ref, mask_mode_ref, background_ref = silhouette(reference, threshold)
    mask_candidate, mask_mode_candidate, background_candidate = silhouette(candidate, threshold)
    intersection = mask_ref & mask_candidate
    union = mask_ref | mask_candidate
    iou = float(np.count_nonzero(intersection) / max(np.count_nonzero(union), 1))
    if np.any(intersection):
        colour_error = float(np.mean(np.abs(
            reference[:, :, :3].astype(np.float32)[intersection]
            - candidate[:, :, :3].astype(np.float32)[intersection]
        )) / 255.0)
    else:
        colour_error = 1.0
    distance_mean, distance_p95, ref_to_candidate, candidate_to_ref = boundary_distance(
        mask_ref, mask_candidate
    )
    if overlay_path is not None:
        write_overlay(overlay_path, reference, candidate, mask_ref, mask_candidate)
    return {
        "width": int(reference.shape[1]),
        "height": int(reference.shape[0]),
        "silhouetteIou": iou,
        "boundaryDistanceMeanPx": distance_mean,
        "boundaryDistanceP95Px": distance_p95,
        "referenceToCandidateMeanPx": ref_to_candidate,
        "candidateToReferenceMeanPx": candidate_to_ref,
        "meanRgbErrorIntersection": colour_error,
        "laplacianNoiseReference": surface_noise(reference, mask_ref),
        "laplacianNoiseCandidate": surface_noise(candidate, mask_candidate),
        "referencePixels": int(np.count_nonzero(mask_ref)),
        "candidatePixels": int(np.count_nonzero(mask_candidate)),
        "maskModeReference": mask_mode_ref,
        "maskModeCandidate": mask_mode_candidate,
        "backgroundReference": background_ref,
        "backgroundCandidate": background_candidate,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference_dir", type=Path)
    parser.add_argument("candidate_dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--overlays", type=Path)
    parser.add_argument("--views", nargs="+", default=list(DEFAULT_VIEWS))
    parser.add_argument("--background-threshold", type=int, default=12)
    parser.add_argument("--min-iou", type=float, default=0.98)
    parser.add_argument("--max-boundary-p95", type=float, default=2.0)
    parser.add_argument("--allow-fail", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.overlays:
        args.overlays.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []
    for view in args.views:
        metrics = compare_view(
            args.reference_dir / f"{view}.png",
            args.candidate_dir / f"{view}.png",
            args.background_threshold,
            args.overlays / f"{view}-comparison.png" if args.overlays else None,
        )
        passed = (
            float(metrics["silhouetteIou"]) >= args.min_iou
            and float(metrics["boundaryDistanceP95Px"]) <= args.max_boundary_p95
        )
        row = {"view": view, **metrics, "passed": passed}
        rows.append(row)
        print(
            f"{view:16} IoU {metrics['silhouetteIou']:.5f} | "
            f"boundary mean/p95 {metrics['boundaryDistanceMeanPx']:.3f}/{metrics['boundaryDistanceP95Px']:.3f}px | "
            f"RGB {metrics['meanRgbErrorIntersection']:.5f} | {'PASS' if passed else 'FAIL'}"
        )

    report = {
        "schemaVersion": "pcg-procedural-pixel-comparison/v1",
        "referenceDir": str(args.reference_dir),
        "candidateDir": str(args.candidate_dir),
        "thresholds": {
            "minimumSilhouetteIou": args.min_iou,
            "maximumBoundaryDistanceP95Px": args.max_boundary_p95,
        },
        "views": rows,
        "aggregate": {
            "meanSilhouetteIou": float(np.mean([row["silhouetteIou"] for row in rows])),
            "worstSilhouetteIou": float(min(row["silhouetteIou"] for row in rows)),
            "meanBoundaryDistancePx": float(np.mean([row["boundaryDistanceMeanPx"] for row in rows])),
            "worstBoundaryDistanceP95Px": float(max(row["boundaryDistanceP95Px"] for row in rows)),
            "meanRgbErrorIntersection": float(np.mean([row["meanRgbErrorIntersection"] for row in rows])),
            "passed": all(bool(row["passed"]) for row in rows),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(
        f"aggregate        IoU mean/worst {report['aggregate']['meanSilhouetteIou']:.5f}/"
        f"{report['aggregate']['worstSilhouetteIou']:.5f} | boundary worst p95 "
        f"{report['aggregate']['worstBoundaryDistanceP95Px']:.3f}px | "
        f"{'FINAL_ACCEPTED' if report['aggregate']['passed'] else 'REWORK'}"
    )
    return 0 if report["aggregate"]["passed"] or args.allow_fail else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"comparison failed: {error}", file=sys.stderr)
        raise SystemExit(3)
