# XSTNSIONS extension manifest format

A version 1 package is one directory with an `extension.json` file; optional content resides in subdirectories. Supported package kinds are `preset` and `sample-pack`; `native-plugin` is reserved metadata only and does not execute binaries.

| Property | Required | Meaning |
| --- | --- | --- |
| `schemaVersion` | yes | Currently `1`. |
| `id` | yes | Stable ASCII ID using letters, digits, dots, dashes or underscores; unique per user's library. |
| `name` | yes | Display name for the extension browser. |
| `version` | yes | Human-readable extension version. |
| `kind` | yes | `preset`, `sample-pack`, or reserved `native-plugin`. |
| `author` | recommended | Creator's name. |
| `description` | recommended | What the extension contains. |
| `preset` | optional | Sound/effects values for the preset. |

Preset values can configure `waveform` (`sine`, `saw`, `pulse`), `volume` (0–1), `cutoff` (0–1), `resonance` (0–0.95), `delay` (0–1) and `chorus` (0–1).

The extension importer **copies data only**. It does not execute scripts, native binaries or symlinks. Imports are capped at 512 MiB and 20,000 files. Keep individual files clearly attributable and include any third-party licenses.
