# Original scope record — version 001_00

This page records the historical product scope associated with version `001_00`.
It predates retirement of the single-carrier Robust implementation and does not
define current Robust behavior or qualification.

The maintained components are the independent Fast and Legacy modems and the
canonical native Robust transmitter/channel/Helper Generator stack. Robust GUI
transmission and reception remain unavailable pending receiver integration.
See [current requirements](requirements.md),
[Robust architecture and limits](robust-current.md), [Fast](indexes/fast.md),
and [Legacy](legacy-modem.md).

Historical copies of old binaries and protocols must be matched to their own
source revision. The current tree does not promise in-place compatibility with
retired single-carrier formats. Protocol changes evolve the canonical source;
older protocol versions belong in separate tags and binary releases. Any
security fix that needs a backport is applied narrowly to strictly sanitized
dynamic-input surfaces.
