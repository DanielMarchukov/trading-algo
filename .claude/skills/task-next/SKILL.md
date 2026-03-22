______________________________________________________________________

## name: task-next description: Show the next rich-on-paper task to work on with full context

Show the user what to work on next in the rich-on-paper project:

1. Run `task project:rop ready limit:1` to get the highest priority unblocked task
1. Run `task <id> info` on that task to get full details including annotations
1. If the task has dependencies, briefly note which completed tasks enabled it
1. Mark the task as started: `task <id> start`
1. Find the matching GitHub issue number:
   - If the task description starts with `github_issue:`, use that number directly
   - Otherwise run `gh issue list --search "<keywords from desc>"` to find it
   - If no matching issue exists, note this (the user should run `/task-sync` first)
1. Create a feature branch from mainline:
   - Format: `<github-issue-number>/<short-slug>` (e.g. `43/spdlog-logging`)
   - Derive the slug from the task description, lowercase, hyphens, max ~4 words
   - Run `git checkout mainline && git pull && git checkout -b <branch>`

Present:

- Task ID, title, project, estimate, tags
- Full description and acceptance criteria (from annotations prefixed with "AC:")
- Any relevant context about what this task builds on
- The branch name that was created
- Keep it brief — the user wants to start working, not read a report

IMPORTANT: Only show tasks from `project:rop`. Never show tusk tasks.
