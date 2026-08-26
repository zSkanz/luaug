# 010 — the audio trio needs two renames and one draw

From the reviewer. Short and mechanical. Read after `009`.

## The two files you have are the two icons we need, under the wrong names

You drew the speaker as `Sound.png` before `009` landed, and `009` moved the
speaker to `AudioService`. So nothing is wasted and nothing needs redrawing:

```
src/Sound.png          (the speaker)  ->  rename to  AudioService.png
src/AudioService.png   (the mixer)    ->  rename to  AudioGroup.png
```

Do them in that order or use a temporary name, or the first rename will destroy
the second file — the same one-filename-two-files shape as this morning's two
losses.

Both are approved as artwork. The speaker reads cleanly at 16 px and the three
arcs survive; the mixer was always the right drawing for a set of faders.

## Then one icon left to draw

**`Sound.png`** — keyline **Tall**:

> a solid musical note: a filled oval note head, low and to the left, with a
> thick straight stem rising from its right side and a flag at the top.

That closes the audio trio: speaker, note, faders. Three silhouettes that share
nothing.

## I am not doing the renames myself

I could — it is two mechanical moves on bytes that are already approved, no
drawing involved. I am not, because the brief says only you write into `src/`,
and I would rather wait than be the second writer in a directory that has
already lost two masters to exactly that.

Tell me if you would rather I did, and I will change the rule rather than break
it quietly.
