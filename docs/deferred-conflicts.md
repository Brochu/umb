# Deferred conflicts

Design notes for UMB's central bet. Nothing here is settled; this document exists so the model can be
argued with before it is written in C.

## 1. The problem

Every mainstream version control system treats a merge conflict as a **failure of the merge operation**.
The operation aborts, the tool puts the working copy in a special half-state, and normal work is blocked
until a human resolves it.

That framing is wrong. A conflict is not a failure — it is a *fact*: two people changed the same thing,
and someone has to decide what it means. The failure is in the **scheduling**. The tool picks the moment
of resolution (right now, at sync time) and it picks the person (whoever synced second). Neither choice
has anything to do with who is best placed to decide, or whether now is a sane time to decide it.

For text this is annoying. For a 300 MB binary asset it is genuinely expensive: resolution may need the
original author, a DCC tool, an hour of work, and possibly a conversation. Forcing that at the moment
someone happens to pull is close to the worst possible scheduling policy.

The name UMB comes from this: an umbrella, shielding people from merge issues arriving at a bad moment.

**The claim to test:** deferral is not a workaround for bad merging. It is the correct default, and
resolution timing should be a project decision rather than a mechanical consequence of when you synced.

## 2. Prior art, honestly

This idea is not new, and pretending otherwise would waste time.

**Jujutsu (jj)** — the closest existing thing. Conflicts are first-class objects recorded *inside*
commits. A merge or rebase that conflicts **succeeds**; the conflict travels in the commit and you
resolve it later. Because jj auto-rebases descendants, the conflict becomes visible immediately but
never blocks. It also means there are no interrupted operations to `--continue`.
*What it does not do:* it is built for text in a Git-backed, small-file world. No binary-first storage,
no locking, no sparse workspaces at studio scale.

**Pijul** — goes deeper theoretically. Patches commute, conflicts are a formal state in the model rather
than an error, and crucially a resolution **applies everywhere, permanently** — solve it once and it
never comes back.
*Worth being precise:* Pijul's contribution is resolution **reuse**; UMB's question is resolution
**timing**. These are orthogonal and compatible. Reuse is a strong candidate to steal later, and it is
the thing that would stop deferred conflicts from being re-litigated on every branch.

**Lore** (Epic, open-sourced June 2026) — solves storage and composition properly, and is worth reading
for both. On conflicts it is deliberately conventional: pushes are a compare-and-swap on branch
pointers, so a diverged branch means fetch, reconcile locally, retry. Binaries surface as "explicit
divergence" where you pick a survivor. Its recommended answer for unmergeable content is locking, which
today "informs rather than blocks."
*Its own open question, ADR-00018:* "View-independent merges, subtree adoption, and out-of-view
conflicts." See §7 — UMB has a natural answer there and Lore does not yet.

**Git `rerere`** — "reuse recorded resolution." A partial, opt-in, local-only gesture at Pijul's
resolve-once property. Evidence the need is real; the fact that it is off by default and forgotten is
evidence that bolting it on after the fact does not work.

**Perforce** — sidesteps merging for binaries with mandatory server-enforced exclusive checkout. Honest
and effective, and the reason gamedev tolerates centralization. The cost: you must predict what you will
edit, and someone holding a lock while on holiday is a real operational problem.

**SVN** — blocks at commit. Same scheduling failure as Git, with less branching to soften it.

**Takeaway:** deferral is proven for text in decentralized small-file worlds. Nobody has done it for
*unmergeable binary content*, and nobody at all has asked **who owns a pending conflict** or **which
branches may carry one**.

## 3. The conflict as data

A conflict is a record in a revision, not a state of the working directory.

```
conflict <id> <path> base=<hash> side=<rev>:<hash>:<author> side=<rev>:<hash>:<author> owner=<name> state=pending
```

- **`id`** — derived from the path and the *sorted* side hashes, so the same divergence discovered twice
  is the same conflict. This is what makes Pijul-style resolution reuse possible later.
- **`base`** — blob hash of the common ancestor, or absent for add/add.
- **`side`** — one entry per contributing revision. **Stored symmetrically.** No side is privileged in
  history; privilege happens only at materialization (§4).
- **`owner`** — who is on the hook (§8).
- **`state`** — `pending` or resolved.

**Lifecycle.** A merge opens a conflict. Subsequent commits **carry it forward verbatim** unless they
resolve it — so the set of pending conflicts is a property of the tip revision and can be read directly,
without walking history. A resolution is an ordinary commit that records the chosen content and drops
the entry.

**Consequence worth noticing:** because conflicts live in revisions rather than in a `.git/MERGE_HEAD`
style side-channel, they survive being pushed, cloned, branched from and handed to someone else. That
handoff is the entire point.

## 4. Mine-first materialization

**One rule, no exceptions:** a conflicted path materializes the side authored by the current user.

Fallback chain when you authored no side (an integrator merging two other people's work, or CI):
`mine → first-parent → newest`. Configurable via `prefer`; default `mine`.

Applies uniformly, text included. The working copy is always *yours*; conflicts live in the repository,
not smeared into your files. One rule is easier to reason about than a rule with a text exception, and
the tree always builds.

**What this costs.** No in-place conflict markers for text. Both sides stay reachable —
`umb conflict show <id>` prints them side by side, `--extract` pulls one out, and `umb resolve --edit`
generates a markered scratch file on demand. Nothing is unreachable; it is just not in your working file
by default.

### The invariant this breaks

Git guarantees *revision determines working tree*. UMB does not: the tree is a projection of
**(revision, identity, prefer)**. Two people at the same revision can legitimately have different files
on disk.

This is the real price of mine-first and it should not be buried. Consequences:

- **CI must not use `prefer=mine`.** It has no identity and would silently pick by fallback. CI should
  either set `prefer=first-parent` for determinism, or — better — run on branches where policy forbids
  pending conflicts, so the question never arises.
- **"Works on my machine" gets a new cause.** Two developers can both build green while the merged truth
  is undefined.

The cost is bounded, though, and that boundedness is what makes the design defensible: **a revision with
no pending conflicts determines its tree uniquely.** Only conflicted revisions are identity-dependent,
and §9's policy gate keeps release branches free of them. Anything you actually ship is deterministic.

### The failure mode to design against

If everyone always sees their own side, a conflict can go **unnoticed indefinitely**. Every tree looks
right. Every build passes. Nobody is ever interrupted — including when they should be.

This is the direct cost of the umbrella, and it means the visibility surfaces are load-bearing rather
than cosmetic:

- `umb status` must say plainly that a file is *your side of an unresolved conflict*, not merged truth.
- `umb conflicts` must show age, so debt is visibly aging.
- `umb log` must flag revisions carrying pending conflicts.
- Policy (§9) must make it impossible to ship one by accident.

If these are weak, mine-first becomes silent data loss with extra steps.

## 5. Audience-scoped variants

The `.env` case: the core team needs real content at a path; outside collaborators need a sanitized
version at that same path, or nothing and must supply their own.

Git can only express this as convention — commit `.env.example`, ignore `.env`, document "copy it and
fill it in." It rots the moment someone adds a variable, and it rots **silently**, because the ignored
file is *invisible* to the tool. Git cannot tell you your `.env` is missing a key added last week; it
does not know the file exists.

**Model.** A path is declared a *variant path*. The repository commits the **public** variant. A local
file differing from it is automatically treated as your **private** variant: never staged, never
committed, no ignore-list discipline required. Private content never enters history.

The declaration also carries a **contract** — for `.env`, the set of required keys.

```
variant .env public=.env.public keys=DB_HOST,DB_PORT,API_KEY
```

**The point.** The path and its contract are **tracked while its content is not**. That is the state Git
lacks: visibility of the contract without visibility of the content. It buys:

- `umb variants` reports each variant path as **satisfied**, **missing**, or **stale**.
- Adding a key to the committed contract makes every collaborator's private variant *stale* — reported,
  by name, before it fails at runtime.
- Nobody has to remember to update an example file, because the contract *is* the tracked artifact.

**Deliberately out of scope:** storing private content in the repository at all, in any form —
encrypted, partitioned or otherwise. Secrets in history are permanent, clone sprawl is real, and
revocation is not a VCS feature. UMB tracks the *shape* of the secret and leaves the bytes to whatever
the team already uses. If restricted-but-shared content becomes a requirement later, Lore's partition
model is the thing to copy.

## 6. Lanes

Variants and conflicts meet here, and pleasantly.

A variant path defines **lanes**: different audiences legitimately hold different content at the same
path. Divergence *between* lanes is intentional and is **never a conflict**. Conflicts exist only
*within* a lane.

This falls out of the model rather than needing a special case:

- Private variants are never committed, so they can never conflict. Two people's real `.env` files are
  simply unrelated objects.
- The **public** variant is ordinary tracked content and conflicts normally.
- A change to the public contract does not conflict with anyone's private variant — it marks them
  **stale**, which is a reportable state with an obvious remedy, not a merge to resolve.

So the `.env` class of problem stops being a conflict problem at all. That is a good sign for the model:
the two features were designed separately and did not need glue.

## 7. Out-of-view conflicts

Lore lists ADR-00018 — "View-independent merges, subtree adoption, and out-of-view conflicts" — as an
open problem: what happens when a merge conflicts on a path outside your sparse view?

Under resolve-now, this is genuinely hard. You are required to resolve something you do not have on
disk, may not have permission to fetch, and probably know nothing about. The options are all bad:
force-hydrate content the user deliberately excluded, block the merge, or silently pick a side.

Under deferral it is not a special case at all. **Record the conflict, assign an owner, continue.** The
conflict is data in the revision; materialization is a separate concern that simply does not happen for
paths outside your view. Someone whose view *does* include the path resolves it later.

The fact that a hard problem for everyone else becomes a non-problem here is the strongest available
argument that deferral is the right primitive rather than a convenience feature. Worth stating clearly
in the README.

## 8. Ownership and routing

An unresolved conflict with no owner is just abandoned work. Candidate default heuristics, to be tested:

- **Author of the side that did not materialize.** Appealing: the person whose work is currently invisible
  has the strongest interest in it not being lost. Probably the right default.
- **Whoever performed the merge.** Simple, but recreates the status quo — punishing whoever synced.
- **Last person to touch the path before the split.** Approximates domain ownership.
- **A declared map**, CODEOWNERS-style. Best for asset directories with clear ownership; needs upkeep.

Reassignment must be cheap (`umb conflict assign <id> <who>`) — handoff is a feature, not an exception.

Open: should ownership be per-conflict or per-path-pattern? Per-path is less noisy but cannot express
"you made this mess."

## 9. Policy and bounding

Deferral without limits is a landfill. Policy is what makes it an umbrella instead.

```
deny-pending-conflicts: main, release/*
```

A merge into a denied branch **refuses** while conflicts are pending. Feature branches carry debt
freely; protected branches never do. The gate is the mechanism that keeps §4's determinism argument
honest: anything reachable from a protected branch has a unique working tree.

Further ideas, not yet designed:

- **Age limits** — warn, then refuse, once a conflict passes N days.
- **Count limits** — refuse to open new conflicts on a path that already carries one, forcing
  conflict-on-conflict to be dealt with rather than stacked.
- **Debt in `status`** — surface repository-wide pending count and oldest age, always.

## 10. Relationship to locking

Locking and deferral are **complementary, not alternatives**, and conflating them is why the existing
tools land where they do.

- **Locking prevents divergence.** Best when you can predict what you will edit and coordination is
  cheap. Perforce enforces it server-side; Lore's is currently advisory and admits it does not scale.
- **Deferral absorbs divergence.** Best when prevention failed or was impossible — offline work, an
  out-of-view path, or two people who both had good reason to edit.

A complete system probably wants both: locks to make binary conflicts *rare*, deferral so the ones that
happen anyway do not detonate at the worst moment. UMB is exploring only the second, on the assumption
that the first is well understood and mostly an operational problem.

## 11. Open questions, and what the prototype answered

The prototype in `src/` implements all of the above. Status of each question after running it:

**1. Does carrying a pending conflict keep history coherent, or does it metastasize? — Holding up.**
A conflict survived ordinary commits on top of it, travelled onto a branch created from a conflicted
revision, and came back through a merge of that branch without duplicating. The content-derived id is
what does the work: re-merging produced the *same* id, so the conflict was recognised rather than
re-opened. Untested at any real scale or branch count.

**2. Conflict-on-conflict? — Found a genuine defect here, now fixed.**
The first implementation skipped any path that already carried a conflict. A second divergence on that
path was therefore *silently discarded* — the exact failure mode §4 warns about, produced by the very
design meant to prevent it. It now becomes an **additional side** (`side 3`, `side 4`, …) with the id
recomputed across all sides.

Two consequences worth chewing on:
- Adding a side **changes the conflict's id**, since the id is derived from the set of sides. A
  resolution recorded against the old id would no longer match it. That is the first real tension with
  Pijul-style resolution reuse (§2) and needs a proper answer before reuse is attempted.
- Selecting a side by author name goes ambiguous once one person holds several sides. That is now a
  hard error telling you to use the index, rather than a silent pick of the first match. An alternative
  worth considering is having a later side by the same author *supersede* the earlier one, which keeps
  the record small at the cost of hiding intermediate work.

**3. Does mine-first cause real silent loss, and is the reporting enough? — Mechanism verified, human
question open.** `status`, `conflicts` and `log` all flag pending conflicts, and `status` states
explicitly that the file on disk is your side rather than merged truth. Whether that is *loud enough*
when someone is heads-down for a week cannot be settled by a test. Still the most likely way this
design fails.

**4. Branch from, and merge out of, a conflicted revision? — Yes.** Verified in both directions, with
no duplication.

**5. Uniform mine-first for text? — Works, but the markered output is weaker than Git's.**
`resolve --edit` emits per-side blocks rather than interleaved diff3-style hunks, because sides are
whole blobs and nothing computes line-level hunks. Fine for short files, poor for large ones. If text
ergonomics end up mattering, this is where the cost of the uniform rule actually shows up.

**6. Out-of-view conflicts? — Untested.** The prototype has no sparse views, so §7 remains an argument
rather than a demonstration.

**7. Default owner heuristic? — Implemented, socially untested.** The side you did *not* author becomes
the owner. Reads sensibly in output; whether it feels fair in a real team is unknown.

**8. Does the variant contract earn its complexity? — Yes for `.env`, unproven beyond it.** Growing the
committed contract by one key correctly reported the local file as `stale` and named the missing key,
which is the thing `.gitignore` structurally cannot do. Whether the idea generalises past `KEY=value`
files is untested.

### Known limitations of the prototype

- Whole-file blobs, FNV-1a addressing, no chunking, no packing, no network.
- Fixed-capacity revisions (see `umb.h`); merges hold four revisions on the stack.
- `merge` overwrites the working copy and assumes a clean tree.
- Editing a conflicted file does **not** resolve it; `umb resolve` is required. Deliberate, but it
  means an edit to a conflicted path is quietly ignored at commit time.
- No ignore list. Everything outside `.repo` and declared variant paths is tracked.
