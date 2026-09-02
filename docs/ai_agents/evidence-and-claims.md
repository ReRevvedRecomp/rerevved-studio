# Evidence and claims

This repository records file behavior recovered from public specifications and
retail content supplied by contributors. Retail files, extracted assets,
captures, decoder output, and bulk analysis state are never committed.

## What counts as evidence

A file format observation identifies its exact input range, the tool or method used, the observed bytes or structure, and any unresolved ambiguity. A parser result identifies the test input, expected behavior, and boundary exercised.

Extensions and magic values help identify a file type, but they do not prove
its full structure. A successful parse of one file does not prove that every
variant is supported. Tests establish parser behavior only for their inputs.

Repository code, tests, and format guides establish repository contracts.
Automated output, summaries, and uncited notes are leads, not evidence.

## Claim boundary

Make the narrowest claim supported by the observation. Keep standard format facts, title observations, and runtime behavior distinct. Preserve unknown fields and conflicting interpretations until evidence resolves them.

Do not infer guest runtime semantics from an asset layout. Executable analysis
belongs in `rerevved-research`; runtime behavior belongs in `rerevved` or
`rerevved-sdk` according to ownership.

## Public boundary

Tracked documentation retains settled conclusions and reproducible methods, not retail inputs, machine paths, build output, or private working files. A public claim must carry enough context to be reviewed without access to those materials.

Preserve source, reference, permission, and provenance citations verbatim, including dates embedded in external citations.
