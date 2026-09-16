# Data Model and Binary Protocols

`PcgDataCollection` carries typed values between nodes. Editable geometry retains points, faces, groups, and attributes; render mesh data contains triangulated buffers; point, spline, heightfield, material, and metadata types preserve their domain-specific contracts.

Binary formats are versioned. Writers and readers must agree on byte order, sizes, optional channels, and index domains. In particular, PCGG triangulation indices use a face-corner domain; triangle rendering should use the PCGM mesh blob.

Keep golden fixtures for every protocol version and test them in native and host consumers.
