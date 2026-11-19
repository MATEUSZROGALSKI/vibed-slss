# 0001. Use Cinder's bundled GLM, never vcpkg's

Date: 2026-11-19

## Status

Accepted

## Context

Cinder translation units resolve `#include <glm/...>` against the GLM copy
bundled in `cinder-src/include`. If `glm` is also declared in `vcpkg.json`,
our own TUs compile against a newer vcpkg GLM while Cinder's prebuilt
binaries and headers expect its bundled one — two different type layouts
(`ci::ivec2` and friends) under identical names.

That is an ODR violation with no diagnostic: calls compiled against the
"wrong" GLM pass arguments in the wrong registers/offsets and misbehave at
first use (observed as `ExcInvalidWindow` / segfault at first draw; Windows
builds appeared to work by ABI luck only).

## Decision

`glm` must never appear in `vcpkg.json`. All Cinder-consuming targets get
GLM exclusively from Cinder's bundled include tree. Pure-core code that
needs a math library uses plain math or Cinder headers where already linked.

## Consequences

- One GLM ABI across the whole link — no silent ODR violation.
- GLM version is pinned to whatever the pinned Cinder tag ships, which is
  consistent with Cinder being pinned overall.
- Agents must not "fix" missing-GLM configure errors by adding the vcpkg
  port; the fix is to include Cinder's headers in the affected target.
