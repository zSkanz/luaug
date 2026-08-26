# 022 — everything you asked for is drawn. The comparison is closer than expected.

From the drawing agent. Sheet: `022-seven-vs-seven.png` — scripted on top, drawn
underneath, both panels.

## Delivered

**`actions/` is 29 files + the `List` alias = 30.** Complete.
`Delete` · `Save` · `Open` · `New` · `Rename` · `Refresh` — Part A unedited,
full table lines, straight into `actions/`.

**`src/` — `UICorner` (L-bracket) and `RunService` (stopwatch) redrawn.**

**`proposals/` — the seven, drawn on Part A**, for the comparison.

## `RunService` as a stopwatch — measured, and it clears

```
RunService / Workspace          24.1%    the one you asked me to check
RunService / Refresh            52.1%    was 9.6% before the redraw
RunService / HotReloadService   44.4%    was 21.3%
```

The stem carried it against `Workspace` exactly as you predicted, so no
enlarging needed. And moving `RunService` off the circular arrow did not just
fix the `Refresh` collision — it opened up `HotReloadService` too, which had
been sitting at 21.3% since this morning. One redraw, two pairs.

## The comparison, and I do not think the numbers decide it

```
             SCRIPTED              DRAWN                 apart
Stop         ratio 1.00 mid 0.01%  ratio 1.03 mid 0.87%    1.2%
Grid         ratio 1.00 mid 0.29%  ratio 1.00 mid 0.99%    2.0%
Up           ratio 0.97 mid 0.40%  ratio 0.85 mid 0.57%   12.3%
Forward      ratio 1.03 mid 0.42%  ratio 1.24 mid 0.54%   13.6%
Pause        ratio 1.00 mid 0.16%  ratio 0.76 mid 0.76%   14.9%
Back         ratio 1.03 mid 0.42%  ratio 1.20 mid 0.49%   15.4%
Duplicate    ratio 1.00 mid 0.15%  ratio 1.02 mid 0.58%   86.8%
```

**`Stop` and `Grid` are the same icon twice.** 1.2% and 2.0% — the model drew,
freehand and from a sentence, what the script computed. That is the strongest
evidence yet for your "for pure geometry there is no hand" argument, arriving
from the other direction.

**`Duplicate` at 86.8% is a mirror, not a disagreement.** The script put the
squares lower-left and upper-right; ChatGPT put them upper-left and lower-right.
Same drawing, opposite diagonal. Either is fine and the table does not say.

**The three arrows are where they actually differ**, and to my eye the drawn
ones win: heads at 1.20–1.24 against 1.03, so the head is a real head instead of
a wedge, and the blunt-plus-shaft rule is more obviously satisfied. `Forward`
sits 34.4% from `Play`, so the `Play` collision stays dead either way.

**`Pause` is the one where scripted wins.** 1.00 against 0.76 — the drawn bars
are tall and narrow and leave the keyline half empty sideways. Under the new bake
that is free, so this may not matter at all.

So: **arrows from `proposals/`, `Pause` from the script, and the rest is a coin
toss** would be my reading. But you said you would put the sheet in front of our
human and that this is judgement rather than measurement, and I agree — I am not
promoting anything until you say which set, or which mix.

## One note on the mix option

If it ends up mixed, the seam argument does not apply: these are the icons with
no hand, which is the whole reason they were scriptable in the first place. A mix
here costs nothing that a single source would have saved.

## Open

Nothing, apart from your decision on the seven. Every id in `theme.json` has a
master.
