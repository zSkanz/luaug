# 011 — tested Bing Image Creator as a second source. It cannot hold the brief.

From the drawing agent. Our human asked me to try Bing while the ChatGPT quota
is out. I drew `Delete` there. Recommending against it, on three grounds, the
first of which is decisive on its own.

## 1. Part A does not fit. The field caps at 480 characters.

`PROMPT.md`'s style block plus one subject line is **3668 characters**. Bing's
prompt box is `maxlength=480` and the Generate button stays disabled above it —
I confirmed by putting the full block in programmatically and watching the
counter go red at *3668 / 480*.

So the block cannot be sent unedited, and "unedited" is the rule `README.md`
opens with. To get anything at all I had to write a **different, compressed
prompt** — about an eighth of the brief, keeping only white-on-black, flat
silhouette, fill-not-outline, no text, 80% centred. Everything else is gone:
the keyline grid, the four keylines, 85 / 35 / 120, the four-shape limit.

That means the test below is not even a fair one. It is the best case for Bing,
not a like-for-like.

## 2. It returns JPEG, not PNG

The blob comes back `image/jpeg`. On a hard black-and-white mask that matters
more than usual, because the batch step reads luminance as alpha and JPEG
ringing is grey:

```
                  neither black nor white
Bing (JPEG)                 2.66%
ChatGPT (PNG)               0.92%
```

Three times as many pixels that the mask conversion has to guess about, and
unlike anti-aliasing they are not all on the edge.

## 3. At 16 px it does not match the family

Beside `Locked`, `Settings`, `Search`, `Script` and `Folder` it reads visibly
paler and softer — a grey slab where the others are white shapes. Contact sheet
sent to our human.

Measured, with the compressed prompt:

```
shape ratio    0.81   against the Tall keyline's 0.80   <- good
shape height   636 px against 854                       <- 74% of the keyline
margins        254 px against the 85 specified
```

The **proportion** it got right without being told, which is interesting. The
**fill** it did not: it left the shape at three quarters of its keyline, which
is the "looks shrunken beside the others" failure the brief names as the single
most visible flaw in a set. With no keyline paragraph in the prompt there was
nothing to hold it.

## What was good, in fairness

The drawing itself is faithful to the subject line: tapered solid body, narrow
slots cut out rather than drawn, separate lid bar with a handle, no text, no
gradient inside the shape. Bing understood the subject. It is the *system*
around the subject that will not fit through a 480-character field.

## Recommendation

Wait for the quota rather than mix hands. Fourteen icons at stake against
fifty-eight already drawn, and the thing that would break is the thing this
whole brief exists to protect.

If a second source is ever genuinely needed, the requirement to check first is
**prompt length**, not image quality — any tool that cannot take 3700 characters
cannot draw for this set at all.
