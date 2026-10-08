#!/usr/bin/env python3
"""Estimate a rating from games against opponents with known ratings.

    elo_estimate.py results.json

results.json lists, for each opponent, its rating and Bastion's wins, draws and
losses against it:

    [{"name": "Stash 25", "rating": 2744, "wins": 28, "draws": 13, "losses": 19}, ...]

The rating R is the maximum-likelihood solution of
    sum over games (score - expected(R - opponent rating)) = 0
with the logistic Elo model. A 95% interval comes from bootstrapping the games.
"""
import json
import random
import sys


def expected(diff):
    return 1.0 / (1.0 + 10 ** (-diff / 400.0))


def solve(games):
    """games: list of (opponent rating, score). Returns the rating that makes the
    expected total score equal the actual total (bisection; the sum is monotonic)."""
    total = sum(s for _, s in games)
    lo, hi = 0.0, 4500.0
    if total <= 0:
        return lo
    if total >= len(games):
        return hi
    for _ in range(100):
        mid = (lo + hi) / 2
        if sum(expected(mid - r) for r, _ in games) < total:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def main():
    data = json.load(open(sys.argv[1]))
    games = []
    print(f"{'Opponent':12s} {'Rating':>6s} {'Games':>6s} {'W-D-L':>11s} {'Score':>7s} {'Perf':>6s}")
    for opp in data:
        g = [(opp["rating"], 1.0)] * opp["wins"] + [(opp["rating"], 0.5)] * opp["draws"] + \
            [(opp["rating"], 0.0)] * opp["losses"]
        games += g
        n = len(g)
        score = sum(s for _, s in g) / n
        print(f"{opp['name']:12s} {opp['rating']:6d} {n:6d} {opp['wins']:>3d}-{opp['draws']}-{opp['losses']:<4d}"
              f" {100 * score:6.1f}% {solve(g):6.0f}")

    rating = solve(games)
    rng = random.Random(1)
    samples = sorted(solve([rng.choice(games) for _ in games]) for _ in range(2000))
    low, high = samples[50], samples[1949]
    print(f"\nEstimate: {rating:.0f}  (95% interval {low:.0f} to {high:.0f}, {len(games)} games)")
    json.dump({"rating": round(rating), "low": round(low), "high": round(high), "games": len(games)},
              open(sys.argv[1].replace(".json", "-estimate.json"), "w"))


if __name__ == "__main__":
    main()
