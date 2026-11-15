# Cambridge Battlecode 2026: conqueror_of_openai

Our team's bot for [Cambridge Battlecode](https://2026a.battlecode.co.uk) 2026. **v294** is our final submission: a
team of five champions that farm camps, bank gems and fight, all written in C++ and compiled to WebAssembly.

## What's here

| Path | Contents |
| --- | --- |
| [`bots/v294/src/`](bots/v294/src) | The final submission's source, exactly as submitted |
| [`docs/overview.md`](docs/overview.md) | **Start here.** How the bot works: the constraints, the core idea, the turn, the engines |
| `cambc.toml` | Workspace config for the `cambc` CLI (SDK 5.0.0) |

The submitted zip is attached to the [v294 release](../../releases/tag/v294).

## The idea in short

Every champion decides for itself each turn, and every option is priced in one currency: **banked gems per round**.
A camp job, an altar trip, a shopping trip and a fight can all be compared directly. Each question has one calculator
(the job engine, camp engine, bank engine, fight search, shop), and models fitted on ladder games supply the numbers the
rules cannot give, such as the chance of getting caught or what a gold is worth in gems.

The team coordinates without messages: the draft writes a shared plan, each champion publishes one claim byte, and
everyone runs the same rules on the same facts. See the [overview](docs/overview.md) for the full picture.

## Build and run

You need the `cambc` CLI, version 5.0.0.

```bash
cambc init
```

```bash
cambc run v294 v294
```

```bash
cambc bundle v294
```

`cambc init` fetches the SDK and maps into the workspace. `cambc bundle` rebuilds the submission zip; from this tree it
is byte-identical to the submitted one.
