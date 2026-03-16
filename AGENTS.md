# AGENTS.md — BrokerSimulator (Current Operating Instructions)

This file defines the execution contract for AI agents working in BrokerSimulator.
It reflects current behavior in this repo and its role in ElanTradePro integration.

Last updated: 2026-03-14

---

## 1) Hard Rules (Always Follow)

1. If simulator restart is needed, run it yourself:
   - `./start.sh restart`
   - Do not ask the user to run restart commands.
2. After each user request handled in this repo, append one line to:
   - `codex_history.log`
   - Include timestamp + request summary + action taken.
3. Do not silently revert user changes.
4. If unexpected unrelated changes appear, pause and ask.
5. Simulator behavior must remain live-like for replay:
   - no future-data leakage beyond current simulated time.

---

## 2) Repo Role and Scope

BrokerSimulator is the live-like market/trading simulator for ElanTradePro.
It exposes broker-compatible APIs and streams for:

- Control service: `:8000`
- Alpaca simulation: `:8100`
- Polygon simulation: `:8200`
- Finnhub simulation: `:8300`
- WebSocket gateway: `:8400`

Primary directories:
- `src/` C++ simulator code
- `manager-ui/` simulator UI
- `integration-test/` tests
- `requirements/` design/reference docs

---

## 3) Live-Like Time and Data Contract

1. Session owns replay time (`current_time`) and speed.
2. Data APIs/streams must not expose records after session `current_time`.
3. Historical API requests are allowed for prior data, but must be clamped for live-like endpoints where applicable.
4. Session time window (`start_time`, `end_time`) must gate replay progression and stream emission.
5. Dynamic symbol subscriptions must attach to current session time state (no backfill leak by default).

---

## 4) Order Fill Semantics Contract

1. Accept and honor strategy `decision_time` for order placement where supplied.
2. Fill timestamps should anchor to decision/simulated timeline for consistency in high-speed replay.
3. Avoid introducing lookahead or post-hoc corrections that use future ticks.
4. Accounting truth precedence for debugging:
   - orders/trades/events in API payloads > chart marker placement.

---

## 5) API Compatibility Contract

Simulator endpoints should mimic real provider API shapes and constraints.

- Alpaca simulation: account/orders/positions lifecycle compatibility.
- Polygon simulation:
  - supports feed selection (`polygon_news`, `benzinga_news`) where implemented.
  - ticker and all-ticker news behavior must match capability model.
- Finnhub simulation:
  - symbol-scoped news and other supported market-info endpoints.

Do not add simulator-only payload deviations unless explicitly versioned and documented.

---

## 6) Streaming Contract

1. WebSocket streams are session-scoped via `session_id`.
2. Stream events must align with session replay time.
3. Do not duplicate emit paths that create racey duplicate events.
4. Keep stream payload ordering deterministic when timestamps are equal (stable ordering).

---

## 7) Data Source Contract

- Primary ClickHouse schema: `market_data`.
- Support tables include Polygon and Finnhub synced datasets used by controllers.
- Queries must enforce replay-time cutoffs before returning records.
- For LowCardinality columns in ClickHouse, use safe casts where needed in SQL.

---

## 8) Verification and Testing Discipline

For non-trivial changes, validate with evidence before completion.

Minimum checks:
1. Build simulator successfully.
2. Start simulator and verify health endpoints respond.
3. Run targeted API flow reproducing the issue.
4. Validate both REST and WS behavior for the changed path.
5. For timing consistency fixes, run repeated replay tests (>=3 runs, preferably 5) and compare fill fingerprints:
   - symbol, side, qty, fill price, fill time.

When debugging against ElanTradePro:
- Validate end-to-end behavior from ElanTradePro API/UI, not simulator in isolation only.

---

## 9) Operations

Start/restart:
```bash
./start.sh restart
```

Stop:
```bash
./stop.sh
```

UI helpers:
```bash
./start_ui.sh
./stop_ui.sh
```

Use repo scripts in `scripts/` rather than ad-hoc process spawning.

---

## 10) Logs and Debugging

Check these first for regressions:
- simulator runtime logs under `~/logs/` and repo log outputs.
- WS/controller logs for session_id/time filtering behavior.

For timing mismatches, capture:
1. order create payload (including decision_time)
2. fill response timestamp/price
3. emitted WS event payload timestamp
4. corresponding query-time filters and session current_time

---

## 11) Cross-Repo Coordination (Mandatory)

BrokerSimulator and ElanTradePro must remain behaviorally aligned.

If changing contracts used by ElanTradePro (payload fields, timing semantics, feed behavior):
1. update both repos as needed,
2. validate end-to-end,
3. report both branches/commits.

No unilateral contract drift.

---

## 12) Practical Agent Checklist (Per Request)

1. Confirm branch + git status.
2. Reproduce first.
3. Implement minimal correct fix.
4. Verify with targeted repeated tests where timing is involved.
5. Summarize evidence.
6. Append one request/action trace to `codex_history.log`.
