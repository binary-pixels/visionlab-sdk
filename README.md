# VisionLab SDK

**An embeddable machine-vision runtime and plugin SDK for industrial inspection equipment.**

VisionLab lets machine builders and system integrators ship inspection/measurement machines
without maintaining an in-house vision-algorithm team. You embed the runtime into your
machine, teach an inspection *recipe* (ROIs + algorithms + between-step geometry + PASS/NG
logic) before shipment, and the machine then runs the recipe offline, per device.

This repository contains the **public SDK surface**: the plugin ABI, the integration
protocol, host-side SDKs, sample plugins/recipes, and user/feature documentation.

> **Model:** the *SDK and ABI* here are open (Apache-2.0). The **runtime engine is
> proprietary** and licensed commercially (per-device / seat). See [Licensing](#licensing).

---

## What you can build

- **Custom algorithm plugins** that run inside the runtime (in-process C ABI) — your model
  participates in the algorithm registry, the parameter UI, and the recipe system.
- **Standalone host applications** that drive the runtime over shared-memory IPC
  (`RunRecipe` for a whole recipe, or a streaming session for many shots).
- **Recipes** that combine built-in algorithms with between-step geometric constraints to
  emit a semantic PASS/NG result (pose / presence / defect / string / measured values).

## Capabilities (built into the runtime)

- Sub-pixel **shape fitting**: circle, line, ellipse, rectangle.
- **Template matching** (gradient-orientation, rotation-invariant).
- **Blob analysis**, **golden-template diff**, **scratch** / **crack** detection.
- **OCR** (DNN / dot-matrix / VLM), **barcode / QR**, **DataMatrix**.
- **Unsupervised anomaly detection** (PatchCore-style — build a library from good parts,
  flag anything off the normal feature manifold) for unknown defects.
- **Process-chain recipes**: multi-shot capture, cross-field-of-view coordinate
  unification, measurement/compute nodes, coordinate frames and placement arrays.
- **Verification tooling**: batch runs, GRR / repeatability reports, SQLite result logging,
  per-execution debug capture (original + result image + reproducing parameters).

See [`docs/USER_MANUAL.md`](docs/USER_MANUAL.md), [`docs/RECIPE_GUIDE.md`](docs/RECIPE_GUIDE.md)
and [`docs/ALGO_USAGE_GUIDE.md`](docs/ALGO_USAGE_GUIDE.md).

## Two integration models

| Model | Where | How the plugin runs | Use when |
|---|---|---|---|
| **In-process algorithm plugin** | `shared/plugin_sdk/algo_plugin` | C ABI DLL loaded by the runtime | You want your algorithm inside the runtime's UI + recipe engine |
| **Out-of-process host SDK** | `Master/` | Your app launches the runtime and talks to it over shared-memory IPC | You want the runtime as a black-box "vision brain" driven by your own software |

### 1. In-process plugin (C ABI)

```c
// shared/plugin_sdk/algo_plugin/circle_qt_plugin.h
CQ_EXPORT int circle_qt_plugin_register(const char* appVersion, char* outJson, int outBufSize);
CQ_EXPORT int circle_qt_plugin_run(const char* algo, const CQImage* img,
                                   const char* paramsJson, char* outResultJson, int outResultBufSize,
                                   CQImage* outDisplayBgr, char* errBuf, int errBufSize);
```

See [`docs/plugin_abi.md`](docs/plugin_abi.md) and a complete example under
`shared/plugin_sdk/algo_plugin/examples/`.

### 2. Host SDK (out-of-process, shared memory)

```cpp
// Master/QtHost/IpcClient.h  — RunRecipe / StartRecipe+PushShot+FinishRecipe
IpcClient::sendRunRecipe(images, recipeJson);
```

```python
# Master/plugin_host.py
ipc.send_run_recipe([img0, img1], recipe_json)
ipc.send_start_recipe(recipe_json); ipc.send_push_shot(img, 0); ipc.send_finish_recipe()
```

```csharp
// Master/WpfHost/PluginIpc.cs
await _host.RunRecipeAsync(images, recipeJson);
```

See [`docs/PLUGIN_INTEGRATION_GUIDE.md`](docs/PLUGIN_INTEGRATION_GUIDE.md) and
[`docs/RECIPE_IPC_PROTOCOL.md`](docs/RECIPE_IPC_PROTOCOL.md).

## Documentation

| Document | Contents |
|---|---|
| [`docs/plugin_abi.md`](docs/plugin_abi.md) | In-process plugin ABI specification |
| [`docs/PLUGIN_SDK_QUICK_START.md`](docs/PLUGIN_SDK_QUICK_START.md) | Write your first plugin |
| [`docs/PLUGIN_SDK_API_REFERENCE.md`](docs/PLUGIN_SDK_API_REFERENCE.md) | SDK API reference |
| [`docs/PLUGIN_SDK_WIN32_EXAMPLE.md`](docs/PLUGIN_SDK_WIN32_EXAMPLE.md) · [`docs/PLUGIN_SDK_QT_EXAMPLE.md`](docs/PLUGIN_SDK_QT_EXAMPLE.md) | Worked plugin examples |
| [`docs/PLUGIN_HOST_DEV_GUIDE.md`](docs/PLUGIN_HOST_DEV_GUIDE.md) | Build a host application |
| [`docs/RECIPE_IPC_PROTOCOL.md`](docs/RECIPE_IPC_PROTOCOL.md) | IPC protocol + semantic result schema |
| [`docs/RECIPE_GUIDE.md`](docs/RECIPE_GUIDE.md) | Recipes and geometric constraints |
| [`docs/CLI_JSON_SPEC.md`](docs/CLI_JSON_SPEC.md) | CLI JSON interface |
| [`docs/USER_MANUAL.md`](docs/USER_MANUAL.md) · [`docs/ALGO_USAGE_GUIDE.md`](docs/ALGO_USAGE_GUIDE.md) | Using the tool and the algorithms |
| [`docs/VERSUS_HALCON.md`](docs/VERSUS_HALCON.md) | Capability comparison and positioning |

> Some documents are currently Chinese-first; English translations are in progress.

## Licensing

- **SDK / ABI / sample code / docs** in this repository: **Apache License 2.0** (see [`LICENSE`](LICENSE)).
  Use them freely, including commercially.
- **Runtime engine**: **proprietary**, licensed **perpetually (node-locked) per device / per
  seat**, with an optional **annual maintenance/support subscription**. It is *not* in this
  repository and is distributed **on request, with a license key**. Copying it does not grant
  a license.

### Getting the runtime (key on request)

1. Open a **[license request issue](https://github.com/binary-pixels/visionlab-sdk/issues/new?template=license_request.yml)**
   (evaluation or commercial; state your OS and use case).
2. You receive a small tool that prints this machine's **fingerprint**.
3. Send the fingerprint back; you get a `license.json` **bound to that machine**.
4. Put `license.json` next to the runtime executable and run.

Details: [`docs/EVALUATION_AND_LICENSING.md`](docs/EVALUATION_AND_LICENSING.md).

### Contact

- Repository: <https://github.com/binary-pixels/visionlab-sdk>
- Issues: <https://github.com/binary-pixels/visionlab-sdk/issues>
- GitHub: [@binary-pixels](https://github.com/binary-pixels)

## Not included

The runtime engine source, the algorithm internals, roadmaps and commercial terms are not
part of this repository. Some optional integrations reference commercial third-party
libraries (e.g. Basler pylon, HALCON) that are **not** redistributed here and must be
licensed separately by you.
