# art/communication/ — a mailbox between the two sides of the icon work

Two agents work on `art/editor-icons/`: one **draws** the icons, one **reviews**
them. They do not share a session, so this directory is how they talk.

| Directory | Who writes | Who reads |
|---|---|---|
| [`for-cowork/`](for-cowork/) | the reviewer | the agent drawing the icons |
| [`for-reviewer/`](for-reviewer/) | the agent drawing the icons | the reviewer |

## The protocol

1. **Check your own inbox before each batch of work.** One `ls` costs nothing
   and a message you did not read is a batch you drew twice.
2. **Read the whole file, act on it, then move it into `_acted/`** beside your
   inbox. A message still sitting in the inbox means nobody has acted on it, so
   an empty inbox is a real signal rather than an ambiguous one.

   This started as "delete it", and the rule is better for a reason neither side
   chose: the drawing agent's device bridge refuses `rm`, so it archived because
   it could not delete. The workaround turned out to beat the rule — the inbox
   still empties, and the record of what was asked and when survives, which
   matters here because half of what both sides learned came from re-reading
   *why* a decision was made.
3. **Files are numbered** — `001-`, `002-` — and read in order. A later message
   may correct an earlier one.
4. **Reply by writing into the other inbox.** Nothing is expected to be answered
   in place; a message is not a thread.

## The one rule that matters

**Anything that must survive being read goes into the brief, not into a
message.** A message is deleted; `art/editor-icons/README.md` and `PROMPT.md`
are not, and they are the source of truth for the style block and every subject
line.

So when a specification turns out to be wrong, the fix is: edit the brief, then
send a message saying *what changed and why*. Never send a correction that lives
only here — the next person to read the brief would not find it, and the same
mistake gets made a second time.

## Message shape

No template, but a message worth reading says these four things:

- **who it is from**, so a reply has somewhere to go
- **what is approved**, explicitly, so nothing is redrawn for no reason
- **what needs redoing and exactly why** — the reason is what stops the same
  failure recurring in the next icon
- **what is deliberately not worth the effort**, which is often the most useful
  part of the message
