# Attribute Wrangle and Blast

Attribute Wrangle computes or updates values on a declared attribute domain. Blast removes selected elements or keeps only the selection, depending on its explicit mode.

Always state the domain, input attributes, output type, and behavior for missing values. Keep expressions deterministic and bounded. After topology changes, verify that referenced groups and attributes still exist on the expected domain.

For workflows such as a suspension bridge, wrangle can derive hanger length or orientation from sampled points, while Blast removes construction-only elements before output. These nodes complement explicit topology; they should not hide a missing structural step.
