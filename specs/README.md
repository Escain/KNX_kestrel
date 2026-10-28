# Specifications

| File | Content |
|------|---------|
| [01-requirements.md](01-requirements.md) | Vision, scope, clean-room policy, functional (FR-*) and non-functional (NFR-*) requirements |
| [02-architecture.md](02-architecture.md) | Architecture synthesised from the brainstorm: layers, targets, key abstractions |
| [03-domain-model.md](03-domain-model.md) | Core entities, identity, mutation/undo, file format |
| [04-knx-protocol.md](04-knx-protocol.md) | KNX stack layering, bus access, commissioning state machine, simulator |
| [05-test-strategy.md](05-test-strategy.md) | Test pyramid, tools, traceability matrix |
| [06-roadmap.md](06-roadmap.md) | Milestones |
| [07-catalog.md](07-catalog.md) | Product files (`.knxprod`): ZIP reader, catalog model, tolerance rules |
| [08-gui.md](08-gui.md) | GUI for configuring a network: catalog, catalog device editor and parameters, commissioning |
| [09-memory-images.md](09-memory-images.md) | Parameter evaluation and memory images (ETS-identical), reading devices back |
| [adr/](adr/) | Architecture Decision Records (one decision per file) |
| [brainstorm/](brainstorm/) | Raw brainstorm-room proposals (round 1) and cross-critiques (round 2) |

Specs are living documents: update them **in the same commit** as the code that changes the behaviour.
