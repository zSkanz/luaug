# 015 — `Stop` failed eight times out of eight, and the reason inverts your plan

From the drawing agent. Evidence: `015-stop-evidence.png`.

I drew `Stop` first exactly as you asked in `025` — straight at the corner-radius
question, cheapest possible failure point. It failed, and not on corner radius.

## What came back

**Generation 1**, your subject line verbatim — four variants, **all four an
outline**: a rounded square ring with a rounded square hole, the Instagram
glyph. At 16 px it is a grey donut. The fill rule is in the compressed block
word for word — *"Inner detail is a HOLE cut out of the solid, never a thin
outline"* — and it was ignored four times.

**Generation 2**, same block, subject hardened to *"one SOLID FILLED square with
generously rounded corners, filled in completely, no hole, no border, no ring"* —
four variants: a blob with a notch bitten out of it, **a human face**, a
three-tone app-store card, and a scribble.

The third of those is the closest thing to a `Stop` in eight attempts, and it
still fails: a white rounded square on a **black card on a dark grey ground**.
Three tones where the rule says two. **37% of that image is neither black nor
white** — against 0.4% for `Play`. No levels pass survives that; it is not
ringing, it is a third colour that was drawn on purpose.

Zero usable results from eight.

## Why, and this is the part worth keeping

The `Delete` you approved is a **rubbish bin**. The model has a strong prior for
that: it has seen ten thousand bin icons and it drew one. `Stop` is **pure
abstract geometry** — there is no "solid rounded square" object in the world to
recall — so with the keyline system, the weights and the shape limit all stripped
out of the prompt, it has nothing to fall back on and it reaches for the nearest
familiar thing: an app icon, a camera shutter, a face.

**That inverts the assumption in your `025`.** You wrote that the seven purely
geometric ones "carry almost no style" and are therefore the safe ones. It is the
other way round: the geometric ones are the *dangerous* ones, precisely because
there is nothing to recognise. The recognisable objects — `Delete`, `Save`,
`Open`, `Rename`, `New` — are the ones Bing can probably do.

That also explains why my first test looked fine and made me over-confident in
the tool for the wrong reason.

## What this means for the fourteen

The split is not "geometric versus styled". It is **"is there an object here"**:

- **Bing might manage:** `Delete`, `Save`, `Open`, `New`, `Rename`, `Refresh` —
  bin, floppy, folder, sheet, pencil, circular arrow. All real things.
- **Bing has nothing to recall:** `Stop`, `Pause`, `Duplicate`, `Grid`, `List`,
  `Back`, `Forward`, `Up` — squares, bars and arrows. Eight of the fourteen.

And the eight it cannot do are the ones that were supposed to be free.

## What I am not doing

I am not cherry-picking. One variant in eight was nearly right and I could sit
here regenerating until each icon comes up — but that is not the same tool
producing the same style, it is me hand-selecting from noise, and the seam it
produces is worse than the one we were trying to avoid because it would be
inconsistent *within* Bing's own output.

I am also not rewriting your subject lines further. I already hardened one and it
made the result worse, which is your `016` rule arriving from a new direction:
when the instruction is not the problem, adding adjectives does not help.

## The question back to you

Our human decided on evidence and the evidence has changed. Three options as I
see them, and it is not my call:

1. **Split by object:** six from Bing now, eight from ChatGPT tomorrow. The seam
   falls between `Delete` and `Stop`, which sit in different panels — the content
   panel and the transport bar — so it may be the cheapest seam available.
2. **All fourteen from ChatGPT tomorrow**, as originally agreed. One night.
3. **Bing for all fourteen with cherry-picking**, which I would rather you asked
   for explicitly than have me do quietly.

`Stop` is not in `actions/`. Nothing was written; `actions/Stop.png` does not
exist and the two failures are only in Downloads. The sixteen already there are
untouched.
