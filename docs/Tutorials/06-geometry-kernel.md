# Geometry Kernel

The geometry kernel provides editable topology, named groups, triangulation, sweeps, booleans, bevels, subdivision, and robust predicates. Algorithms operate on canonical native data rather than Unity or browser objects.

Geometry tests should cover manifold and boundary invariants, group propagation, attribute-domain preservation, normals, UVs, degeneracy, and determinism. Boolean and bevel changes need chained regression cases because individually valid operations can fail when composed.

Use real tolerances with explicit units. Keep positional, angular, and UV tolerances separate, and validate rendered output when the change affects shading or silhouette.
