<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Design specifications

These specifications record the design baseline of 28 October 2026. They describe planned behavior and release requirements, not implemented behavior. Test evidence and unresolved questions retain their original scope; see the [main README](../../README.md) for current implementation and validation status.

- [Architecture](ARCHITECTURE.md) specifies module boundaries, frame ownership, GPU transport, inference scheduling, reconstruction and failure handling.
- [Models](MODELS.md) records publisher artifacts, licence evidence, manifest and weight-binding requirements, downloads and hint-model candidates.
- [Filter and model-manager design](UX_DESIGN.md) specifies presets, controls, defaults, setup, status messages and accessibility.
- [Documentation plan](DOCS_PLAN.md) defines user guides, option references, compatibility reports and the real OBS screenshot shot list.
- [Implementation work packages](TASKS.md) defines the backlog, dependencies, acceptance criteria and release risks.
- [AMD and ROCm assessment](ROCM_AMD.md) records the DirectML and MIGraphX plans, vendor evidence, testing requirements and hardware gaps.

## Planned pipeline and modules

The diagram follows the module boundaries and pipeline contract in [Architecture](ARCHITECTURE.md). Each completed result stays paired with its captured input frame.

```mermaid
flowchart TD
  input[OBS filter input] --> capture[Capture, crop and resize]
  capture --> slots[Bounded frame pool and scheduler]
  slots --> transport[GPU interop or staged transport]
  transport --> ck2[CK2: MoGe cache, RVM step, Stage 1, optional Stage 2]
  transport --> hint[CK1: chroma or optional model hint]
  hint --> ck1[CK1: GreenFormer]
  models[Model catalogue, licences, downloads and local weight binder] --> runtime[ORT sessions, provider probes and caches]
  runtime --> ck2
  runtime --> ck1
  ck2 --> reconstruct[Guided reconstruction and background estimation]
  ck1 --> reconstruct
  reconstruct --> refine[Optional native-tile edge refiner]
  reconstruct --> finish[Despill, matte levels and source-alpha mask]
  refine --> finish
  finish --> output[Paired foreground and alpha, OBS composition or debug view]
  slots --> retained[Retained native RGB frame]
  retained --> reconstruct
  retained --> output
  ui[Filter properties and Qt model manager] --> models
  ui --> slots
  output --> telemetry[Timing, diagnostics and optional audio sync]
```
