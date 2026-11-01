# Reading the source provenance

Linux/ACK/stable history and both parents of the U59 merge are retained in the
kernel repository. The module repository retains its existing upstream history;
the new package paths are source snapshots with exact donor mappings where
established. Do not assume that every newly packaged file has its donor's full
repository history imported into that new path.

The `e4d228a4e4441fa87f432ed5f1a8ea281857d4bc` module merge adds 480 filtered
original ACK file-history commits. Other source imports keep their original
copyright/license notices and are attributed by repository, revision and path
in `provenance.json`. This is source attribution, not a claim that the port
maintainer authored Qualcomm/Nothing/redducc/LineageOS/AOSP code.

The current `files` map contains the checked-out source files and their SHA-256.
`retired_files` retains older mappings for paths removed during source-closure
cleanup. A current digest means the file was inventoried; it does not by itself
prove compilation, runtime behavior or that a local adaptation is correct.
`LOCAL_PORT_SOURCE` identifies files without a confident exact donor mapping.
Such files must not be advertised as original upstream work by the maintainer.
The provenance JSON excludes its own digest to avoid a self-reference.

The ROM patch applier is copied verbatim from the Pong device support. The
SELinux patch is an attributed capture of an existing local source change,
not newly invented policy and not a change to Enforcing. Build/integration
tools and documentation are separate from imported driver implementations.

Published branches are extended by normal commits. The older
`pong-a17-6.18.32` branch remains available; no force-push or history rewrite is
needed. Private signing keys, personal backups and raw device logs are excluded.
