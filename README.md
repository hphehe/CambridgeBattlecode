# Cambridge Battlecode 2026: conqueror_of_openai

Our bot for [Cambridge Battlecode](https://2026a.battlecode.co.uk) 2026: five champions that draft, farm camps, bank
gems and fight, written in C++ and compiled to WebAssembly.

**Result: #17 of 831 teams at the international qualifiers.** We did not make the final.

## Why this repo

This was our first real attempt at Battlecode. The bot has flaws, and we did not reach the final, but it grew into a
machine I am proud of: a decision engine built on the game rules, with models trained on thousands of ladder games
filling in what the rules cannot tell us.

I am using this repo to write down the thought process behind it: what each part does, why it is built that way, and
what we learned along the way. It starts with the high-level idea, and a doc for each engine will follow.

## How the bot thinks

- **One currency.** Every champion decides for itself each turn, and every option (a camp, an altar trip, a shopping
  trip, a fight) is priced in banked gems per round, so they can be compared directly.
- **One calculator per question.** How long a camp takes, which camp to work next, when a bag is worth banking and
  whether to fight are each answered in one place, which the rest of the code asks.
- **Coordination without messages.** The draft writes a plan every champion reads, each champion publishes one claim
  byte, and everyone runs the same rules on the same facts.

The full picture is in the [overview](docs/overview.md).

## Repository

| Path | Contents |
| --- | --- |
| [`docs/overview.md`](docs/overview.md) | The high-level idea: constraints, core idea, the turn, the engines |
| [`bots/v294/src/`](bots/v294/src) | Source of v294, our final submission, exactly as submitted |
| [`cambc.toml`](cambc.toml) | Workspace config for the `cambc` CLI |

The submitted zip is attached to the
[v294 release](https://github.com/hphehe/CambridgeBattlecode/releases/tag/v294).

## Build and run

Requires the `cambc` CLI, version 5.0.0.

```bash
cambc init              # fetch the SDK and maps into this workspace
cambc run v294 v294     # play a match against itself
cambc bundle v294       # rebuild the submission zip (byte-identical to the submitted one)
```
