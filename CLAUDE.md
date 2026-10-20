# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Everything about the project itself — the build and release workflow, architecture, conventions,
and load-bearing pitfalls — is in `docs/Development.md`, imported here so it is always loaded:

@docs/Development.md

The rest of this file is how to work in this repository as an assistant.

## Top priority rule

### File editing permissions

You may automatically view and edit any file **inside this repository's folder**. You must **not**
view or edit any file **outside it** — ask the user to grant permission first if you need to.

### Ask before editing

When you find unclear instructions, or find multiple ways to implement requested features, ask user first instead of editing arbitrarily.

### Karpathy-Inspired Claude Code Guidelines

Use the skill of andrej-karpathy-skills for all planning and editing tasks. This means you should:

**1. Think Before Coding: Don't assume. Don't hide confusion. Surface tradeoffs.**
* State your assumptions explicitly. If uncertain, ask.
* If multiple interpretations exist, present them - don't pick silently.
* If a simpler approach exists, say so. Push back when warranted.
* If something is unclear, stop. Name what's confusing. Ask.

**2. Simplicity First: Minimum code that solves the problem. Nothing speculative.**
* No features beyond what was asked.
* No abstractions for single-use code.
* No "flexibility" or "configurability" that wasn't requested.
* No error handling for impossible scenarios.
* If you write 200 lines and it could be 50, rewrite it.
* Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

**3. Surgical Changes: Touch only what you must. Clean up only your own mess.**
* When editing existing code: Don't "improve" adjacent code, comments, or formatting; don't refactor things that aren't broken; and match existing style, even if you'd do it differently.
* If you notice unrelated dead code, mention it - don't delete it.
* When your changes create orphans: Remove imports/variables/functions that YOUR changes made unused, but don't remove pre-existing dead code unless asked.
* The test: Every changed line should trace directly to the user's request.

**4. Goal-Driven Execution: Define success criteria. Loop until verified.**
* Transform tasks into verifiable goals (e.g., "Add validation" → "Write tests for invalid inputs, then make them pass").
* For multi-step tasks, state a brief plan such as: 1. [Step] → verify: [check], 2. [Step] → verify: [check].

## Working rules

- **Always launch `build\SnapCap.exe` after finishing a change** so the user can verify it live:
  kill any running instance first, then build, then run.
- **Keep the docs in lockstep** as `docs/Development.md` → Conventions describes: `claude-plan/*.md`
  status and `Architecture-FileMap.md` rows after a feature, `docs/Usage.md` when usage changes,
  and the `ui/PreferencesDialog` row for every Preferences setting and its consumer.

### Visual testing — the user does it, not you

**The user performs all visual/GUI verification themselves.** Do **not** write automated
visual tests or build tooling to self-verify appearance: no screenshot / `grab()`-to-PNG
harnesses, no synthetic mouse/key event injection, no new `tools/*_demo` targets just to
check how something looks. (Real unit tests for pure `core/` logic are still expected — this
rule is only about *visual* checks.)

Instead, after a GUI change: kill the running instance, rebuild, launch `build\SnapCap.exe`,
then **tell the user how to run the visual test** — the exact click-path to reach the changed
UI and what to look for. Keep it specific to what changed, e.g.:

> 1. Open the tray menu (the `^` overflow on Windows 11) → **Preferences**.
> 2. Go to the **Control** tab → click **Key Bindings**.
> 3. Hover a row and confirm a full-row hollow outline appears with no cell fill.

### i18n: English only — the user handles translations

**The user maintains all non-English locales themselves.** When you add or change UI strings,
only edit `lang/en.yaml` (the authoritative locale), then run `python tools/i18n_check.py`.
**Do not** touch the other `lang/*.yaml` files (zh_cn, zh_tw, de_de, es_mx, fr_fr, ja_jp, ko,
nl_nl, pt_br, pt_pt, ru). It is expected and fine that the check then reports those locales as
missing/orphaned keys — the check's printed key list is exactly the to-do the user will use.
Leave the translation work to the user unless they ask otherwise.

## Verify, Don't Trust

When producing an analysis or summarization of something gleaned from a resource (web page, MCP call, user-provided document), do not trust a memory or retained summary of that resource. Always retrieve the resource afresh and compare it to the summary or analysis you are preparing. When comparing, do so in an adversarial way: you are fact-checking work that you suspect at the start contains errors and hallucinations.
