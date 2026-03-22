______________________________________________________________________

## name: task-sync description: Sync Taskwarrior tasks to GitHub Issues and Project #3

Sync `project:rop` Taskwarrior tasks with GitHub Issues. This is a single procedural flow — do NOT use sub-agents.
Execute all steps sequentially in one conversation turn.

## Step 1: Export Taskwarrior tasks

Run `task project:rop export` and parse the JSON output. Keep all tasks regardless of status (pending, completed,
deleted). For each task, extract: `id`, `uuid`, `status`, `description`, `estimate`, `tags`, `annotations`, `priority`.

## Step 2: List GitHub issues

Run:

```
gh issue list --state all --limit 200 \
  --json number,title,body,state,labels,milestone
```

Parse the JSON output.

## Step 3: Match tasks to issues (three-tier)

For every TW task, attempt to find a matching GH issue using these methods in order. Stop at the first match.

**Tier 1 — UUID match**: Search each issue body for the string `Taskwarrior UUID: <uuid>`. This is the format used by
issues created by this skill.

**Tier 2 — Legacy ID match**: Search each issue body for the pattern `Taskwarrior: #<id>`. Validate by checking that the
issue title is similar to the TW description (prefix overlap). This guards against ID reuse after task
deletion/completion.

**Tier 3 — Exact title match**: Issue title exactly equals TW task description. Use only as a fallback.

**Special cases**:

- Skip issue #56 (tracking issue, has no TW reference)
- TW tasks with no matching issue and status `completed` are ignored (already done before GH sync existed)

Build two maps:

- `matched`: TW uuid -> GH issue number
- `unmatched_pending`: TW tasks with status `pending` and no matching GH issue

## Step 4: Create issues for unmatched pending tasks

For each task in `unmatched_pending`:

**Title**: The TW task description verbatim.

**Body**: Compose from annotations and metadata:

- First line: the task description (as a summary sentence)
- If the task has annotations prefixed with `AC:`, add an `## Acceptance criteria` section with each AC as a bullet
- If the task has non-AC annotations, add them as context lines
- Footer line: `Estimate: <estimate> | Taskwarrior UUID: <uuid>`

**Labels**: Map TW tags to GH labels using this table (skip tags not in the table):

| TW tags                       | GH label      |
| ----------------------------- | ------------- |
| testing, quality, security    | testing       |
| architecture                  | architecture  |
| hot_path, perf, latency       | performance   |
| operability, reliability      | operability   |
| trading_logic, strategy, risk | trading-logic |

A task may match multiple labels if it has tags from multiple rows.

**Milestone**: Assign by first-match rule on TW tags:

| TW tags                     | Milestone                  |
| --------------------------- | -------------------------- |
| testing, quality, security  | Test Hardening             |
| hot_path, perf, latency     | Hot-Path & Standards       |
| (any other known tag above) | Architecture & Integration |

If no tags match any row, omit the milestone.

**Create command**:

```
gh issue create \
  --title "<title>" \
  --body "<body>" \
  --label "<label1>" --label "<label2>" \
  --milestone "<milestone>" \
  --assignee @me
```

**Add to Project #3**: After creating each issue, add it to the project and set its fields:

```bash
# Get the issue node ID
ISSUE_ID=$(gh issue view <number> --json id --jq .id)

# Add to project
ITEM_ID=$(gh api graphql -f query='
  mutation {
    addProjectV2ItemById(input: {
      projectId: "PVT_kwHOAQlLl84BQgmq"
      contentId: "'"$ISSUE_ID"'"
    }) { item { id } }
  }' --jq '.data.addProjectV2ItemById.item.id')

# Set Status = Todo
gh api graphql -f query='
  mutation {
    updateProjectV2ItemFieldValue(input: {
      projectId: "PVT_kwHOAQlLl84BQgmq"
      itemId: "'"$ITEM_ID"'"
      fieldId: "PVTSSF_lAHOAQlLl84BQgmqzg-m2_c"
      value: { singleSelectOptionId: "f75ad846" }
    }) { projectV2Item { id } }
  }'
```

Set Estimate field using these option IDs:

| Estimate | Field ID                       | Option ID |
| -------- | ------------------------------ | --------- |
| XS       | PVTSSF_lAHOAQlLl84BQgmqzg-m3mw | b76ef9a8  |
| S        | PVTSSF_lAHOAQlLl84BQgmqzg-m3mw | 9f1a7485  |
| M        | PVTSSF_lAHOAQlLl84BQgmqzg-m3mw | d5290038  |
| L        | PVTSSF_lAHOAQlLl84BQgmqzg-m3mw | d9c54d12  |
| XL       | PVTSSF_lAHOAQlLl84BQgmqzg-m3mw | 58ee40fe  |

Project field reference (Status):

| Status      | Field ID                       | Option ID |
| ----------- | ------------------------------ | --------- |
| Todo        | PVTSSF_lAHOAQlLl84BQgmqzg-m2_c | f75ad846  |
| In Progress | PVTSSF_lAHOAQlLl84BQgmqzg-m2_c | 47fc9ee4  |
| Done        | PVTSSF_lAHOAQlLl84BQgmqzg-m2_c | 98236657  |

## Step 5: Close issues for completed TW tasks

For each matched pair where the TW task status is `completed` but the GH issue state is `OPEN`:

```bash
gh issue close <number> --reason completed
```

Then set the project Status to Done:

```bash
# Get item ID from project
ITEM_ID=$(gh api graphql -f query='
  query {
    user(login: "DanielMarchukov") {
      projectV2(number: 3) {
        items(first: 100) {
          nodes {
            id
            content { ... on Issue { number } }
          }
        }
      }
    }
  }' --jq '.data.user.projectV2.items.nodes[]
    | select(.content.number == <NUMBER>) | .id')

# Set Status = Done
gh api graphql -f query='
  mutation {
    updateProjectV2ItemFieldValue(input: {
      projectId: "PVT_kwHOAQlLl84BQgmq"
      itemId: "'"$ITEM_ID"'"
      fieldId: "PVTSSF_lAHOAQlLl84BQgmqzg-m2_c"
      value: { singleSelectOptionId: "98236657" }
    }) { projectV2Item { id } }
  }'
```

## Step 6: Log label and milestone drift

For each matched pair (TW pending, GH open), compute the expected labels and milestone from the tag mapping tables
above. Compare with the actual GH issue labels and milestone.

If they differ, log a warning line but do NOT modify the issue:

```
WARNING: Issue #<n> labels mismatch:
  expected=[perf] actual=[architecture]
WARNING: Issue #<n> milestone mismatch:
  expected="Hot-Path & Standards" actual="Architecture & Integration"
```

## Step 7: Print summary

Print a concise summary table:

```
Task sync complete:
  Matched (in sync): <N>
  Created:           <N>
  Closed:            <N>
  Warnings:          <N>
  Skipped (no GH):   <N>  (completed before sync)
```

List any created issue numbers and any closed issue numbers.

## Important constraints

- **Idempotent**: Running twice creates no duplicates. UUID matching catches already-synced issues.
- **Read-only on Taskwarrior**: Never modify TW data.
- **No sub-agents**: Execute everything sequentially in bash.
- **Confirm before creating/closing**: Before executing step 4 and step 5, print what will be created/closed and ask the
  user for confirmation. This is a safety measure.
