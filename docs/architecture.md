# Architecture

ReRevved Studio is a content workbench. It separates file parsing, document ownership, and desktop presentation. Source data remains read-only, and parser behavior can be tested without a GPU or window system.

## Product ownership

Studio owns inspection of supported legally owned content.

- Studio provides archive inspection and extraction, transformed static wireframes,
  and read-only material and texture-source inspection. External texture loading
  and normal-dependent presentation are unsupported.

Studio does not install, activate, launch, or replace game content. ReRevved and the SDK own runtime replacement, overlay and filesystem precedence, installation, launching, and runtime validation.

Studio does not link generated guest code or inspect executable code or live
guest memory. Executable analysis belongs in `rerevved-research`; Studio
implements documented public format contracts.

## Layers

### Core library

`rerevved_studio_core` owns file classification, parsers, document models, model assembly, and single-entry extraction. It depends on neither Dear ImGui nor GLFW/OpenGL.

- Each parser consumes an explicit byte range and produces a format-specific
  document or a typed failure.
- FPK documents retain validated archive bytes and indexes. Embedded routing
  revalidates one selected range and invokes only the explicitly selected
  existing span parser.
- DDS and MP3 documents perform their supported CPU decoding without a GPU,
  window, audio device, or temporary retail file.
- NIF documents own all retained block metadata and geometry arrays. Model
  assembly produces ordered index-only descriptors back into that document,
  validates renderability separately from parsing, and copies no geometry
  arrays.
- Extraction revalidates the selected source span and creates only a new
  caller-selected destination.

Detailed byte layouts and evidence qualifications live in [File format support](file-formats.md).

### Application

`rerevved-studio` owns Dear ImGui windows, opened-document lifetime, selection, navigation, layout, presentation formatting, and Preview interaction.

- Each opened asset owns its parsed documents and format-specific UI state.
- Each archive owns its selected entry, explicit embedded-format choice,
  extraction input/results, optional opened document, and embedded preview
  state.
- Direct and explicitly opened embedded NIFs share the same Inspector and
  wireframe presentation.
- Presentation reads parser-produced values. It does not reinterpret unknown
  bytes or make a parse failure disappear.

### Preview resources

The DDS viewer is the only current asset preview that owns an OpenGL texture. The application clears that resource while its context remains current before destroying or replacing the owning document. MP3, MAP, and NIF previews use CPU data and ImGui drawing only. NIF wireframes create no model GPU resources.

## Document ownership

| Owner | Retained state |
|---|---|
| Application | Ordered opened assets, selected asset, status, and DDS viewer |
| Asset document | File inspection plus optional format document, model result/error, and direct NIF navigation state |
| Archive document | FPK bytes/index, selected entry, explicit format, extraction state, optional embedded document, and embedded NIF navigation state |
| NIF document | Validated block metadata, transforms, geometry, materials, texturing properties, and source carriers |
| NIF model | Ordered stable indices and composed static mesh transforms; no copied arrays or GPU state |

Closing or replacing one document discards only state owned by that document. Separate files and archives do not share NIF mesh, projection, pan, zoom, opened-document, or extraction state.

## Parser and presentation boundaries

Parsing and presentation answer different questions:

- Parsing asks whether bytes satisfy the supported structural contract. It
  bounds every read, validates references required by that contract, preserves
  retained raw values, and requires exact selected-block consumption where the
  format defines it.
- Model assembly asks whether a structurally valid NIF scene has supported,
  safely addressable geometry. Semantic renderability failures do not invalidate
  the parsed Inspector document.
- Presentation formats validated values, handles responsive layout and input, and owns preview transforms and resources. It does not add file-format semantics.

External NIF texture-source names illustrate the boundary. The parser retains and validates their exact source carriers, while Inspector escapes the bytes. Studio does not implement title lookup roots, archive relationships, a target texture format, or a loading policy.

## Invariants

Every change must preserve these constraints:

1. Source reads are bounded and non-mutating.
2. Extraction never overwrites an existing destination.
3. Retained unknown values remain observable and unsupported input fails explicitly. Data documented as outside the retained API may be checked and skipped; nothing is guessed, normalized, or repaired.
4. Core parsing, document models, assembly, and extraction remain independent
   of ImGui, GLFW, and OpenGL.
5. A parser is covered by wholly synthetic repository-owned tests and remains
   usable without a GPU or window system.
6. Retail files, extracted payloads, captures, private evidence, and generated
   analysis output are never repository dependencies.
7. Every field meaning not defined by a public standard carries its evidence locator. New format semantics require a documented observation of legally owned content or reproducible parser evidence, with scope and provenance preserved.

## Dependencies and warnings

Dear ImGui docking, GLFW, and dr_mp3 are fetched at exact revisions. The core, application, and tests use the repository's strict warning policy. Fetched targets keep their upstream warnings so dependency diagnostics do not redefine Studio's build contract.

See [Development](development.md) for reproducible configure, build, test, smoke, formatting, and diff checks. See the [User guide](user-guide.md) for current application workflows.
