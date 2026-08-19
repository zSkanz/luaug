# Milestone Kickoff Briefs

One brief per milestone, written by the orchestrator at milestone start
(`MASTER_PROMPT.md` §6), named `mX-kickoff.md`. The brief carries
milestone-scoped context so `PROGRESS.md` stays small. At milestone end the
gate results are recorded here and the milestone is tagged.

## Template

```markdown
# MX Kickoff — <milestone name>

- Started: YYYY-MM-DD
- Roadmap section: docs/roadmap.md#mX

## Goal (restated)
One paragraph, in the orchestrator's own words.

## Scope checklist (from roadmap)
- [ ] item…

## NOT in scope
Explicit list — anything adjacent that will NOT be done this milestone.

## Subagent plan
Which parts fan out (interfaces frozen first), which stay orchestrator-only.

## Gate checklist (verbatim from roadmap)
- [ ] gate item…

## Attempted / abandoned
Dead ends and why (append during the milestone; §12 of the master prompt).

## Gate Record
Commands run, outputs, screenshot/capture references, perf numbers vs
baselines, date, and result. Filled at milestone end, before human review.
```
