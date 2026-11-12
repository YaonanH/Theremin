# Editing and publishing this book

## Project layout

The firmware and documentation live together so that a code change can include
its explanation in the same revision:

```text
theremin_project/
  main/                 ESP32 firmware and embedded control interface
  tests/                Firmware host checks
  docs/                 Book chapters
    figures/            Project photos and diagrams
  myst.yml              Book metadata and theme
  toc.yml               Chapter order, following the TU Delft starterkit
  .github/workflows/    GitHub Pages workflow adapted from the TU Delft approach
  package.json          Documentation commands and pinned MyST dependency
  package-lock.json     Resolved documentation dependencies
  docs.cmd              Windows command wrapper
  README.md             Quick setup and firmware instructions
```

`_build/`, `node_modules/`, and the optional `.docs-tools/` runtime are generated
local files, excluded from version control. The documentation is not embedded in
the ESP32 and does not change the firmware's CMake build.

## Install and preview

Install Node.js LTS with npm if it is not already installed. In PowerShell at the
project root, run:

```powershell
.\docs.cmd install
.\docs.cmd preview
```

The wrapper uses Node.js from PATH, or the project-local runtime under
`.docs-tools/` when available. It does not download a runtime itself. The `.cmd`
wrapper works from PowerShell or Command Prompt. Alternatively, use the npm
commands in a terminal with Node.js and npm on PATH:

```powershell
npm.cmd ci --ignore-scripts --no-audit --no-fund
npm.cmd run docs:dev
```

Open the local address printed by MyST, usually **http://localhost:3000**.
Keep the terminal running while editing; press Ctrl+C to stop. The first preview
or HTML build needs internet access to download the book theme and its libraries.
Connect to an internet-capable network for this step.

## Build static HTML

```powershell
.\docs.cmd build
```

With Node.js and npm on PATH, the equivalent is:

```powershell
npm.cmd run docs:build
```

The command builds HTML under `_build/html/` and enables strict checking so MyST
warnings must be resolved. Serve the generated directory with a static web server
when checking a production build; use the preview command for ordinary writing.

## Add a chapter

1. Create a Markdown file under `docs/`, beginning with a descriptive `# Heading`.
2. Add its path to `project.toc` in `toc.yml` at the intended sidebar position.
3. Link to related chapters using relative Markdown links, such as
   `[Hardware](hardware.md)`.
4. Preview the page and run the strict HTML build before committing.

The project home page is `docs/index.md`. Keep claims tied to the implementation
or recorded measurements. Update the file-summary table in `docs/firmware.md`
when responsibilities or filenames change.

## Add figures

Put original photos and schematics in `docs/figures/`, using descriptive filenames.
After adding the actual file, include it in a chapter with a MyST figure:

````markdown
```{figure} figures/prototype.jpg
:alt: ESP32 prototype connected to two oscillators and an amplifier
:name: prototype-photo

The assembled prototype, with pitch and volume input buffers visible.
```
````

This example filename is illustrative; add the file before using the directive.
Include units and test conditions on plots. Attribute external figures and check
their reuse terms. The home page's system diagram is written as Mermaid source,
so it can be edited without exporting a bitmap.

## Publish with GitHub Pages

The project is connected to [YaonanH/Theremin](https://github.com/YaonanH/Theremin).
The included `.github/workflows/deploy.yml` adapts the Pages publishing approach
of the [TU Delft starterkit](https://github.com/TUD-JB-OS/starterkit). There is no
need to run `myst init --gh-pages`.

The starterkit delegates to `TUD-JB-OS/deploy`. The reviewed version of that shared
publisher (`aac8cc664247081f43161bea0d93fa8bab99fc00`) installs MyST 1.11.0.
That version failed with the current cached theme on this Windows setup. This
project therefore includes its own HTML-only Pages workflow using Node.js 24 and
the same locked MyST 1.12.0 dependency as local builds. It retains the starterkit's
`book-theme`, separate table of contents, and Pages publication pattern. PDF and
notebook execution steps can be added when the report requires them.

To publish:

1. Open [repository Pages settings](https://github.com/YaonanH/Theremin/settings/pages).
2. Set **Build and deployment > Source** to **GitHub Actions**.
3. Commit and push these files from the project root:

   ```powershell
   git add .github/workflows/deploy.yml myst.yml toc.yml package.json package-lock.json README.md docs/writing.md docs/figures/favicon.svg
   git diff --cached
   git commit -m "Configure TU Delft documentation deployment"
   git push origin main
   ```

4. Open the repository's **Actions** tab and check **Deploy documentation**.
5. Open the URL reported by the successful deployment, expected to be
   **https://YaonanH.github.io/Theremin/**.

The workflow runs when documentation/configuration changes are pushed to `main`.
After the workflow has been pushed, **Actions > Deploy documentation > Run workflow**
also starts a build manually. The included publisher sets
`BASE_URL` to `/Theremin` for this project; that subpath makes its assets resolve
under GitHub Pages. Local preview continues to use the root path.

For a different repository, update `project.github` in `myst.yml` and the URLs in
this guide. Keep the branch in the deployment workflow aligned with that repository.

## PowerShell script-policy error

PowerShell can select `npx.ps1` or `npm.ps1` instead of the Windows command
wrappers. If it reports that running scripts is disabled, invoke `npx.cmd` or
`npm.cmd` explicitly, or use `docs.cmd` for the existing local build/preview
commands. No execution-policy change is needed.

For example, this command is useful only if you intentionally want MyST to
generate a different workflow; the TU Delft workflow is already included here:

```powershell
npx.cmd --no-install myst init --gh-pages
```

When using the optional project-local Node.js runtime, first put its directory on
the current terminal's PATH, as `docs.cmd` does automatically:

```powershell
$env:PATH = "$PWD\.docs-tools\node-v24.22.3-win-x64;$env:PATH"
```

## References and inspiration

- [MyST quickstart](https://mystmd.org/guide/quickstart)
- [MyST table of contents](https://mystmd.org/guide/table-of-contents)
- [MyST GitHub Pages deployment](https://mystmd.org/guide/deployment-github-pages)
- [TU Delft starterkit](https://github.com/TUD-JB-OS/starterkit)
- [TU Delft shared publisher](https://github.com/TUD-JB-OS/deploy/blob/aac8cc664247081f43161bea0d93fa8bab99fc00/.github/workflows/jboss.yml)
- [Example theremin thesis](https://ingeschmidt.github.io/Thesis_ISS_SP/)
- [Example source repository](https://github.com/IngeSchmidt/Thesis_ISS_SP)

The TU Delft starterkit uses MyST with `book-theme`, separate chapter files, a
`toc.yml`, and a shared deployment workflow. Its README states that the book is
licensed under Creative Commons Attribution-NonCommercial 4.0. The publishing
configuration here is adapted from starterkit revision
`bf0e6d36df07c8982d423d24a69d8e3775a91a3e`; the theremin chapters are original
project documentation. Template sample authors, thesis metadata, and university
branding have not been copied into this project. PDF export, author metadata,
custom branding, and bibliography configuration can be added when the report
needs them.
