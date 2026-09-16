# Cable Generator

The Cable Generator subgraph creates a cable mesh along an input spline.

## Input

- `path`: the spline that controls the cable centreline.

## Output

- `mesh`: the generated cable mesh.

## Parameters

Use the metadata and subgraph interface for the current parameter names and defaults. Typical controls include radius, radial segments, path resampling, and optional sag or noise.

## Internal structure

The subgraph resamples the path when required, builds a stable frame, sweeps a circular profile, and outputs the resulting mesh. Frame continuity is important on tight curves because abrupt normal changes can twist the section.

## Limits

The generator models geometry, not physical cable simulation. Very small radii, self-intersecting paths, or insufficient path resolution can produce poor topology. Validate silhouette, normals, UV continuity, and scale in the target client.
