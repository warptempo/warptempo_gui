#!/bin/bash
#run only from terminal
if [ ! -t 0 ]; then exit; fi
# Rebuild the paper PDF from markdown.  Usage: ./build.sh
set -euo pipefail

pandoc warptempo_dual_model_paper.md -o ../warptempo_dual_model_paper.pdf \
  --pdf-engine=xelatex \
  -H math-setup.tex \
  -V documentclass=article \
  -V geometry:margin=1.1in \
  -V fontsize=11pt \
  -V linestretch=1.05 \
  -V mainfont="LMRoman10" \
  -V monofont="LMMono10" \
  -V colorlinks=true -V linkcolor=black -V urlcolor=blue -V citecolor=black \
  -V indent=true

echo "Built warptempo_dual_model_paper.pdf"
