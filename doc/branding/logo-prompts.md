# Prompt per il nuovo logo (Gemini)

Ordine: generare prima il prompt 1 e scegliere una variante. Per i prompt 2, 3 e 4 allegare a Gemini l'icona scelta.
Nessuna immagine contiene testo: il nome del progetto viene aggiunto dopo.
Salvare le immagini scelte in `doc/branding/source/` con i nomi indicati.

## 1. Icona principale -> `icon.png`

```text
Design a modern app icon for a desktop 3D-printing slicer with a built-in AI print assistant.

Concept: a stylized 3D printer nozzle seen from the side, depositing three stacked horizontal layer lines; a small four-point spark (like a subtle "smart assistant" sparkle) sits at the upper right of the nozzle.

Style: flat vector look, bold simple geometric shapes, thick clean outlines, no gradients or only one very soft gradient, no photorealism, no 3D rendering, no text, no letters, no numbers.
Colors: at most two colors plus white: deep teal (#0F766E) and warm orange (#F97316) for the spark.
Composition: centered, fills about 80% of the canvas, generous safe margin, perfectly square 1:1.
It must stay readable at 16x16 and 32x32 pixels: avoid thin lines and small details.
Background: plain solid white, nothing else on the canvas.
Do not resemble the Creality, Bambu Lab, Prusa or OrcaSlicer logos; avoid any "C" letter shape.

Output: a single square 1024x1024 PNG. Give me 4 different variations of this concept.
```

## 2. Simbolo monocromatico -> `glyph.png` (allegare `icon.png`)

```text
Using the attached app icon as reference, create a single-color glyph version of the same symbol.
Pure black (#000000) shapes on a plain solid white background, no gradients, no shading, no outline-only thin strokes.
Simplify details so it reads clearly at 20x20 pixels: thick shapes, fewer layer lines if needed.
Centered, square 1:1, about 80% of the canvas. No text. Output one 1024x1024 PNG.
```

## 3. Schermata di avvio -> `splash.png` (allegare `icon.png`)

```text
Using the attached app icon as reference, design a desktop application splash screen background, landscape 860x480 ratio (43:24).
Dark background: deep charcoal (#111827) with a very subtle teal (#0F766E) glow and faint horizontal 3D-print layer lines texture.
Place the app icon on the left third, vertically centered, about 40% of the height.
Leave the right two thirds calm and empty: I will add the product name and version text there myself.
Flat modern style, no text, no letters, no people, no printers. Output one PNG at 1720x960.
```

## 4. Banner dell'installer -> `installer-banner.png` (allegare `icon.png`)

```text
Using the attached app icon as reference, create a vertical installer side banner, portrait ratio 164x314 (about 1:1.91).
Deep teal (#0F766E) to charcoal (#111827) vertical background, faint stacked layer-line texture.
App icon in the upper third, centered, about 60% of the width, white or light version of the symbol.
Lower two thirds simple and calm. No text, no letters. Output one PNG at 656x1256.
```
