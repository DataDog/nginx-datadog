# Contributing to the Datadog Nginx Module

## Documentation

Refer to the [documentation](doc) to learn about the development processes for the Datadog Nginx
Module. In particular, review:

- [Conventions](doc/conventions.md)
- [Development Processes](doc/development.md)

## Contribution Guidelines

When authoring a Pull Request (PR), you must follow these rules:

- You are the author, regardless of the tools you use.
  - Labels and signatures that mention tools are forbidden. Do not shift responsibility. Take
    ownership.
- Before submitting a PR, you must review every line in detail.
- A PR that is not humanly reviewable will be rejected. The criteria are:
  - The diff must be of reasonable size (usually less than 300 lines).
  - The PR description:
    - Must be written manually. Slightly imperfect wording is better than a long, unclear or
      cluttered description.
    - Must state the objective, and, unless obvious, the context and a high-level explanation.
    - Must explain what changed and why, but without restating implementation details.
    - Must not contain irrelevant details.
  - No long comments (unless truly needed).
  - No useless comments.
- The code must be clean. Notably (in addition to the above):
  - Short and focused functions (usually less than 20 lines, and less if possible).
  - Meaningful and understandable names. Avoid abbreviations; favor explicit names, even if long.
  - No code duplication.
- The PR must address only one concern.
- The PR must not include unrelated changes, unless truly tiny. Major cleanup, reformatting or
  reorganization must go in dedicated PRs.
- The PR must include tests that are easy to relate to the behavior they verify.
- The tests must focus on important behavior, not exhaustively cover minor details unlikely to
  break.
- The PR must have verifiable claims (such as test results).
- Commits must be in a logical and reviewable order.
- Commits message must be short and straight to the point (usually less than 3 lines).

## Pull Request Hygiene

- Draft PRs are not reviewed (unless explicitly requested).
- PRs not updated within one month of the latest review will be converted to drafts.
- Draft PRs not updated within three months will be closed.
