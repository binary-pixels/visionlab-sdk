# Evaluation & Licensing

VisionLab ships in two tiers with different licenses:

| Tier | What | License | Where |
|---|---|---|---|
| **SDK** | plugin ABI, integration protocol, host SDKs, sample plugins/recipes, docs | **Apache-2.0** | this repository — use freely, including commercially |
| **Runtime** | the engine (`VisionLab` executable) | **proprietary, per device / per seat** | distributed **on request with a license key** |

The SDK lets you **build** against the runtime (custom plugins, host apps). The runtime
itself is what runs a machine in production and requires a license.

> Copying the runtime binary does **not** grant a license and does **not** make it run — the
> runtime requires a valid, machine-bound `license.json`.

## How to obtain the runtime

1. **Request a license** — open
   [a license request issue](https://github.com/binary-pixels/visionlab-sdk/issues/new?template=license_request.yml)
   and state:
   - **Type**: evaluation or commercial
   - **Use case** (e.g. "SMT AOI", "hole measurement", …)
   - **OS / hardware** (Windows version, x64)
2. **Get a fingerprint tool.** You receive a small utility (`getuuid.exe`) that prints this
   machine's hardware fingerprint.
3. **Run it on the target machine** and send back the fingerprint (a text file).
4. **Receive `license.json`** — a license key **bound to that machine**.
5. **Install**: put `license.json` next to the runtime executable and start it.

## Evaluation vs commercial

- **Evaluation** — **time-limited** (e.g. 30 days) and machine-bound; for testing on your own
  images/parts. Ideal to run our **HALCON comparison** and your acceptance samples
  (see [`VERSUS_HALCON.md`](VERSUS_HALCON.md)).
- **Commercial — perpetual, node-locked** — a **permanent** license bound to the machine: the
  production line **never stops** because of licensing. Sold **per device** (or per seat),
  with an **optional annual maintenance/support subscription** for updates and support.
  Discuss specifics in the license-request issue.

> **Production licenses do not expire.** They are perpetual and node-locked. Only
> *evaluation* licenses are time-limited. Renewing does not affect a running line.

## What runs offline

A licensed machine runs its recipes **fully offline** — no connection to us is required at
runtime. The license is issued once per machine and checked at startup.

## Notes

- The **SDK** (this repo) needs no license. Only the **runtime** does.
- Some optional integrations in the SDK reference **commercial third-party libraries**
  (e.g. Basler pylon, HALCON) that are **not** redistributed here — you must license those
  separately.
- Handle your `license.json` like a secret; it is bound to a specific machine.
