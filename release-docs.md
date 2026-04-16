# Xon Release Operations Guide

Last updated: 2026-04-13

This document is the canonical release runbook for Xon. It covers release preparation, validation, publish steps, rollback guidance, and post-release checks.

## 1. Scope and Release Targets

Primary package covered here:
- npm: `@xerxisfy/xon` (core package)

Secondary targets (managed separately, optional per release cycle):
- npm: `@xerxisfy/xon-language-server`
- VS Code extension package in `vscode-xon/`
- Python package from `bindings/python/`

This runbook prioritizes shipping the core npm package first.

## 2. Release Roles and Responsibilities

Typical responsibilities:
- Release owner: runs checklist, tags, and publish commands.
- Reviewer/approver: validates changelog and high-risk diffs.
- CI observer: confirms post-tag and post-publish checks.

Single-maintainer flow is acceptable, but still run every checklist item.

## 3. Prerequisites

Required tooling:
- Node.js 18+ (for core package).
- npm configured and authenticated.
- C compiler (`gcc` or `clang`) available.
- Python 3 available if running Python checks.

Required access:
- npm publish rights for `@xerxisfy/xon`.
- Git push rights for repo and tags.

Authentication check:
```bash
npm whoami
```

If unauthenticated:
```bash
npm adduser
# or
npm login
```

## 4. Branch and Version Policy

Recommended branch strategy:
- Release from a clean branch based on `main`.
- Use a branch name like `codex/release-v1.0.0`.

Versioning:
- Follow semver (`MAJOR.MINOR.PATCH`).
- Update `package.json` version before publish if needed.

## 5. Mandatory Pre-Release Checks

Run from repo root:

```bash
# Ensure correct Node version
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"
nvm use 18

# Core build + tests
./build.sh
./scripts/run_tests.sh
npm run prepublishOnly
./scripts/release_check.sh
```

Expected outcomes:
- `build.sh` succeeds (with parser fallback if bundled lemon is incompatible).
- C tests pass.
- Node addon tests pass.
- CLI test passes.
- `npm pack --dry-run` succeeds.
- `release_check.sh` completes (may skip VS Code packaging on Node < 20).

## 6. Package Inspection and Smoke Test

Dry-run package creation:
```bash
npm run pack:preview
```

Create actual tarball:
```bash
npm pack --cache ./.npm-cache
```

Install smoke test in fresh temp dir:
```bash
tmpdir=$(mktemp -d /tmp/xon-pkg-test.XXXXXX)
cd "$tmpdir"
npm init -y
npm install /absolute/path/to/xerxisfy-xon-1.0.0.tgz
node -e "const x=require('@xerxisfy/xon'); console.log(x.xonifyString('{a:1}').a);"
./node_modules/.bin/xon validate sample.xon
```

What to verify:
- package installs without build failures.
- JS API works.
- npm CLI binary `xon` is present and functional.

## 7. Core npm Publish Procedure

From repo root:

```bash
npm publish --access public --cache ./.npm-cache
```

Common publish outcomes:
- Success: package is live on npm.
- Failure `ENEEDAUTH`: run `npm adduser` or `npm login`, then retry.
- Failure `E403`: permission/scope ownership issue.

Live package verification:
```bash
npm view @xerxisfy/xon version
npm view @xerxisfy/xon dist-tags
```

## 8. Exact Git Commit and Tag Commands for v1.0.0

Use this exact sequence after final review:

```bash
# 1) Create release branch (optional but recommended)
git checkout -b codex/release-v1.0.0

# 2) Stage all release files
git add .

# 3) Commit
git commit -m "release: prepare v1.0.0 core package"

# 4) Push branch
git push -u origin codex/release-v1.0.0

# 5) Merge to main via PR (or locally if you own main policy)
# After merge and on main:
git checkout main
git pull origin main

# 6) Tag release
git tag -a v1.0.0 -m "Release v1.0.0"

# 7) Push tag
git push origin v1.0.0
```

If publishing directly from `main` without release branch:

```bash
git checkout main
git pull origin main
git add .
git commit -m "release: prepare v1.0.0 core package"
git push origin main
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

## 9. Post-Publish Validation

After successful publish:

```bash
# Verify package metadata
npm view @xerxisfy/xon

# Optional global install smoke
npm install -g @xerxisfy/xon
xon --help
```

Runtime checks:
- parse sample `.xon` file
- `xon convert` roundtrip
- `xon eval` on expression-heavy sample

## 10. Release Notes Template

Use this template per release:

```markdown
## Xon v1.0.0

### Highlights
- Added npm CLI packaging with `xon` command.
- Hardened build flow with parser-generation fallback.
- Strengthened prepublish and release checks for Node 18.
- Consolidated repository documentation.

### Verification
- `./build.sh`
- `./scripts/run_tests.sh`
- `npm run prepublishOnly`
- `./scripts/release_check.sh`
- fresh tarball install smoke test

### Known Limitations
- Import/include language features are not yet first-class.
- VS Code packaging requires Node 20+.
```

### 10.1 v1.0.0 Public Messaging (Copy/Paste)

Use this block in npm changelog/release notes:

```markdown
## What Developers Should Expect in v1.0.0

Xon v1.0.0 is production-ready for trusted, version-controlled configuration files.

### Included in v1.0.0
- Native Node package (`@xerxisfy/xon`) with runtime parser and CLI.
- CLI commands: `validate`, `format`, `convert`, `eval`, `build`.
- Runtime expressions: `let/const`, operators, functions/closures, built-ins.
- C API for parsing, evaluating, traversing, and serializing config data.
- Optional `xon.config.json` convention for zero-argument build-time JSON generation.

### Recommended Production Use
- Parse and validate config in CI before deploy.
- Keep `.xon` files in source control and enforce formatting.
- Add schema validation in app code after parsing.
- Use `eval` only for trusted input or isolated execution contexts.

### Not Included Yet
- First-class import/include module system.
- Built-in schema validation engine.
- Full advanced literal roadmap (binary/octal and other planned syntax extensions).
```

## 11. Rollback and Incident Procedures

### 11.1 npm dist-tag rollback

If latest publish is bad but should remain available:

```bash
npm dist-tag add @xerxisfy/xon@<previous-good-version> latest
```

### 11.2 Deprecation (preferred over unpublish for mature packages)

```bash
npm deprecate @xerxisfy/xon@1.0.0 "Known issue: <reason>. Use <fixed-version>."
```

### 11.3 Unpublish constraints

`npm unpublish` is heavily restricted and should be treated as emergency-only.
Use deprecation + new patch release whenever possible.

## 12. Secondary Target Release Notes

### 12.1 Language Server

From `xon-language-server/`:
```bash
npm install
npm publish --access public
```

### 12.2 VS Code Extension

From `vscode-xon/` (Node 20+ recommended):
```bash
npm install
npm run package
npm run publish:vsce
```

### 12.3 Python Package

From repo root:
```bash
python3 -m pip install --upgrade build twine
python3 -m build --wheel bindings/python --outdir dist-python
python3 -m twine check dist-python/*
python3 -m twine upload dist-python/*
```

## 13. Audit Trail Checklist

For each release, archive:
- commit hash and tag
- `npm publish` output snippet
- `npm view` verification output
- test/preflight command outputs
- release notes link

## 14. Known Current State (2026-04-13)

- Core package prepublish checks pass on Node 18.
- `npm publish` was attempted and blocked only by authentication (`ENEEDAUTH`) when credentials are missing on machine.
- After login, publishing command can be rerun without additional code changes.

## 15. Canonical Release Artifacts

Keep these as release sources of truth:
- `README.md` for project entry overview.
- `docs.md` for long-lived technical details.
- `release-docs.md` for operational release execution.

## 16. Maintainer Contact

- Maintainer: Isaac Muliro
- Repository: https://github.com/xerxisfy/xon
- Issues: https://github.com/xerxisfy/xon/issues
- Discussions: https://github.com/xerxisfy/xon/discussions
- Email: xerxisfyágmail.com
