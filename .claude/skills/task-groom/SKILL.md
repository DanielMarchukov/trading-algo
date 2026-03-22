______________________________________________________________________

## name: task-groom description: Review and groom the rich-on-paper backlog

Sprint grooming for the rich-on-paper project backlog. Run all checks, then present findings and suggested fixes.

## Track Definitions

| Track | Name            | Primary tags                                                                                 |
| ----- | --------------- | -------------------------------------------------------------------------------------------- |
| 1     | Quality/Cleanup | quality, cleanup                                                                             |
| 2     | Core Engine     | hot_path, latency, rewrite, risk, reliability, architecture (without pnl/backtest/quant/tui) |
| 3     | Operability     | operability (without tui)                                                                    |
| 4     | Testing         | testing                                                                                      |
| 5     | Backtesting     | backtest                                                                                     |
| 6     | Cost/PnL        | pnl (without tui)                                                                            |
| 7     | Dashboard       | tui                                                                                          |
| 8     | Quant           | quant, strategy (without higher-track tags)                                                  |

## Checks to Run

### 1. Verify urgency config

Run `task show uda.track` and confirm the track UDA exists with values 1-8. Run `task show urgency.uda.track` and
confirm coefficients are set (70,60,50,40,30,20,10,0). If missing, report "Track UDA not configured -- run initial setup
first."

### 2. Find untracked tasks

Run `task project:rop status:pending track: list` to find pending tasks without a track assignment. For each untracked
task, suggest a track based on its tags using the table above. If tags are ambiguous, flag for manual decision.

### 3. Track summary

For each track 1-8, run:

```
task project:rop track:<N> status:pending count
task project:rop track:<N> +READY status:pending count
task project:rop track:<N> +BLOCKED status:pending count
```

Present as a table: | Track | Name | Total | Ready | Blocked | Show completed tracks (0 remaining) as "DONE".

### 4. Ordering verification

Run `task project:rop ready limit:15` and verify:

- All shown tasks are from the lowest-numbered track that has ready tasks
- No task from track N+1 appears above a task from track N
- Flag any ordering violations

### 5. Cross-track dependency audit

Run `task project:rop +BLOCKED status:pending export` and parse JSON. For each blocked task, check if ANY of its
dependencies are in a HIGHER-numbered track (later work). These are cross-track blockers -- the task cannot be reached
until later tracks are partially completed. Flag these as: "Task #X (track N) blocked by #Y (track M, M > N) -- will be
skipped until track M progresses."

### 6. Stale task check

Run `task project:rop status:pending modified.before:4w list` to find tasks not modified in 4+ weeks. Flag as
potentially stale -- may need re-evaluation or removal.

## Output Format

Present results as:

**Backlog Health**

- Total: X tasks (Y ready, Z blocked)
- Track progress bar (visual)

**Issues Found**

- [UNTRACKED] Tasks without track assignment
- [ORDERING] Any urgency ordering violations
- [CROSS-DEP] Cross-track blockers
- [STALE] Tasks not touched in 4+ weeks

**Track Breakdown** (table)

**Suggested Actions**

- Specific `task modify` commands to fix issues

IMPORTANT: Only operate on `project:rop` tasks.
