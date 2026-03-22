______________________________________________________________________

## name: task-status description: Show project status report for rich-on-paper backlog

Generate a project status report by running these commands and summarizing:

1. Run `task project:rop summary` to get completion percentages
1. Run `task status:pending project:rop count` to get total remaining tasks
1. Run `task status:completed project:rop count` to get completed tasks
1. Run `task +ACTIVE project:rop list` to see any in-progress tasks
1. Run `task project:rop +BLOCKED list` to see blocked tasks and why
1. Run `task project:rop ready limit:5` to see the priority queue

Present the results as a concise status report with:

- Overall progress (X of Y tasks complete)
- What's currently in progress
- What's blocked and why
- Top 3 highest-priority unblocked tasks
- A one-line recommendation on what to focus on next

IMPORTANT: Only show tasks from `project:rop`. Never show tusk tasks.
