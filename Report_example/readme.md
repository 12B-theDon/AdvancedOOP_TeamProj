# Real Report Example

Generated from the real GUI capture run on 2026-11-18. No synthetic scores.

- DB: FVC2000_DB3_B; enrollment: `102_6.tif`.
- Model: `fvc2000_db3`, retrained run `models/retrain_20261117_235258`.
- Model SHA-256: `26901a10a1b172874b36656b859d4fc1c6be38100c290a20301a1b9d577227e5`.
- All eligible pairs: 7 Genuine and 72 Impostor; the enrollment image is excluded.
- Genuine threshold: 0.90; Impostor threshold: 0.80.
- Outcomes: PASS 7, MISS 0, REJ 66, FA 6, ERR 0.
- FMR: 8.33333%; FNMR: 0%; observed EER: 0%.
- Selected report metrics: FTE, FMR, FNMR, EER, matching time.
- CSV keeps all metrics and model scores. Paths describe locations relative to the project.
- `templates/*.bin` are 2,048-byte output features, not model weights.

[HTML report](report.html) · [Preview](report_preview.png) · [Pair results](comparisons.csv)

The HTML preview is rendered with Qt. Open the HTML for the actual document and
vector graphs. Timings are specific to this run. Train/Validation were split by
capture number; this is a GUI example, not an independent Test-set accuracy result.
