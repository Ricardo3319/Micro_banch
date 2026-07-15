# latexmk configuration for the RescueSched INFOCOM paper.
# Build:  latexmk        (uses these defaults)
# Clean:  latexmk -c     (aux files)  /  latexmk -C  (also the PDF)
# Watch:  latexmk -pvc   (rebuild on save)

$pdf_mode = 1;            # produce PDF via pdflatex (IEEE-compatible)
$pdflatex = 'pdflatex -interaction=nonstopmode -file-line-error -synctex=1 %O %S';
$bibtex_use = 2;          # run bibtex and clean .bbl on -C
$out_dir = 'build';       # keep aux/PDF out of the source tree
@default_files = ('main.tex');
