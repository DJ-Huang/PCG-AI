# Public History Cleanup Runbook

The working tree no longer tracks reproducible build output, but older commits still contain large native archives, test binaries, and generated scene data. Rewriting public history is disruptive and must be coordinated separately from normal repository cleanup.

## Preconditions

1. Pause merges and announce a maintenance window.
2. Create a mirror clone and a recoverable backup tag outside the rewritten repository.
3. Confirm that no released artifact depends on an object that will be removed.
4. Install `git-filter-repo` and record the current default-branch commit.

## Candidate removal classes

- native build directories and archives;
- test executables and coverage output;
- Web dependency/build directories;
- Unity `Library`, `Temp`, logs, recordings, and generated workspaces;
- generated showcase artifacts that have a reproducible source graph.

Generate the exact path list from repository history and review it before running a rewrite. Do not use broad extension rules for `.pcg`, `.scene`, `.asset`, `.meta`, schemas, lockfiles, `.agents`, or `.picg`.

## Execution and recovery

Run the approved `git filter-repo` operation in the mirror clone, expire reflogs, repack, and compare branch/tag inventories. Re-run the full validation matrix from a fresh clone of the rewritten repository. Force-push protected refs only after review, then require contributors to re-clone or carefully reset their local branches.

This repository does not automate the force-push. History cleanup requires explicit maintainer approval because it changes every affected commit ID and invalidates existing clones and pull-request bases.
