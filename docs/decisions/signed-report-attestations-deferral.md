# Decision: defer cryptographic signatures for report attestations

**Status:** Deferred; detached SHA-256 manifests detect changes only relative to a separately retained manifest.

## Decision

Keep the current unsigned report integrity sidecar. Do not describe its hash as author authentication or tamper-proof storage.

## Why

A signature is useful only with an explicit identity and key-trust model. Driftglass has not selected key provisioning, local key protection, key rotation/revocation, signer identity semantics, or a policy for reports signed by unknown keys. Adding a signature without that model would create a misleading security signal.

## Re-entry criteria

Define canonical bytes, signature algorithm and manifest-version migration; document key creation, storage, backup, rotation, revocation and trust-on-first-use risks; show unknown/invalid/expired-key states distinctly; and test tampering, truncation, unsupported algorithms and key loss.

## Consequence

The existing report attest/verify commands check report bytes and content IDs against an unsigned sidecar. They do not authenticate a publisher or establish that observed behaviour was complete.