# Editing and publishing

The documentation uses MyST Markdown with `book-theme`, following the TU Delft
starterkit's chapter and navigation structure. [TU Delft starterkit](https://github.com/TUD-JB-OS/starterkit)

## Files to edit

| Path | Purpose |
| --- | --- |
| `docs/index.md` | Website home page |
| `docs/*.md` | Documentation chapters |
| `docs/figures/` | Photos, schematics, and other figures |
| `toc.yml` | Sidebar order and chapter hierarchy |
| `myst.yml` | Book title, repository URL, theme, and site options |
| `package.json`, `package-lock.json` | Documentation commands and locked dependencies |
| `docs.cmd` | Windows helper for installation, preview, and build |
| `.github/workflows/deploy.yml` | GitHub Pages build and deployment |

## Preview locally

Open PowerShell in the project folder and run the installation command once, or
after dependency changes. Then start the preview.

```powershell
.\docs.cmd install
.\docs.cmd preview
```

Open the address printed in the terminal, usually **http://localhost:3000**.
Keep the terminal running while editing and press Ctrl+C to stop the server.
The first run needs internet access to download the book theme and libraries.

The helper finds Node.js on PATH or uses the optional runtime in `.docs-tools/`.
Install Node.js LTS with npm if neither is available. When Node.js is on PATH,
the equivalent commands are available directly.

```powershell
npm.cmd ci --ignore-scripts --no-audit --no-fund
npm.cmd run docs:dev
```

PowerShell can select `npm.ps1` or `npx.ps1` and report that running scripts is
disabled. The explicit `.cmd` extension selects the Windows command wrapper.

## Add or update a chapter

1. Edit a Markdown file under `docs/`, or create a new one with a descriptive
   top-level heading.
2. Add new chapter paths to `project.toc` in `toc.yml`.
3. Use relative links between chapters, such as `[Hardware](hardware.md)`.
4. Update descriptions and configuration values when the implementation changes.
5. Preview the result and run the HTML build.

Place actual figure files in `docs/figures/` before adding their references.
The following example shows the syntax for a project photo.

````markdown
```{figure} figures/prototype.jpg
:alt: ESP32 connected to the oscillator buffers and audio amplifier
:name: prototype-photo

The assembled theremin prototype.
```
````

Mermaid diagrams are stored directly in the Markdown source. Edit the diagram's
nodes and arrows when the data flow changes. Attribute reused figures and
include units and test conditions on measurement plots.

## Build the website

```powershell
.\docs.cmd build
```

The command generates HTML in `_build/html/` with strict MyST validation. Resolve
reported documentation warnings before publishing. `_build/`, `node_modules/`,
and `.docs-tools/` are ignored generated directories.

## Publish to GitHub Pages

The repository includes the deployment workflow and uses Node.js 24 with locked
MyST 1.12.0 dependencies for local and GitHub builds. It publishes HTML when
documentation or configuration changes are pushed to `main`.

1. Set **Build and deployment > Source** to **GitHub Actions** in the repository's
   Pages settings. [Pages settings](https://github.com/YaonanH/Theremin/settings/pages)
2. Commit the documentation changes and push them to `main`.

   ```powershell
   git add docs toc.yml myst.yml README.md
   git diff --cached
   git commit -m "Update theremin documentation"
   git push origin main
   ```

3. Check the **Deploy documentation** run. The workflow also supports **Run
   workflow** from its Actions page. [Workflow runs](https://github.com/YaonanH/Theremin/actions)
4. Open the URL reported by a successful deployment. The expected address is
   [ESP32 Theremin documentation](https://YaonanH.github.io/Theremin/).

The workflow sets `BASE_URL` to `/Theremin` so that navigation and assets resolve
under the repository's Pages address. Local preview uses the root path.

### Pages setup error

If **Configure Pages** reports **Get Pages site failed** with **Not Found**, open
the repository's Pages settings and select **GitHub Actions** as the publishing
source. Then rerun the failed job. A plan or repository-policy message on that
screen identifies any additional setup requirement.

### Attribution and reference material

The publishing setup adapts the TU Delft starterkit approach from revision
`bf0e6d36df07c8982d423d24a69d8e3775a91a3e`. Its README gives the book's
Creative Commons Attribution-NonCommercial 4.0 license. This project's workflow
installs its locked MyST dependency directly so local and hosted builds use the
same version. [Starterkit source](https://github.com/TUD-JB-OS/starterkit/tree/bf0e6d36df07c8982d423d24a69d8e3775a91a3e)

- [MyST quickstart](https://mystmd.org/guide/quickstart)
- [MyST table of contents](https://mystmd.org/guide/table-of-contents)
- [MyST GitHub Pages deployment](https://mystmd.org/guide/deployment-github-pages)
- [Example theremin thesis](https://ingeschmidt.github.io/Thesis_ISS_SP/)
