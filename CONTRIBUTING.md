# Contributing

ReRevved Studio accepts focused changes to its desktop application, file parsers, extraction tools, tests, and public documentation. Contributions use the [GNU General Public License version 3 only](LICENSE).

## Scope

- Check every read against the supplied byte range and leave source content unchanged.
- Keep parsers and extraction logic independent of the desktop interface.
- Preserve unknown fields until reproducible evidence gives them a stable
  meaning.
- Support format claims with a public specification, a documented observation with an explicit input range, or a reproducible parser result.
- Pin new dependencies to exact revisions and review their licenses.
- Do not submit retail files, extracted assets, captures, decoder output,
  build output, credentials, or machine paths.

Use the documentation according to its authority:

- [User guide](docs/user-guide.md) defines current user-visible workflows.
- [Architecture](docs/architecture.md) defines ownership, layers, and invariants.
- [File format support](docs/file-formats.md) defines parser contracts, evidence qualifications, and unsupported boundaries.
- [Development](docs/development.md) defines prerequisites and local build and test commands.

Update the guide that owns the changed behavior or contract instead of repeating release notes across multiple documents.

When automated or AI assistance contributes to a change, follow the
[automated and AI-assisted contribution policy](docs/ai_agents/README.md).
