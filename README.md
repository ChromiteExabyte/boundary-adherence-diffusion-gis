# Boundary-Adherent GIS Evaluation Tools

Tools for evaluating boundary adherence between GIS-derived masks and AI-generated imagery.

## What's Here

- **AUC-BAS metrics** — boundary precision/recall/F1 across a 1–5 pixel tolerance sweep
- **Batch evaluation** — pairs mask rasters with generated images, outputs CSV/JSON summaries
- **Plotting** — mean boundary-adherence curve with standard deviation shading
- **Synthetic test data** — simple square mask + offset image for sanity checks

## Not Implemented

Vector preprocessing, diffusion model training/inference, and example datasets are not included.
