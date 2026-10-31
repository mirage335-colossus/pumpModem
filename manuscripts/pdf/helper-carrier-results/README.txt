HELPER CARRIER: METHODS, RESULTS AND RECOMMENDED PRACTICES
Evidence cutoff: 31 October 2026

This is an academic-style synthesis of recorded standalone development results,
centered on successful techniques, practical gains and recommended receiver practices.
The approximate +/-0.1 Hz objective is treated as an aspirational development guide,
not a mandatory acceptance criterion. Comparisons preserve both favorable results
and operating limits; numerical evidence is unchanged by this editorial revision.
It is not a new receiver experiment, peer-reviewed publication, downstream
decoding demonstration, or hardware qualification. No receiver source was changed
while preparing it.

Files
  helper-carrier-results.pdf   Rendered manuscript (delivered separately).
  article.html                Browser-readable editable-format rendition.
  manuscript.py               Authoritative editable prose and tables.
  render_article.py           PDF, HTML and vector-figure renderer.
  paper-data.json             Extracted data used by the figures.
  timing-holdouts.csv          Frozen timing values for the 32 held-out cases.
  evidence-manifest.json      Source/evidence hashes and recorded binary hashes.
  prepare_data.py             Extraction from the original repository evidence.
  visual_figures.py           Format, hopping, probe, waterfall and cost figures.
  prepare_visual_data.py      Read-only extraction from the archived cable capture.
  spectrogram-data.npz        Extracted STFT arrays; no receiver corrections applied.
  visual-data-provenance.json Window, normalization, timing and capture provenance.
  revision-evidence.json      Sources inspected for the expanded revision.
  figures/                   Eight scientific figures, each in PDF and SVG formats;
                             the waterfalls embed raster data with vector labels.

Regeneration
  python3 render_article.py

The renderer requires Python, NumPy, Pillow, ReportLab and Liberation Serif/Sans TrueType fonts.
FONTDIR in render_article.py may be adjusted for another machine. It reads only
the bundled manuscript and extracted data and writes only local article outputs.
It never builds or runs the receiver. Open article.html beside its figures folder
to read the HTML rendition.

prepare_data.py is optional and requires the original repository, including its
ignored local-evidence files. It is not necessary to regenerate the manuscript.
prepare_visual_data.py additionally requires the existing archived cable capture.
It performs an STFT for illustration, without opening an audio device. Its extracted
arrays are bundled, so that capture is not required to render the manuscript.
Schematic format and hopping diagrams are not received-data measurements.
The bundle does not contain the complete simulation captures, binaries, or full
raw campaign logs. The evidence manifest identifies those original inputs; the
article provides commands and limitations for reproducing receiver experiments.

The paper distinguishes historical matched ablations from later campaigns;
frozen 36-second estimates from later corrections; clean twins from noisy probes;
and conditional engineering intervals from calibrated confidence guarantees.
Table and figure captions identify trial counts and aggregation conventions.

Visual QA used Poppler rendering of every PDF page plus text/bounds checks.
