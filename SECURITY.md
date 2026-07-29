# Security Policy

## Supported versions and fix policy

**mog** treats security seriously, with a simple support model:

- Security fixes are developed on the default branch and **ship in the next
  release** (or sooner via an out-of-band release if warranted).
- **We do not backport security fixes to older releases by default.**
- Exceptions may be made for **compelling reasons** (for example, a widely used
  previous release with a severe issue and a clear maintainer commitment). Any
  such exception is discretionary and will be called out in release notes.

| Line | Security fixes |
|------|----------------|
| Default branch (development) | Yes — fixed here first |
| Next / upcoming release | Yes — normal ship vehicle for fixes |
| Older released versions | **No** (unless an explicit exception) |

Always prefer upgrading to the latest release when a security fix is announced.

## Reporting a vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Prefer one of these private channels (first that applies):

1. **GitHub Private Vulnerability Reporting** (Security tab → Advisories →
   Report a vulnerability), if enabled on this repository.
2. Contact a repository maintainer privately (see the owner/maintainer profile
   or organization security contact).

Include:

- A description of the issue and its impact
- Steps to reproduce or a proof of concept if available
- Affected versions / commit SHAs if known

We will acknowledge receipt when possible and work with you on a coordinated
disclosure timeline. Please give maintainers reasonable time to investigate and
ship a fix (typically via the next release) before any public discussion.

## Preferred languages

English is preferred for security reports.
