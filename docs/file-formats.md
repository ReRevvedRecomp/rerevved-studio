# File format support

ReRevved Studio identifies known formats by filename extension. It
reads at most 16 bytes for the generic header display. Recognizing an extension
does not mean Studio can parse that file.

| Format | Current support |
|---|---|
| FPK | Version 6 indexing, selected entry extraction, and embedded document opening from a validated entry |
| DDS | Standard and DX10 metadata parsing; legacy 32-bit RGB/RGBA preview |
| GFX | Uncompressed GFX v2 movie metadata and external image inspection |
| BIK | Extension recognition and generic file inspection |
| MP3 | Decoded waveform preview limited to ten seconds |
| NIF | Gamebryo 20.3.0.9 scene, geometry, material, texturing, source-carrier, assembly, and static wireframe support |
| MAP | Xbox DLC 1088-byte envelope inspection |
| Other regular files | Generic file inspection with an unknown kind |

## FPK version 6

The core parser accepts an explicit range of bytes and returns an entry list.
All integers use little-endian byte order. The 14-byte header contains a 32-bit
version, the `FPK_` signature, two preserved unknown bytes, and a
32-bit entry count.

Each table record has variable length. It contains a 32-bit byte count, that
many uninterpreted bytes aligned to four bytes, two preserved unknown 32-bit
fields, a 32-bit payload size, and a 32-bit payload offset. A record
therefore occupies `20 + align4(byte_count)` bytes. Payload offsets and sizes
use bytes.

The parser rejects truncated tables, arithmetic overflow, records that overlap
the table, and payload ranges outside the archive.

The unknown fields remain uninterpreted, and the variable data is skipped
rather than exposed. The header field order and record alignment are also
documented by the public
[QuickBMS Civilization script](https://mirror.aluigi.org/bms/civilization.bms).

The core can write one selected entry to an explicit destination chosen by the
caller. It checks the selected byte range again, creates only a new file, and
reports selection, range, destination, open, write, and finalization failures
separately from parse errors. It never modifies the source bytes or overwrites
an existing destination.

The embedded-document API can explicitly route one validated selected payload
to the existing DDS, GFX, NIF, MP3, or MAP span parser. The retained archive
bytes and selected range are unchanged, and no temporary retail file is
created. A format mismatch preserves the selected parser's exact malformed,
truncated, or unsupported-content reason. Automatic format naming, bulk
extraction, archive rewriting, and repacking are outside this capability.
Archive navigation and extraction usage are documented in the
[User guide](user-guide.md#archive-explorer).

## DDS

The core parser accepts an explicit range of bytes and returns a metadata
record. It supports the standard 128-byte DDS description and the 148-byte form
with a `DDS_HEADER_DXT10` extension. All fields are 32-bit values in
little-endian byte order. The base layout is the `DDS ` signature, a 124-byte
`DDS_HEADER`, and its embedded 32-byte `DDS_PIXELFORMAT`. The parser reads the
DX10 extension only when the pixel format flags include `DDPF_FOURCC` and the
FourCC is `DX10`.

The parser rejects truncated metadata, an invalid signature, and incorrect
header or pixel format structure sizes. It preserves header values, FourCC
bytes, masks, reserved bytes, capabilities, and raw DX10 fields without
guessing their meaning.

The GPU-free DDS document decoder uses that parsed metadata to preview the
top-level image of a legacy 2D texture. It accepts only 32-bit `DDPF_RGB` data,
with optional `DDPF_ALPHAPIXELS`, non-overlapping contiguous eight-bit RGB
masks, and a matching alpha mask when alpha is declared. It computes the
tightly packed row size from the dimensions and bit count as recommended by
the programming guide, checks the complete top-level pixel range, and produces
RGBA8 pixels. RGB data without declared alpha becomes opaque. Extra mip levels
are not decoded.

DX10 formats, FourCC compression, other bit counts or masks, cubemaps, volume
textures, and truncated pixel data report an unsupported or invalid preview
instead of being guessed. The application viewer owns OpenGL upload,
presentation, and texture lifetime. Preview never modifies or writes a source
file.

The layout and minimum validation follow Microsoft's DDS documentation:

- [DDS programming guide](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide)
- [DDS pixel-format reference](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-pixelformat)
- [DX10 extension reference](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header-dxt10)

## Xbox DLC map envelope

The Studio parser accepts exactly 1088 bytes and preserves them as two separate
ranges: a 1024-byte map core and a 64-byte footer. Shorter inputs report a
truncated record. Longer inputs report an unsupported length.
The parser does not modify the source and does not infer terrain names, validity
rules, map orientation, or a semantic purpose for the footer.

The presentation does not assign terrain meaning or player-facing orientation
to the core bytes, and a non-`0xFF` footer remains structurally inspectable
because the available public evidence does not establish a runtime validity
rule. See the [User guide](user-guide.md#xbox-dlc-map) for grid and hover behavior.

The [public loader-boundary evidence](https://github.com/ReRevvedRecomp/rerevved-research/blob/cddfc1ac6f040595dbdc0247d4566dfb69f10714/manifests/map-authoring-loader-boundary.json) establishes the 32 by 32 core and its 1024-byte read.
It does not establish that runtime acceptance requires the footer, that a bare core
is valid, or that other trailing bytes are tolerated.
Tests use only generated synthetic records and contain no retail bytes.

## GFX

The core parser accepts an explicit range of bytes containing an uncompressed
`GFX` movie. It requires the 32-bit declared file length to match the supplied
range exactly. The parser preserves the file version, signed frame rectangle
coordinates in twips, raw 8.8 frame rate, and frame count. The application
shows both the encoded rectangle and its derived pixel dimensions.

The first tag must be Scaleform ExporterInfo tag 1000 with an exporter version
whose major byte is 2. The parser preserves the raw exporter version, flags,
bitmap format, prefix bytes, and SWF-name bytes. It then walks standard short
and long SWF tag records, checks every payload against the declared file
boundary, and skips tag codes it does not inspect.
A zero-length End tag must finish exactly at the file boundary.

Scaleform DefineExternalImage tag 1001 records preserve the character ID, raw
bitmap format, target dimensions, export-name bytes, and filename bytes. The
records remain in source order, including duplicates. Length-prefixed names
remain byte strings because the available layout evidence does not establish a
text encoding or runtime path-resolution rule. Presentation escapes bytes that
cannot be shown safely.

`CFX` compression, other signatures, non-v2 exporter records, truncated
fields, inconsistent lengths, malformed tag streams, and data after the End
tag report clean failures. Decompression, execution, rendering, export, and
writing are outside the GFX document contract. Inspection behavior is
documented in the [User guide](user-guide.md#gfx).

The public format references are:

- [Adobe SWF file format specification](https://open-flash.github.io/mirrors/swf-spec-19.pdf) - movie header, bit-packed rectangle, frame fields, and tag framing
- [GFxExport documentation](https://help.autodesk.com/cloudhelp/ENU/Scaleform-Help/scaleform_help/gfxexport_reference/usage.html) - external image export behavior

The proprietary tag numbers, field layouts, and exporter major-version requirement come from these pinned Scaleform GFx v2 sources:

- [ExporterInfo](https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxLoaderImpl.cpp)
- [Tag identifiers](https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxTags.h)
- [DefineExternalImage](https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxTagLoaders.cpp)

Only those narrow format facts were used. No Scaleform source code, dependency, or asset was copied into Studio.

## MP3

The core passes an explicit byte range to
[dr_mp3 0.7.3](https://github.com/mackron/dr_libs/tree/5690d4671d7ad07ae6021756d7222eb159745f06)
and returns the decoded sample rate, channel count, and a waveform window. The
core first checks that the complete audio region consists of decodable MP3
frames. It then retains 100 signed peak points per second for at most the first
ten seconds of decoded audio. This bounds PCM decoding and retained waveform
data while leaving the source bytes unchanged.

The document API produces the waveform without an audio device or GPU resource.
Invalid, truncated, or unsupported streams report a clean decode failure.
Playback, transcoding, export, and writing are outside the MP3 contract;
presentation is documented in the [User guide](user-guide.md#mp3). Tests use
only an embedded stream generated from a synthetic sine source; no retail audio,
capture, or decoder output is included.

The decoder is fetched at the exact pinned revision above under its
[public-domain or MIT-0 terms](../licenses/dr_libs-LICENSE.txt). Studio uses
the dependency through its public API and does not copy codec implementation
into the document layer.

## NIF

### Container

The core accepts an explicit byte range containing the supported
Gamebryo NIF variant. It requires matching textual and binary version
20.3.0.9, big-endian type 0, and user version 0. The binary version, user
version, and block count are stored little-endian; subsequent standard numeric
header and footer fields follow the declared big-endian byte order.

The parser preserves exact block-type and string bytes, block type indices and
sizes, group values, and footer root references. It derives each block offset
from the validated header boundary and preceding block sizes. Every count,
length-prefixed byte string, type index, block range, and root reference is
checked against the supplied range, and the footer must end exactly at the file
boundary. Unsafe bytes are escaped only for application display.

The standard field order and mixed-endian types follow these sources from the pinned NifTools revision:

- [Header schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1963-L1982)
- [Footer schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1732-L1736)
- [20.3.0.9 user-version profile](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L220)
- [Endian values](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1228-L1231)

This support is limited to the documented Gamebryo 20.3.0.9 variant; other title
NIF variants are outside the documented contract.

### Scene objects

For exact `NiNode` and `NiTriShape` block types, the core also reads the standard
20.3.0.9 `NiObjectNET` and `NiAVObject` fields: string-table name index,
extra-data and controller references, flags, translation, rotation, scale,
property references, and collision-object reference. Nodes preserve ordered
child and effect references. Triangle shapes preserve their geometry-data and
skin-instance references and the complete standard material-name, material
extra-data, active-material, and update fields. Reference and string indices
must be null or remain inside the corresponding table, array counts must fit
inside the declared block, and each selected block layout must consume its
declared size exactly. Float values remain observable without geometry-validity
rules.

### Material property

For exact `NiMaterialProperty` blocks, the core first validates the inherited
`NiObjectNET` name, extra-data references, and controller reference. It then
retains the unconditional 56-byte derived payload as ambient, diffuse,
specular, and emissive colors of three ordered f32 components each, followed by
one glossiness f32 and one alpha f32. Inventories preserve source block index
and block order. The selected layout has no obsolete Flags, Emissive Mult,
presence value, or version branch, and must consume its declared block exactly.
The parser preserves each decoded f32 bit pattern without arithmetic, clamping, normalization, replacement, color conversion, finite-value policy, or range policy. Research manifest `RVA-F-0129` documents this title-specific retention contract. It does not establish color space, lighting, blending, shading, texture interaction, or visual equivalence.

The standard type and profile conditions are corroborated by:

- [NiMaterialProperty schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3978-L3988)
- [Color3 definition](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1583-L1588)

### Texturing property and source texture

For exact `NiTexturingProperty` blocks, the core validates the inherited
`NiObjectNET` payload before retaining the raw u16 Flags and requiring the
supported Texture Count of nine. The nine standard slots remain in the fixed
Base, Dark, Detail, Gloss, Glow, Bump, Normal, Parallax, and Decal 0 order.
Every raw presence byte is retained; a descriptor body follows only when that
byte equals one. Each retained descriptor contains its validated null-or-in-range Source block reference, raw u16 Flags, raw Has Texture Transform byte, and the optional transform selected only when that byte equals one. A transform
retains two translation f32 values, two scale f32 values, rotation f32, raw u32
Transform Method, and two center f32 values in serialized order. Present Bump
and Parallax slots additionally retain their unconditional six-f32 and one-f32
tails. The raw shader count controls an ordered shader record sequence; each
record retains its raw Has Map byte and includes a descriptor plus raw u32 Map
ID only when that byte equals one.

For exact `NiSourceTexture` blocks, the same inherited validation precedes one
packed 24-byte derived payload: raw Use External byte, validated File Name
string index, validated Pixel Data block reference, raw Pixel Layout, Use
Mipmaps, and Alpha Format u32 values, then raw Is Static, Direct Render, and
Persist Render Data bytes. Both source carriers occupy those positions for
either Use External value. A retained source is classified as the supported
external relationship only when Use External equals one, File Name is valid
and non-null, and Pixel Data is the standard null reference. Other structurally
valid combinations remain retained but unsupported; a non-null Pixel Data
target stays opaque.

Both inventories preserve source block order, raw bytes, component order, and f32 bit patterns without the title's Flags mask, regenerated presence values, discarded Map ID, normalization, clamping, enum policy, or finite-value policy. Counts must fit the remaining declared block before allocation, every reference and string index must be null or in range, and each selected block must be consumed exactly.

Research manifests `RVA-F-0130` and `RVA-F-0131` document these title-specific retention contracts. They do not establish external path lookup, embedded pixel decoding, DDS encoding, UV behavior, sampling, filtering, wrapping, color space, shader or material behavior, rendering, editing, conversion, export, or writing.

These inherited layouts follow the pinned NifTools revision:

- [NiFixedString definition](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L352-L354)
- [NiObjectNET schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3359-L3367)
- [NiAVObject schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3440-L3494)
- [NiNode schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L4384-L4389)
- [MaterialData and NiGeometry schemas](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3840-L3869)
- [NiTriBasedGeom schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3872-L3874)
- [NiTriShape schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5278-L5280)

### Geometry data

For exact `NiTriShapeData` blocks, the core preserves the existing scalar
geometry inventory: group ID, vertex count, raw keep, compression, data,
consistency, and presence flags, bounding sphere, additional-data reference,
triangle and triangle-point counts, and match-group count. It also retains
ordered three-float vertex positions, ordered three-float normal vectors,
ordered three-u16 triangle selectors, and every nested raw-u16 normal-sharing
group. Absent conditional arrays produce empty containers.

The low six data-flag bits derive the UV-set count, while bit `0x1000` and the
raw normals flag determine whether tangent and bitangent arrays are present.
Those two arrays, vertex colors, and UV coordinates remain bounds-checked skips
outside the retained API. One-byte presence fields retain their raw values and
use nonzero truthiness. Every retained or skipped array and nested group must
fit inside the declared block before allocation or reading, and the selected
layout must consume the block exactly.

The parser does not impose rules on group IDs, float values, bounds, flag
values, triangle-point arithmetic, topology, selectors, winding, or UV ranges.
In particular, triangle and normal-sharing selectors retain their source order
and raw values without a vertex-count range check. Safe selector use remains a
later model-renderability boundary; Studio does not generate, normalize,
repair, deduplicate, reorder, or reinterpret the retained geometry.

The inherited layout, flag widths, fixed element widths, and conditional arrays follow these sources from the pinned NifTools revision:

- [NiGeometryData and NiTriBasedGeomData schemas](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3876-L3925)
- [NiTriShapeData schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5282-L5289)
- [Data-flag layout](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1629-L1633)
- [One-byte boolean rule](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L296-L298)
- [Divinity-II user-version condition](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L30-L32)
- [Extra geometry fields](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3897-L3898)

The accepted user version is zero, so the extra geometry fields do not apply.

The pinned schema describes
[MatchGroup values as vertex indices](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1745-L1749),
but title consumer evidence
([RVA-F-0109](https://github.com/ReRevvedRecomp/rerevved-research/blob/40add4aed55a443b42126226efdf95d010887e5c/manifests/nif-trishapedata-normal-group-consumer.json))
establishes that each nested list selects normal records that receive a shared
normalized result. Studio retains these ordered normal-sharing groups without
applying a vertex-count range rule. Model assembly does not consume or
range-check their selectors. The parser still imposes no float,
bounding-sphere, topology, winding, duplicate-selector, color, normal, or UV
validity rules.

### Model assembly and presentation

Block payloads not covered above are skipped without interpreting animation,
skinning, or unsupported scene semantics. Model assembly walks validated footer
roots and ordered, nested `NiNode` children.
Exact `NiTriShape` blocks become mesh descriptors only when their data reference
resolves to parsed `NiTriShapeData`, positions and triangles are present and
nonempty, every triangle selector addresses a retained position, and any
present normal array has the position count. Null references and valid
unsupported child types are skipped. Active-path node cycles, wrong-type data references, inconsistent retained geometry, out-of-range triangle selectors, and scenes with no supported meshes are explicit assembly errors. They do not make structural parsing reject the document.

Each assembled mesh records its footer-root index, ordered node path, parsed
shape and data indices, and directly referenced `NiMaterialProperty` block
indices in source order. It also records one double-precision world transform
composed from the shape local followed by its node path from immediate parent
to root. The retained f32 translation, column-major 3-by-3 rotation, and uniform
scale remain unchanged in `NifDocument`. Composition follows
`s_world = s_parent * s_local`, `R_world = R_parent * R_local`, and
`t_world = t_parent + s_parent * (R_parent * t_local)`; applying a transform is
`translation + scale * (rotation * position)`. Research manifest `RVA-F-0125`
documents this title-specific contract. The descriptor contains no geometry
arrays, transformed vertex copies, or GPU state.

Direct and explicitly opened archive NIFs use the same wireframe and Inspector
behavior. The application draws transformed positions as wireframe edges and
presents retained material, texturing, and source inventories as read-only text.
Presentation does not alter the format contract,
copy geometry arrays, transform normals, evaluate materials, resolve source
names, load textures, decode pixel data, or create GPU model resources. Current
controls, deterministic fitting, numeric formatting, byte escaping, and
empty-state behavior are documented in the
[User guide](user-guide.md#direct-and-embedded-nif-wireframes).

Studio makes no winding, front-face, handedness, up-axis, or coordinate-space
claim and does not convert, export, or write NIF content. Tests construct
wholly synthetic containers, including one complete material-positive
untextured model. No retail assets or captures are included.
