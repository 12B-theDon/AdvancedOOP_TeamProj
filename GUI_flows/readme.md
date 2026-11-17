# GUI Captures

Captured on 2026-11-18 from the current Qt GUI on macOS, using real DeepPrint
inference. These are application captures, not mockups. The offscreen Qt renderer
omits the native window frame. Each captured application area is 900 x 720 px.

Weights: `models/retrain_20261117_235258` (local; excluded from Git).
All four archives matched their SHA-256 values in that run's `verification.json`.

| File | Contents |
|---|---|
| [00_overview.png](00_overview.png) | Initial DB screen through results, 2 x 4 captures. |
| [01_single_enrollment.png](01_single_enrollment.png) | Single enrollment, 3 x 3 captures. |
| [02_multiple_enrollment.png](02_multiple_enrollment.png) | Two enrollment images and four models, 3 x 3 captures. |
| [03_external_db.png](03_external_db.png) | External-folder demo and four models, 3 x 3 captures. |
| [UI_updates.png](UI_updates.png) | Rules, Genuine results, Impostor results, and Metrics tab, 2 x 2 captures. |

The nine-frame scenarios show DB, enrollment, templates, comparison type, rules,
execution, Genuine results, Impostor results, and Metrics/report. Save all is disabled
on comparison tabs and enabled on the Metrics/report tab.

| Scenario | DB / Enrollment | Models | Genuine / Impostor rules | Pairs G / I | Threshold G / I |
|---|---|---|---|---|---|
| Single | FVC2000_DB3_B / `102_6.tif` | fvc2000_db3 | All / All | 7 / 72 | 0.90 / 0.80 |
| Multiple | FVC2004_DB1_B / `101_1.tif`, `102_1.tif` | All four | All / Random 5 per enrollment, seed 42 | 14 / 10 | 0.85 / 0.70 |
| External | 9 real TIFFs copied from FVC2002_DB3_B / `101_1.tif` | All four | All / First available image per other finger | 2 / 2 | 0.80 / 0.65 |

The external demo contains captures 1, 2, and 3 of fingers 101, 102, and 103.
It demonstrates folder loading; it is not an independent external benchmark.

| Scenario | PASS | MISS | REJ | FA | ERR |
|---|---:|---:|---:|---:|---:|
| Single | 7 | 0 | 66 | 6 | 0 |
| Multiple | 13 | 1 | 3 | 7 | 0 |
| External | 2 | 0 | 1 | 1 | 0 |

Rows are numbered from 1 within each comparison group. Green PASS and red MISS
apply to Genuine; blue REJ and orange FA apply to Impostor. Decisions, pair counts,
and real CSV exports were checked. Thresholds illustrate UI behavior; these small
runs do not establish general recognition accuracy.
