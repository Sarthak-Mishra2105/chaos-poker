# Chaos Poker Bot — Submission

**Name:** Sarthak Mishra

---

## Building

Requires a C++17 compiler (e.g. `g++`). Build the engine and all bots with:

```bash
make
```

---

## Launch Command

The bot communicates over `stdin`/`stdout`. Exact launch command:

```bash
./bots/my_bot
```

---

## Running a Match

```bash
./chaos_poker --history 1000 5 15 25 50 ./bots/my_bot ./bots/example_bot ./bots/random_bot
```

Adjust the number of bots (2–6 players supported) and the `--history` flag (hand-history line count) as needed.

---

## Strategy

The bot uses **Monte Carlo equity estimation** as its core decision engine, layered with opponent-adaptive logic and stack-depth tiers.

### Equity Estimation

For every action and vote decision the bot runs a time-budgeted Monte Carlo simulation (up to 1 500 random boards, capped at ~5.5 ms) to estimate win probability at showdown. Each trial completes the community cards and randomly assigns hole cards to all remaining opponents.

### Opponent Modelling

The bot tracks per-seat statistics across hands:

| Stat | Default |
|------|---------|
| Fold rate | 0.30 |
| VPIP | 0.50 |
| Aggression factor | 1.0 |

These drive two key adjustments:

- **Range discounting**: opponents who reach later streets have stronger-than-random ranges, so raw MC equity is reduced by a per-opponent multiplier (0.01 / 0.015 / 0.02 on flop / turn / river). Against hyper-aggressive opponents (`aggression > 2.5`) the discount is scaled down via `agg_factor = max(0, 1 − (agg − 2.5) × 0.2)`, since their wide ranges partially offset the selection effect.
- **Implied-odds calls**: when facing a bet with `SPR > 6` and `equity > 0.25`, the bot will call even slightly below pot odds (`equity > pot_odds − 0.08`) to account for the extra value of catching a strong hand on later streets.

### Action Tiers (Stack-Depth Aware)

| Stack | Strategy |
|-------|----------|
| < 10 BB | Push/fold: commit with equity > 0.45 open, equity > 0.38 facing a bet |
| 10–30 BB | Raise (0.55× pot) with equity > 0.68; positional raise (0.35× pot) with equity > 0.50 in position; call with equity > pot\_odds + 0.02 |
| > 30 BB | Full value-bet / semi-bluff / pot-control logic |

In the deep-stack path:

- **Strong value bet** (equity > 0.72): raises 0.70× pot against aggressive opponents (`opp_agg > 1.5`), 0.55× pot otherwise.
- **Medium value bet** (equity > 0.55, SPR > 3): raises 0.40× pot in position, 0.35× pot out of position.
- **Semi-bluffs** in position: when equity > 0.30, avg fold rate > 0.35, and a calculated EV comparison (`bluff_ev > check_ev`) passes, the bot bets 0.35× pot to steal.
- **Risk adjustment**: the bot slightly tightens committing thresholds when it is the chip leader, scaled by `agg_factor` (so the adjustment shrinks against aggressive opponents).

### Vote Decision

For each community card vote the bot computes MC equity both with and without the current card, then wagers proportional to `|Δequity| × expected_pot × 0.6`. Maximum wager scales with conviction:

| Equity delta | Max wager |
|---|---|
| > 0.15 | chips / 4 |
| > 0.08 | chips / 6 |
| otherwise | chips / 8 |

Spending more on high-conviction votes lets the bot outweigh opponents on boards where it holds a clear advantage.

### Swap Decision

- **Pre-flop**: uses the Chen hand-strength formula — keep any hand scoring ≥ 7 (or suited ≥ 3), or if the swap cost exceeds chips/8.
- **Post-flop**: only swaps with no pair and no flush draw, only when cost ≤ chips/6; confirmed by a mini MC comparison.

### Tradeoffs

- **Speed vs. accuracy**: the 10 ms time limit constrains simulation depth; the bot prioritises faster, shallower MC over deeper but slower analysis.
- **Generalisation**: opponent model defaults are conservative for the first few hands to avoid overfitting early noise.
- **Multiway vs. heads-up**: semi-bluff and range-discount parameters are tuned to work across both formats.

---

## Original Task Description

> Build a bot that plays Chaos Poker.
>
> Implement a bot that communicates with the engine over `stdin` / `stdout` using the protocol in [RULES.md](RULES.md).
>
> The code in this repository is provided as a **test harness** for local development.

If the harness implementation or the message-flow example appear to deviate from [RULES.md](RULES.md), the rules in [RULES.md](RULES.md) should be treated as authoritative.

You may use any language, as long as your bot can be launched from the command line and responds within the time limit.

## Evaluation

Your bot should aim to maximise **match wins**.

We will run the submitted bots through repeated offline evaluations and rank them based on their performance in those tournaments.

We may also assess submissions qualitatively, including the overall quality of the approach and implementation.

## Running the Test Harness

Build the engine and sample bots:

```bash
make
```

Run a sample 3-player match with hand history output:

```bash
./chaos_poker --history 1000 5 15 25 50 ./bots/example_bot ./bots/random_bot ./bots/random_bot
```

## Submission

Create a private GitHub repository for your work.

Your repository should contain:

- your bot source code
- a top-level `README.md`

Submission deadlines:

- Invite `tk-machine-user` with read access by Sunday, April 12, 2026, 23:59 IST so any access or permissions issues can be resolved before the final deadline.
- Follow up by email with your repository URL and your GitHub username after you have sent the GitHub invitation.
- Do not make any further changes to your repository after Monday, April 13, 2026, 23:59 IST.

In your repository `README`, include:

- your name
- the exact command we should use to launch your bot
- how to build your bot
- how to run your bot
- a short note describing your strategy and any tradeoffs

## Questions and Clarifications

If you have questions about the exercise, please raise them as GitHub issues in this repository.

Because this exercise runs over the weekend, you should not depend on receiving a timely answer to any question and should be prepared to make reasonable assumptions and work with uncertainty.

Any official clarifications to the rules will be summarised in this README so you do not need to inspect the commit history to find them.

## Clarifications

- 2026-04-10 13:41 IST: The official response timeout is now 10 ms per decision, and the provided test harness has been updated accordingly.
- 2026-04-12 08:00 IST: We have made fixes to the test harness, and may continue to do so as issues are identified. We may not add a clarification entry for every future test harness fix. Where test harness behaviour deviates from [RULES.md](RULES.md), the rules in [RULES.md](RULES.md) take precedence.
- 2026-04-12 08:43 IST: Deck exhaustion in the test harness is now handled without crashing. Discarded cards are not reused; unavailable swaps and redraws are skipped, a swap round may be skipped if there are not enough cards to fulfil every eligible swap while preserving a valid showdown path, and if a later street cannot be dealt the hand proceeds directly to showdown using the cards already in play.
- 2026-04-12 08:52 IST: The test harness now also rejects impossible match sizes above 26 players, marks players all-in when they spend their last chip during swap or vote phases, and correctly applies the all-error split rule in vote phases.
