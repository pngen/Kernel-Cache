# Persistence format

On disk, each artifact lives under `<root>/artifacts/<id>.meta` and
`<root>/artifacts/<id>.blob`. The metas a version marker, canonical key bytes
(base64), sizes, provenance, and SHA-256 checksums for both the metadata and the
artifact payload. Writes are atomic (temp-write + rename). Recovery verifies
version, checksums, and lengths; rejects unknown versions, corrupt, and
truncated artifacts; and cleans orphan temp files.
