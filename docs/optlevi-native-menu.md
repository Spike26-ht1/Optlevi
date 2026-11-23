# Superseded: OptLevi floating-button prototype

This document describes an **obsolete experimental OptLevi 0.1.0** implementation that registered a floating Android overlay button next to the game. The project owner rejected that direction.

The current project instead uses a **Bedrock JSON-UI resource pack** to render a vanilla-styled Mods button near the start-screen Dressing Room control. Build-time code removes the former floating `ButtonBuilder` registration.

See [Levi API resource UI](levi-api-resource-ui.md) for the active architecture and its outstanding Android native event bridge.

Do not install the old floating-button build when testing the new UI. Use the current `.mcpack` plus `.levipack` artifacts instead, recognizing that the JSON-UI button callback is still not connected to Android native code.
