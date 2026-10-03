# Versioning and compatibility policy

The current 0.1 series is pre-stable; it does not promise source or binary
compatibility. The hazard backend and example routing adapter are experimental.
Installed consumers must rebuild against matching headers and compatible
compiler, standard-library ABI and flags. CMake's SameMinorVersion check is a
package selection rule, not ABI verification.

For a future stable 1.x release, the policy is:

- Patch releases fix defects without deliberate source API changes.
- Minor releases may add APIs with compatible defaults. Supported stable APIs
  scheduled for removal get documented migration guidance and `[[deprecated]]`
  warnings for at least one minor release before the next major version.
- Breaking source changes require a major version. Experimental APIs are
  excluded and must remain visibly marked.
- Binary compatibility is not promised across releases; rebuild and relink.
- Memory lifetime, linearization, rejection and shutdown contracts are API
  commitments, not merely implementation details. Behavioral changes require
  release notes and compatibility review.

A stable release requires preserving the MIT notices, clean supported-toolchain checks,
independent concurrency review and recorded release sign-off. No 1.0 tag or
stable support promise is created by implementing this policy.
