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
  myst.yml              Book metadata, chapter order, and theme
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
npm ci --ignore-scripts --no-audit --no-fund
npm run docs:dev
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
npm run docs:build
```

The command builds HTML under `_build/html/` and enables strict checking so MyST
warnings must be resolved. Serve the generated directory with a static web server
when checking a production build; use the preview command for ordinary writing.

## Add a chapter

1. Create a Markdown file under `docs/`, beginning with a descriptive `# Heading`.
2. Add its path to `project.toc` in `myst.yml` at the intended sidebar position.
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

## Publish later with GitHub Pages

This starter does not create a GitHub repository or publish a website. Once the
project is in a repository under your account:

1. Add the real repository URL as `project.github` in `myst.yml`.
2. From the Git repository root, generate the publishing workflow:

   ```powershell
   npx --no-install myst init --gh-pages
   ```

   If using the local runtime wrapper instead of a Node.js installation, put
   `.docs-tools/node-v24.22.3-win-x64/` on the current terminal's PATH first, or
   install Node.js LTS with npm normally.

3. Review the generated `.github/workflows/` file and select the intended branch.
4. In the GitHub repository, choose **Settings → Pages → Source → GitHub Actions**.
5. Commit and push the documentation, `myst.yml`, package files, and workflow.

MyST's generator configures the repository subpath for GitHub Pages. The site
will normally appear at `https://USERNAME.github.io/REPOSITORY/`. Use the actual
address reported by the successful deployment.

## References and inspiration

- [MyST quickstart](https://mystmd.org/guide/quickstart)
- [MyST table of contents](https://mystmd.org/guide/table-of-contents)
- [MyST GitHub Pages deployment](https://mystmd.org/guide/deployment-github-pages)
- [Example theremin thesis](https://ingeschmidt.github.io/Thesis_ISS_SP/)
- [Example source repository](https://github.com/IngeSchmidt/Thesis_ISS_SP)

The example repository uses MyST with `book-theme`, separate chapter files, and a
table of contents. This project uses that publishing approach with its own
documentation content. PDF export, custom branding, and bibliography configuration
can be added when the report needs them.
