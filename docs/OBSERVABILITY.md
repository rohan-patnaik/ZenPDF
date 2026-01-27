# ZenPDF Observability

## Goals
- Track job throughput, latency, and failures.
- Watch capacity and budget pressure.
- Detect abuse and regressions early.

## Logging Guidelines
- Use structured logs with jobId, tool, tier, teamId, and requestId.
- Log status transitions: queued -> running -> completed or failed.
- Redact PII and omit file contents; log file size and page count only.
- Emit stable error codes and the user-facing message key.

## Metrics to Capture
- Jobs created, completed, failed per tool and tier.
- Queue time, processing time, and total duration.
- Worker concurrency and retries.
- Storage bytes and TTL cleanup counts.
- Budget usage and heavy tool disablement flags.
- Download success, failure, and size distribution.

## Tracing and Correlation
- Propagate a requestId from web to Convex to worker.
- Include it in log lines and job metadata.

## Alerts
- Sustained failure rate above 5 percent.
- Queue latency above 2x baseline.
- Budget near monthly cap.
- Worker crash loops or zero active workers.

## Dashboards
- Capacity overview (jobs per day, concurrency, budget).
- Tool health (success rate and median duration).
- Cost drivers (storage and heavy tool usage).

## QA Checklist
- Confirm logs appear for happy and error paths.
- Verify metrics update for synthetic jobs.
