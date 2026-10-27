# Decision: defer namespace-aware socket attribution

**Status:** Deferred; cross-network-namespace attribution is not claimed.

## Decision

Do not expand the current /proc socket-owner inference into a claim of namespace-wide visibility in this milestone batch.

## Why

The observer samples TCP/TCP6 tables associated with a selected process and uses process file descriptors to infer socket ownership. Process races, PID reuse, access restrictions, namespace boundaries and ambiguous inode ownership can leave gaps. A synthetic path-selection test proves the selected path logic, not comprehensive visibility across namespaces.

## Re-entry criteria

Record namespace identity as evidence; test isolated network namespaces with known synthetic listeners; distinguish inaccessible tables from empty tables; retain explicit partial quality for ambiguous ownership; and document kernel/permission limitations supported by observed CI results.

## Consequence

A missing socket event means it was not observed within the collector's coverage. It does not prove that the process made no network connection.