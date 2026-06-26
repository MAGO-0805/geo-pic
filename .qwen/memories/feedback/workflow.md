---
name: workflow
description: User prefers to run test renders themselves; I should only write code and ensure compilation succeeds
type: feedback
---

Do not run rendering tests or benchmark renders. The user will do test renders themselves.
**Why:** User wants to control rendering (expensive computation) and prefers I focus on writing correct code that compiles.
**How to apply:** After code changes, verify compilation succeeds. If testing is needed, tell the user which testcase to run and with what config. Never run PA1 binary on large scenes (cornell, face_light, etc.) — compile-only verification.
