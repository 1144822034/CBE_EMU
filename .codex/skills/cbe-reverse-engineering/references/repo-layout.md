# Repo Layout

## Source of truth by area

- `src/`: emulator and platform behavior
- `tools/`: repeatable scripts that Codex can invoke
- `docs/net_mock_protocol.md`: current verified Jianghu OL network mock notes
- `docs/re/`: durable reverse-engineering knowledge
- `firmware/`: firmware images, unpacked outputs, manifests
- `samples/`: captures and evidence
- `server/`: future backend implementation

## Artifact placement

- raw or sensitive inputs: `firmware/raw/` or `samples/raw/`
- extracted but reproducible artifacts: `firmware/unpacked/`
- parser outputs or manifests worth tracking: `firmware/manifests/`
- packet examples and decoded transcripts: `samples/packets/`
- screenshots and UI traces: `samples/screens/`
