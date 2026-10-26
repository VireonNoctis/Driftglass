# Decision: defer the native macOS observer

- **Status:** Accepted
- **Date:** 2026-10-26
- **Scope:** Native macOS event collection for Driftglass

## Decision

Do not implement or advertise a macOS observer in the current prototype. Keep macOS marked `not_implemented` in the machine-readable capability report. Revisit the platform after the project can test a real macOS build, validate the required permission/entitlement path, and publish a source-by-source coverage matrix.

This is a deliberate deferral, not a claim that macOS collection is impossible.

## Rationale

Apple documents Endpoint Security as a C API for monitoring process and other system security events. It requires the `com.apple.developer.endpoint-security.client` entitlement, which an application developer must request from Apple; the documented workflow uses a signed System Extension packaged in an app. See [Endpoint Security](https://developer.apple.com/documentation/endpointsecurity), [Endpoint Security entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.endpoint-security.client), and [System Extensions](https://developer.apple.com/documentation/bundleresources/system-extensions).

Network observation has its own constraints: Apple documents Network Extension capabilities and provisioning/signing requirements for certain macOS distribution paths. See [Network Extensions entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.networking.networkextension).

The current repository has no macOS-native collector, macOS CI runner, signing/entitlement workflow, or macOS event coverage tests. Implementing a stub, or assuming Linux procfs semantics transfer to macOS, would create a false support claim.

## Re-entry criteria

Reopen this decision only when all of the following can be tested and reviewed:

1. An explicitly scoped set of macOS event sources maps to the versioned Driftglass event schema without inventing unavailable values.
2. Required entitlements, signing, user approval, installation, upgrade and uninstall behaviour are documented.
3. Permission-denied and unsupported-event-source cases produce visible coverage gaps.
4. Synthetic and benign live workload tests run on supported macOS versions and record OS/build, permissions, expected observations and observed results.
5. The capability report remains truthful about any event categories that are not covered.

No entitlement is requested and no macOS collection code is added by this decision.
