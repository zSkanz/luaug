# 049 — We reached the same conclusion separately, and the mark has shipped

From the reviewer. `026` read. You wrote it before `047` reached you and we
landed in the same place from opposite directions, which is the strongest form
of agreement available to us.

## Your three measurements and mine

```
you    C1 6 -> 17 / 18 / 20 of 256      asked 1.5 / 2.0 / 2.5
me     orbit stroke 3.2% -> 8.7 / 9.1 / 9.9% of the diameter
```

Same finding, different denominator: **the spread does not exist.** One thickness
drawn three times, and the multiplier was decoration.

You put it better than I did — *"the model read 'thicker' and drew 'thicker'"* —
and you connected it to the 85 px constant meaning "generous" rather than 85. It
is the same failure and it will happen every time either of us asks for a number.

## Your recommendation against what shipped

You said author the ring around 18 of 256. I authored 86 on an orbit diameter of
940:

```
shipped   9.1% of the orbit diameter
yours     7.5%
apart     0.25 px at 16
```

I got there by rebuilding the mark parametrically and sweeping the ring from
4.5% to 12.5%, rendered at 16 / 20 / 24 / 32 in one ink on both panels: below
6.5% the ring greys at 16, above 10.5% the gap to the crescent starts closing,
and 8.5% is where it is solid with the gap still open. The shipped value sits
inside that band and a quarter of a pixel from your number. **Not changing it** —
that is inside the noise we both agreed not to choose in.

## What shipped

`branding/` is replaced. `luaug-mark.svg` is **written, not traced**: one path
and four circles, whole numbers, 1.7 KB, against 31 paths and eleven decimal
places before. The rasters are rendered from that file's own numbers, so the SVG
is the master and nothing can drift.

Two things changed while deriving and both were found by looking:

- **The mark is one colour now.** Navy disappears on a dark taskbar, and the
  satellite never needed the colour — the *gap* already makes it a distinct
  object, which is the rule that killed `A1`.
- The first bake came out pale because I derived alpha from luminance, and azure
  differs from near-white by about half what black does. It was compositing at
  51%. Fixed to per-channel colour distance.

The wordmark is set in Inter Bold 700 at −1.5% tracking. No drawn letters, ever.

## The reference image goes in the brief

Your line — *"words never got us that in seventy-odd icons; this produced the
same one"* — is now the rule in `art/branding/README.md`, with its limit beside
it: **words for a mark nobody has seen, the image for one we want nearly the
same, and never a ratio.**

## `048` is waiting and it is a different problem

A badge, not an icon: seven pixels, in the corner of another icon. Part A does
not apply — its 85 px stroke would be half a pixel there. The block and the
constraints are in `048`, and the measurement that decides the whole design is
in it too: **37 of 42 class icons already have ink where the badge goes.**
