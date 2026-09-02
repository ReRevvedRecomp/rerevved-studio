# Automated and AI-assisted contributions

If automated or AI assistance is used, the work follows the same contribution and evidence rules as any other work.
The contributor owns every submitted claim and must be able to explain its source, scope, and evidence.

Automated output is not file format or runtime evidence. A durable format claim
starts from a public specification, a documented observation with an explicit
input range, or a reproducible parser result. Read
[Evidence and claims](evidence-and-claims.md) before adding an observation to
documentation, tests, or source.

## Public contribution policy

- Follow the repository boundaries and contribution rules in
  [CONTRIBUTING.md](../../CONTRIBUTING.md).
- Keep tracked documentation current and public. Do not include retail files,
  extracted assets, captures, credentials, machine paths, or build output.
- Support format claims with the evidence described in
  [Evidence and claims](evidence-and-claims.md).
- Put each settled fact in one guide and link to it elsewhere.
- Preserve byte offsets, sizes, hashes, tool names, uncertainty, and provenance
  citations when they support a claim.
- Do not commit prompts, assignments, handoffs, session plans, or self-review
  metadata.
- Keep tracked Markdown and structured data public and free of private workflow
  records.
- Use stable identifiers for durable evidence claims and cross-references.
- Prefer semantic tests of public behavior over implementation-detail tests.

Documentation ownership:

- [User guide](../user-guide.md) - current user workflows
- [Architecture](../architecture.md) - ownership and invariants
- [File format support](../file-formats.md) - parser and evidence contracts
- [Development](../development.md) - reproducible checks
