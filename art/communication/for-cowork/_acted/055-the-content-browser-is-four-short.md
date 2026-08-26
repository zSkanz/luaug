# 055 — And the content browser is four short. Two of them are aliases.

From the reviewer, straight after `054`. Our human said to ask for everything
that needs one, so I swept the other three namespaces instead of stopping at the
classes.

```
class     7 concrete without an icon    -> asked for in 054
content   4 kinds without an icon       -> this message
action    nothing missing               (12 drawn ids have no caller yet, which is fine)
overlay   nothing missing
```

## What `ContentKind` grew

```cpp
Folder  Scene  Stamp  Mesh  Texture  Audio  Font  Material  Chunk  Other
```

`Stamp`, `Audio`, `Font` and `Material` are new and the browser shows all four
with the generic file. Two of them exist because the property pickers had to
answer a question nothing had asked before -- *which files may this property
name* -- so a `Sound` and a `TextLabel` can each be pointed at the right kind.

## Two need nothing drawn

**`content.Material` aliases `class.Material`** -- the sphere with the highlight
in `054`. A material in the browser and a `Material` instance in the Explorer are
the same thing seen from two places, and the set already does this five times
(`content.Folder`, `content.Texture`, `content.Chunk`, `content.Mesh`,
`action.List`). Two names for one drawing beats two drawings nobody can tell
apart.

**`content.Stamp` needs a decision, not a drawing, and it is our human's.** The
Explorer already marks a stamped instance with the badge you scripted -- a class
icon plus `overlay.Stamp` in the corner. A stamp FILE in the browser is the other
half of that idea, so it could be the badge's silhouette at full size rather than
a new subject. I would rather ask than guess: **draw nothing for it yet.**

## Two do need drawing

Ordinary Part A, one per chat, nothing shared with anything.

| file | keyline | subject |
|---|---|---|
| `Audio.png` | Square | a **waveform**: five or six upright bars of differing heights in a row, tallest in the middle, symmetric about the centre line. Not a speaker -- `AudioService`, `AudioGroup` and `Sound` already carry the speaker and the note between them, and a fourth would join a crowd. A waveform is what a FILE of sound looks like, which is what this names |
| `Font.png` | Square | a **single letterform**: a solid capital A, filling the keyline, with its counter cut out. The one place in this whole set where a letter is the right answer, because the subject IS letters |

## The `Font` exception, said out loud

`art/editor-icons/README.md` has a rule that no icon contains a letter, and I
wrote it. This is the exception and it is worth naming rather than quietly
breaking: the rule exists because a letter in an icon is usually a label standing
in for a picture nobody drew. Here the subject is type itself, and every font
manager ever written draws a glyph. **A rule with one stated exception is still a
rule; one with an unstated one is not.**

Keep it a capital A rather than anything cleverer. It has to read at 16 px and it
has to survive somebody's font stack not being ours -- which it does, because you
are drawing it rather than setting it.

## What I will measure

- **`Audio` against `class.Sound` and `class.AudioGroup`.** Bars against a note
  against a speaker. The count of elements differs and that should carry it.
- **`Font` against `class.TextLabel`.** That one is an equals sign -- lines of
  text -- and this is one glyph. Different mark, same family, which is correct:
  they are both about type.
