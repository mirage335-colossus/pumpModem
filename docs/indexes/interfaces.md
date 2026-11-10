# Interfaces, application behavior and inspection

[Documentation index](../README.md) · [Build and delivery](development.md) ·
[Security and compatibility](security.md)

These references describe shared application behavior and frontend boundaries.
Actual platform/device qualification is recorded in [validation](../validation.md)
and the [evidence index](evidence.md).

| Document | Contents |
| --- | --- |
| [GUI architecture](../gui-architecture.md) | Shared models/controllers, adapter boundaries and maintenance/verification guardrails. |
| [GUI contract](../gui-contract.md) | Required user-visible behavior and shared UI conformance. |
| [Frontend interfaces](../frontend-interfaces.md) | Native, terminal, framebuffer and browser frontend interfaces and boundaries. |
| [Rev backend](../rev-backend.md) | Rev integration, rendering and backend-specific constraints. |
| [Web frontends](../web-frontends.md) | Browser delivery, worker execution, audio and qualification limits. |
| [Web protocol](../web-protocol.md) | Browser/worker message and execution protocol. |
| [Browser implementation guide](../../web/README.md) | Source-tree browser assets, development and execution details. |
| [Inspection](../inspection.md) | Live receiver pattern plots and deferred detailed transmission inspection, with interpretation limits. |
| [Link Planner](../link-planner.md) | Generic planning controls; Robust receive-probability and CPU forecasts remain unqualified for the native receiver. |
| [QR](../qr.md) | QR transport and application behavior. |

For operating commands see the [six interface manuals](../README.md#command-and-interface-manuals).
Modem-specific controls and behavior remain under [Robust](robust.md),
[Fast](fast.md) and [Legacy](../legacy-modem.md).
