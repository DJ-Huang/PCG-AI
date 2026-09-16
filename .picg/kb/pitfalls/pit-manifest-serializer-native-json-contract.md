---
id: pit-manifest-serializer-native-json-contract
name: Manifest, serializer, and native JSON type drift can terminate the editor
severity: high
rootCauseType: serialization contract drift across a native boundary
techStack: [Unity Editor, CSharp, CPlusPlus, nlohmann-json]
tags: [type/pitfall, area/serialization, area/native-plugin, area/pcg]
verified_status: limited
verified_by: "PICG Delete preview regression and compatibility tests"
verified_date: "2026-07-27"
---

# Keep JSON types consistent across every layer

A manifest type unsupported by the C# serializer may be emitted as a JSON string. A native parser that calls `value<double>` or `value<int>` can then throw a type error across the plugin boundary and terminate the editor.

Fix both sides of the contract:

- restrict manifest property types to serializer-supported scalar types;
- serialize numbers and booleans as JSON scalars;
- accept bounded legacy string values in the native parser;
- reject invalid, non-finite, or out-of-range values without throwing across the ABI;
- deploy and restart the actual runtime backend before verifying.

Acceptance requires scalar output for new assets, equivalent behavior for supported legacy strings, deterministic defaults for invalid strings, and an editor preview that reports errors without exiting.
