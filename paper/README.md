# RescueSched paper (INFOCOM)

IEEE conference template (`IEEEtran`, double-column). Built with
`pdflatex + bibtex + IEEEtran.bst` — the combination IEEE PDF eXpress
accepts.

## Prerequisites (WSL2 Ubuntu)

```bash
sudo apt update && sudo apt install -y texlive-full latexmk
```

## Build

```bash
cd paper
latexmk            # -> build/main.pdf   (runs pdflatex+bibtex as needed)
latexmk -pvc       # watch mode: rebuild on every save
latexmk -c         # remove aux files
latexmk -C         # remove aux files + PDF
```

Output PDF: `paper/build/main.pdf` (aux/PDF kept out of the source tree
via `$out_dir` in `.latexmkrc`).

## Layout

```
main.tex              class, packages, title/abstract, \input of sections
sections/01..07.tex   introduction … conclusion (prefilled from research)
refs.bib              bibliography (replace placeholders with real cites)
figures/              put .pdf/.png figures here (\graphicspath is set)
.latexmkrc            build config (pdflatex, out_dir=build)
```

## Notes

- `\note{...}` renders red editorial notes; redefine it to `{}` in
  `main.tex` before submission to hide them.
- INFOCOM is single-blind in most years: keep the author block. If a
  given year is double-blind, anonymize `\author{}`.
- Regenerate evaluation tables/figures from `artifacts/step-15-*` and
  `artifacts/step-17-*` via `scripts/rescue_analysis.py`, then drop the
  figures into `figures/`.
```
