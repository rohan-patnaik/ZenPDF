# Final QA Checklist

## Core flows
- Sign in/out and anonymous sessions behave as expected.
- Tool selection, validation, upload, and job queueing succeed.
- Job status updates and progress indicators refresh correctly.
- Output downloads stream with the expected filename and size.
- Usage & Capacity reflects real usage and limits.

## Premium + teams
- Premium gates for workflows and teams enforce access.
- Team member invites and removals update shared workflows.

## Error handling
- Friendly errors render for limits, invalid input, and capacity.
- Retry guidance appears for transient failures.

## Worker
- Worker claims jobs, updates progress, and uploads outputs.
- Artifacts expire based on TTL settings.

## Browsers
- Chrome, Safari, and mobile layouts render correctly.
- Keyboard navigation reaches all primary actions.
