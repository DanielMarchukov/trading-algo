______________________________________________________________________

## name: task-done description: Mark the current rich-on-paper task as done and show what unblocked

Complete a task and show what's next:

1. Run `task +ACTIVE project:rop list` to find the currently active (started) task
1. If there's an active task, run `task <id> done` to complete it
1. If no active task and the user specified a task ID in their message, complete that one
1. If neither, ask the user which task to complete
1. Switch back to mainline: `git checkout mainline && git pull`
1. After completing, run `task project:rop ready limit:3` to show what unblocked and what's next
1. Run `task project:rop summary` for updated progress

Present:

- Which task was completed
- What tasks are now unblocked (if any)
- The next task to work on
- Updated project progress percentage

IMPORTANT: Only operate on tasks from `project:rop`. Never touch tusk tasks.
