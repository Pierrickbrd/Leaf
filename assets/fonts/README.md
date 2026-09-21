# Fonts used by Leaf

These typefaces are embedded in the Leaf Ubuntu client and copied into its resources at
build time.

| File | Weight | Role |
|---|---|---|
| `BarlowCondensed-SemiBold.ttf` | 600 | series names under a cover — narrow enough that "Parasite · Édition originale colorée" fits |
| `BarlowCondensed-Bold.ttf` | 700 | screen titles |
| `Inter_18pt-Regular.ttf` | 400 | running text, and figures |
| `Inter_18pt-Medium.ttf` | 500 | buttons, emphasis |

Leaf uses static weights rather than variable files. These four faces cost 875 kB and give
the client the exact weights its interface needs. Inter's 18pt optical size is the one
drawn for text at 12–15 px; the 24pt and 28pt cuts are for display sizes this interface
does not have.

Verified by reading the tables rather than assuming:

- **`ū`, `ō`, `Ō`, `é`, `à`, `ç`, `·`, `…` are present in all four.** The library holds
  *Haikyū*, *Yūsei Matsui* and *Tsugumi Ōba*, and a missing macron shows as an empty box in
  the middle of an author's name. Barlow Condensed carries 525 characters, Inter 2 849.
- **All four declare `tnum`**, so figures can be made tabular — a list of volumes is a
  column of numbers, and without fixed width they dance from line to line.

Both families are under the SIL Open Font License; the two licence files ship beside them,
which is what the licence asks.
