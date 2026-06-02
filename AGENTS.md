# CBE Emulator Reverse Engineering Guide

## Scope

This repository mixes three closely related workstreams:

- emulator runtime work under `src/`
- reverse-engineering notes and protocol findings under `docs/`
- tooling and extractors under `tools/`

Treat the repository as a long-running reverse-engineering project. Prefer preserving evidence, documenting hypotheses, and making the smallest change that unlocks the next verified step.

## Repository Map

- `src/`: Unicorn-based emulator and platform/runtime shims
- `tools/`: repeatable helper scripts for disassembly, extraction, decoding, and validation
- `docs/net_mock_protocol.md`: currently verified network mock behavior for Jianghu OL
- `docs/re/`: durable reverse-engineering notes, protocol docs, memory maps, and session logs
- `firmware/`: firmware images, unpacked pieces, and extraction manifests
- `samples/`: packet captures, screenshots, save data, code snippets, and other evidence
- `server/`: future private-server implementation and protocol test harnesses
- `.codex/skills/cbe-reverse-engineering/`: project-local skill that teaches Codex how to work in this repo

## Working Rules

1. Keep raw evidence separate from interpretations.
2. Mark uncertain findings as `hypothesis` until confirmed by code, logs, memory inspection, or repeatable runtime behavior.
3. Prefer implementing emulator or server behavior that matches the observed client/platform contract over patching game logic to force progress.
4. Do not silently replace existing reverse-engineering notes. Extend them and preserve prior findings unless they are clearly disproven.
5. Keep filenames stable once they start being referenced by notes, scripts, or prompts.

## Documentation Rules

When you learn something durable, update the closest matching document in `docs/re/`:

- `docs/re/firmware-map.md`: firmware layout, segments, loaders, container formats
- `docs/re/protocol.md`: packet formats, event sequencing, field semantics
- `docs/re/runtime-contracts.md`: platform APIs, screen lifecycle, timers, file I/O, VM semantics
- `docs/re/open-questions.md`: unresolved hypotheses and next experiments
- `docs/re/session-log.md`: short dated notes for what changed and what was verified

Keep entries compact and evidence-driven. Each important claim should mention how it was established, such as string search, xref path, trace log, or runtime observation.

## Reverse-Engineering Workflow

Start from the smallest reliable source of truth:

1. static evidence from firmware, CBE payloads, symbols, strings, imports, and cross-references
2. emulator logs and runtime traces
3. packet captures or mock-server transcripts
4. behavioral inference only when the first three are insufficient

Preferred sequence for new features:

1. identify the client/runtime entry point
2. document inputs, outputs, and side effects
3. implement the minimum emulator or server behavior needed
4. verify with a repeatable run
5. write the result back into `docs/re/`

## Editing Rules

- Reuse existing helpers before adding new ad hoc code paths.
- For network mock behavior, prefer helper builders in `src/main.c` over handwritten packet assembly.
- Keep diagnostic logging scoped and easy to remove after verification.
- Avoid broad refactors while chasing an unverified reverse-engineering hypothesis.

## Validation

After meaningful changes:

1. rebuild with `build.bat` or `make` when relevant
2. note what scenario was exercised
3. record whether the result is confirmed, partial, or still hypothesis

If a task depends on files that should not be committed, place them under `firmware/raw/` or `samples/raw/` and keep manifests or notes in tracked files.
