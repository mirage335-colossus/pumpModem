# Recovered local development snapshots

This archive preserves older local source drafts and loose text objects found
while backing up all branches and tags on 2026-09-24. The active working tree
was clean. Its main revision was `8ab506dc2492e49c347ce154cbeee3f7871f3aff`.

The preceding 34 commits preserve original Git trees byte-for-byte.
Their order is an archival sequence, not a reconstructed development history.
These snapshots are historical drafts and are not validated release candidates.
No source changes were applied to main or to the active working tree.

`recovery-manifest.json` maps each original tree to its archival commit and
records the recovery audit. Inspect a file with
`git show ARCHIVE_COMMIT:path/to/file` or create a separate worktree at that
commit. The `loose-blobs` directory preserves the remaining unassociated texts
under their original Git object IDs.

Existing branches and release tags remain the references for normal development
and tested releases. Build directories and dependency caches are outside this
source-history archive.
