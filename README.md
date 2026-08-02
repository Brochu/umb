# umb

An experiment in version control.

This is not a product, and it is not trying to replace anything yet. It is a place to
prototype ideas about how a version control system *could* work if it were designed
today, learning from what Git, Perforce and SVN each got right and wrong.

## Why bother

Git won. It is the tool almost every software project reaches for, and for good reason:
cheap branching, offline work, and a data model simple enough that an entire ecosystem of
wrappers, forges and workflows grew on top of it.

But a lot of those wrappers exist because the underlying tool fights the problem:

- **Merge conflicts have to be resolved at the earliest possible moment** to keep history
  in sync, even when the conflict is not urgent and the person hitting it is not the
  person who should be resolving it.
- **File locking doesn't fit.** Being decentralized means nobody can authoritatively say
  "I have this file, don't touch it" — which is exactly what you need for assets that
  cannot be merged.
- **Large binaries inflate the repository forever.** Every clone pays for every version of
  every file that was ever committed.
- **git-lfs feels bolted on** rather than part of the tool, with its own failure modes and
  its own server story.
- **Submodules are a good idea with frustrating ergonomics** — composing repositories is a
  real need, and the current answer makes people avoid it.

The centralized world answered some of this. Game development largely went the other way,
to Perforce or (historically) SVN, and got real file locking, sane handling of big binary
assets, and partial checkouts of enormous trees. But it traded away cheap local branching
and offline work, and the merge story did not get better — it mostly got avoided by
locking instead.

## What Lore already fixed

Epic open-sourced **[Lore](https://epicgames.github.io/lore/)** in June 2026 — MIT, Rust, previously
Unreal Revision Control. Before building anything it was worth checking what was left to build, and
the honest answer is: less than expected.

- **Repository size — solved.** Content-addressed storage, FastCDC chunking, fragment-level dedup
  across history *and* branches, sparse views, lazy hydration. A measured ~200 KiB per change to a
  200 MiB binary, against ~200 MiB for Git, git-lfs *and* Perforce. Native, not bolted on.
- **Composition — solved conceptually.** **Links** (versioned, committed, travel with a clone) versus
  **layers** (local overlay, never committed), both able to mount a *subdirectory*. The useful insight
  is the question it asks: does this piece of composition belong in the revision, or on the
  workstation? Git conflates the two and gives you the worst of both.
- **Ignore lists — improved.** Split in two: `.lore/view` (inbound — what lands on my disk) and
  `.loreignore` (outbound — what may be committed). That is the right cut.
- **Conflicts — untouched.** Pushes are a compare-and-swap; a diverged branch means fetch, reconcile,
  retry. Binaries surface as "explicit divergence" where you pick a survivor. The recommended answer
  for unmergeable content is locking, which today informs rather than blocks.

So UMB stopped trying to be a whole version control system and narrowed to the part nobody has done.

## What UMB is actually testing

> A merge that hits a conflict should **succeed**, recording the conflict as durable data in history,
> and materialize **your own side** in the working tree so your flow is never interrupted — bounded by
> branch policy so the resulting debt cannot hide.

Deferral itself is not novel: **Jujutsu** records conflicts as first-class objects inside commits, and
**Pijul** goes further with patch commutation where a resolution applies everywhere, permanently. Both
are for text, in decentralized small-file worlds. The unoccupied ground is deferral for *unmergeable
binary assets*, where resolution costs hours, a DCC tool and the original author — which is exactly
why forcing it at push time is the worst possible scheduling policy.

Two things fall out of that, which is a good sign the idea has legs:

- **Out-of-view conflicts.** Lore lists this as an open problem (ADR-00018): what happens when a merge
  conflicts on a path outside your sparse view? Under resolve-now it is genuinely hard. Under deferral
  it is not a special case — record it, assign it, continue. You cannot resolve what you do not have.
- **Audience-scoped variants.** A path can hold different content for different people — the real
  `.env` versus a sanitized one. Divergence *between* audiences is intentional and never a conflict,
  so the `.env` problem stops being a conflict problem at all. Private content is never committed;
  what gets tracked is the *contract*, so the tool can tell you your `.env` is missing a key someone
  added last week. Git cannot, because the file is invisible to it.

Design notes and findings live in [docs/deferred-conflicts.md](docs/deferred-conflicts.md).

Some of this will turn out to be wrong. Finding out which parts is the point — the prototype has
already killed one of its own assumptions, which is written up in the findings section.

## Status

Working prototype, deliberately naive underneath: whole-file blobs, no chunking, no network, no
locking. Storage is not the experiment. It can `init`, `commit`, `branch`, `switch`, `merge`,
`conflicts`, `conflict show`, `resolve`, `variants`, `status` and `log`.

`UMB_USER` sets your identity, which is what decides whose side of a conflict lands in your working
copy — set it to two different names to see the whole idea work in one directory.

## Building

Requires MSVC (Visual Studio with the "Desktop development with C++" workload). The build
script locates the toolchain itself.

```bat
build.bat
```

Objects land in `obj\`, the executable in `bin\`. To run it:

```bat
run.bat init myproject
```

## Seeing the point in 60 seconds

```bat
set UMB_USER=alex
umb init demo && cd demo
```

Commit a binary asset, branch off, edit it as someone else, edit it differently as yourself, then
merge. The merge **succeeds**, your bytes stay on disk, and the conflict is recorded rather than
thrown at you:

```bat
umb merge art
umb status
umb commit -m "unrelated work that is not blocked"
umb conflicts
umb resolve <id> --take art
```
