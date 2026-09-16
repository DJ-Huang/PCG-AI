# UV Domains

UV data has three related but distinct sources: point-domain attributes, face-corner attributes, and render-mesh vertex buffers. Seams require duplicated face corners or render vertices even when positions are shared.

Perform topology-changing operations before final UV generation when possible, then preserve or regenerate UVs deliberately through bevel, boolean, merge, and triangulation. Material assignment consumes the resulting UV set but does not create it.

Verify the native attribute domain, serialized mesh UV channel, and actual textured rendering. A non-empty UV array alone does not prove seam correctness or useful projection.
