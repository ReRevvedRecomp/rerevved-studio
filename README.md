<p align="center">
<img src="res/rerevved_studio_logo.png" alt="ReRevved Studio" width="192">
</p>

<h1 align="center">ReRevved Studio</h1>

<p align="center">
The content workbench for ReRevved.
</p>

<p align="center">
<a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
<a href="https://github.com/ReRevvedRecomp/rerevved-studio/actions/workflows/checks.yml"><img src="https://github.com/ReRevvedRecomp/rerevved-studio/actions/workflows/checks.yml/badge.svg" alt="Checks"></a>
</p>

## About

ReRevved Studio lets you browse, inspect, and preview legally owned game content while preserving the original source files. Extraction creates a new destination instead of changing an archive in place.

Studio currently covers inspection, preview, and single-entry extraction. Its
format support is designed to expand into editing, conversion, validation, and
export. [ReRevved](https://github.com/ReRevvedRecomp/rerevved) and the SDK
remain responsible for installation, activation, launching, and runtime
behavior.

## Highlights

- Browse version-6 FPK archives, inspect entries, and extract one entry to a new destination.
- Preview supported DDS images, MP3 waveforms, and Xbox DLC map data.
- Inspect supported GFX metadata without executing the movie.
- Explore supported NIF scenes as transformed static wireframes, with read-only material and texture-source metadata.

## Documentation

- [User guide](docs/user-guide.md)
- [File format support](docs/file-formats.md)
- [Architecture](docs/architecture.md)
- [Development](docs/development.md)
- [Contributing](CONTRIBUTING.md)

Retail game files and extracted assets are not distributed by this project. ReRevved Studio is not affiliated with or endorsed by the publisher. All trademarks belong to their respective owners.

## License

<a href="LICENSE"><img src="https://www.gnu.org/graphics/gplv3-127x51.png" alt="GNU General Public License version 3"></a>

Source code and documentation use the [GNU General Public License version 3 only](LICENSE). External components remain under their respective licenses; see [dependency licenses](licenses/README.md).
