# User guide

ReRevved Studio opens legally owned files for read-only inspection. Source files are never modified. The only write workflow extracts one selected FPK entry to a new destination.

## Open and close files

Use any of these entry points:

- Choose `File -> Open path...`, enter a regular-file path, and select `Open`
  or press Enter.
- Drop one or more files onto the application window.
- Pass one or more file paths on the `rerevved-studio` command line.

An unsuccessful typed-path open leaves the path and exact error in the dialog so you can correct the input. Drag-and-drop and command-line errors appear in application status. Open documents remain independent even when their basenames match; the Assets panel shows each complete source path.

Select a document in Assets to show its Inspector and Preview. `Close selected`
closes only that document and releases its preview state. It does not delete or
change the source file.

Studio identifies FPK, DDS, GFX, BIK, MP3, NIF, and MAP by extension. Other
regular files receive generic header inspection.
Extension recognition does not guarantee format parsing or preview support.

## Archive Explorer

Opening a valid version-6 FPK displays the `FPK archive` Inspector. This is the Archive Explorer. A non-empty archive initially selects entry 1; an empty archive remains unselected.

Initial selection does not infer a format, open an embedded document, decode a preview, extract a file, or write data.

### Navigate entries

- Select an `Entry N` row in the list.
- Use `Previous` and `Next` when the corresponding neighbor exists.
- Enter a one-based number in `Entry number`, then press Enter or select
  `Go to entry`.

Invalid, zero, empty, or out-of-range jumps preserve the current selection and show the navigation error. Very large indexes use clipped row rendering and an explicit selectable-entry limit rather than silently narrowing a parser result.

The selected-entry section displays the entry number and validated byte offset,
size, and exclusive end in decimal and uppercase hexadecimal. `Copy byte range`
copies that exact summary. The two unknown record fields remain uninterpreted.

### Open an embedded document

Archive records do not provide a proved filename or format. Select the intended type in `Open as`, then select `Open selected in memory`. Available formats are:

- DDS texture
- GFX movie
- Gamebryo NIF
- MP3 audio
- Xbox DLC map

Studio parses the selected byte range directly from archive memory and does not create a temporary retail file. A mismatch reports the selected parser's exact malformed, truncated, or unsupported-content reason. Correct the format choice or select another entry; Studio never guesses.

`Close opened entry` releases only the opened embedded document and its preview
state. The archive, selection, explicit format choice, metadata, extraction
path, extraction result, and source bytes remain available.
The same entry can be opened again immediately.

### Extract one entry

Select `Extract selected...`, enter a new destination path, and press Enter or
select `Extract`. Both submission paths perform the same operation.

- An existing destination is never overwritten.
- Failure keeps the dialog open, preserves the typed destination, and displays
  the exact extraction error.
- Success clears the destination and closes the dialog.
- `Cancel` closes the dialog without erasing the typed destination.

Extraction revalidates the selected range before writing. There is no bulk
extraction, archive rewriting, or repacking workflow.

## DDS, GFX, MP3, and MAP views

### DDS

Supported legacy 32-bit RGB and RGBA DDS files display their top-level image in
Preview. RGB without declared alpha is shown opaque. Unsupported compression,
DX10 pixel formats, cubemaps, volume textures, mask layouts, bit counts, or
truncated pixels produce an explicit preview error instead of a guessed image.

### GFX

Supported uncompressed GFX v2 files expose movie and external-image metadata in Inspector. Names are escaped byte strings because no text encoding or path policy is proved. Studio does not execute, decompress, render, export, or write GFX content.

### MP3

Preview displays a peak waveform for at most the first ten seconds of a fully decodable MP3 stream. Studio does not play, transcode, export, or write audio.

### Xbox DLC MAP

Preview displays the retained 1024-byte core as a 32 by 32 file-order byte grid
and the separate 64-byte footer as raw bytes. Cell shade represents only the low
nibble; hover details expose the raw byte and high-bit states without assigning
terrain semantics or player-facing orientation. Studio does not edit or export
maps.

## Direct and embedded NIF wireframes

Open a supported NIF directly, or explicitly open an FPK entry as `Gamebryo NIF`. Both routes use the same read-only Inspector and wireframe presentation. Merely selecting an archive entry never opens or previews it as NIF.

The initial view selects the first assembled mesh, `Auto` projection, and a
fitted view. Preview controls are:

- `Mesh`: select any assembled mesh or `All meshes`. Individual labels contain only the mesh number, shape block index, and data block index.
- `Projection`: choose `Auto`, `XY`, `XZ`, or `YZ`. Auto chooses the raw-axis pair whose transformed bounds have the greatest area.
- Pan: drag the wireframe with the left mouse button.
- Zoom: use the mouse wheel while the wireframe region is hovered.
- `Fit/Reset`: restore the centered, aspect-preserving fitted view.

Changing mesh or projection restores a fitted view. `All meshes` draws every supported assembled mesh in common transformed space and fits their combined transformed bounds. It does not combine the document-owned geometry arrays. Separate direct documents and archives retain independent mesh, projection, pan, and zoom state.

Preview applies retained static node and shape transforms to positions while generating two-dimensional edges. It does not transform normals, fill faces, shade materials, load textures, infer winding, handedness, up axis, or coordinate space, or create GPU model resources.

Parse, model-assembly, non-finite-position, unrepresentable-transform, empty-bounds, and degenerate-projection failures remain explicit. A Preview failure does not hide Inspector metadata or Archive Explorer controls.

## NIF material and texture-source Inspector

Inspector presents the complete retained document inventory, not only the
property references of the selected Preview mesh. Direct and explicitly opened
archive NIFs share the same Inspector presentation.

### Material properties

Every retained `NiMaterialProperty` appears in source block order and is
identified by source block index. Fields appear in this order:

1. Ambient color - three retained f32 components
2. Diffuse color - three retained f32 components
3. Specular color - three retained f32 components
4. Emissive color - three retained f32 components
5. Glossiness
6. Alpha

Finite values use nine significant decimal digits. Positive and negative zero remain distinct, and infinity and NaN are named explicitly. The text does not clamp, normalize, replace, or convert these values. It makes no claim about valid ranges, lighting, blending, shading, or appearance. An empty inventory is reported directly.

### Texturing properties and sources

Every retained `NiTexturingProperty` and `NiSourceTexture` appears in source block order. Texturing rows preserve the fixed nine-slot sequence, raw presence bytes, conditional descriptors and transforms, Bump and Parallax extensions, and ordered shader records. Raw values are shown as neutral decimal, hexadecimal, or binary32 text without assigning enum or renderer meaning.

Source rows show both retained carriers. Printable ASCII filename bytes remain
literal, backslashes are doubled, and NUL or other non-printable bytes use
uppercase `\xNN` escapes. This is byte-safe metadata display, not path or URL
interpretation. Only the exact validated combination of Use External equal to one,
a valid non-null filename index, and a null Pixel Data reference is labeled
`Supported external source`; every other retained combination is labeled
`Unsupported source combination`.

Studio does not resolve these names, search archives or directories, load or
decode external textures, interpret embedded pixel data, or change Preview.

## Narrow panels

Archive Explorer actions, extraction controls, NIF Preview controls, and NIF
Inspector text use the active panel or work-area width. Controls stay reachable
by shrinking or stacking when needed, and long values wrap instead of requiring
horizontal clipping.

For parser layouts, evidence qualifications, and unsupported format boundaries, read [File format support](file-formats.md).
