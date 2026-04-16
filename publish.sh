#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_JSON="$ROOT_DIR/package.json"
LOCAL_ENV_FILE="$ROOT_DIR/publish.local.sh"

EXPECTED_PACKAGE_NAME="@xerxisfy/xon"
PUBLISH_TAG="${PUBLISH_TAG:-latest}"
NPM_CACHE_DIR="${NPM_CACHE_DIR:-$ROOT_DIR/.npm-cache}"

DRY_RUN=0
ALLOW_DIRTY=0
SKIP_VERSION_CHECK=0
OTP=""

TMP_NPMRC=""
NPM_TOKEN="${NPM_TOKEN:-}"

usage() {
  cat <<'EOF'
Usage: ./publish.sh [options]

Xon publish helper for @xerxisfy/xon.
Loads NPM_TOKEN from env or publish.local.sh, validates package metadata,
then publishes to npm with public access.

Options:
  --dry-run             Run validation + npm pack dry-run only (no publish)
  --tag <tag>           Publish with a custom dist-tag (default: latest)
  --otp <code>          Pass OTP for accounts that require one-time code
  --allow-dirty         Continue even with a dirty git worktree
  --skip-version-check  Skip npm version existence check
  --help, -h            Show help
EOF
}

log() {
  printf '[publish] %s\n' "$*"
}

warn() {
  printf '[publish] warning: %s\n' "$*" >&2
}

die() {
  printf '[publish] error: %s\n' "$*" >&2
  exit 1
}

cleanup() {
  if [[ -n "$TMP_NPMRC" && -f "$TMP_NPMRC" ]]; then
    rm -f "$TMP_NPMRC"
  fi
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

package_field() {
  local field_expr="$1"
  node -e "const pkg=require('$PACKAGE_JSON'); const v=pkg${field_expr}; if (v===undefined || v===null) process.exit(2); process.stdout.write(typeof v==='object' ? JSON.stringify(v) : String(v));"
}

load_token() {
  if [[ -f "$LOCAL_ENV_FILE" ]]; then
    # shellcheck disable=SC1091
    source "$LOCAL_ENV_FILE"
  fi

  NPM_TOKEN="${NPM_TOKEN:-}"
  [[ -n "${NPM_TOKEN// }" ]] || die "NPM_TOKEN is empty. Set it in env or publish.local.sh."
  [[ "$NPM_TOKEN" != "your_npm_token_here" ]] || die "NPM_TOKEN is a placeholder value."
}

ensure_node_version() {
  local major
  major="$(node -p "process.versions.node.split('.')[0]")"
  if [[ "$major" -lt 18 ]]; then
    die "Node 18+ is required. Current: $(node -v)"
  fi
}

ensure_git_state() {
  if [[ ! -d "$ROOT_DIR/.git" ]]; then
    warn "No .git directory found. Skipping git checks."
    return
  fi

  if ! git -C "$ROOT_DIR" diff --quiet --ignore-submodules -- || ! git -C "$ROOT_DIR" diff --cached --quiet --ignore-submodules --; then
    if [[ "$ALLOW_DIRTY" -eq 1 ]]; then
      warn "Publishing from a dirty worktree because --allow-dirty was set."
    else
      die "Git worktree has uncommitted changes. Commit/stash or rerun with --allow-dirty."
    fi
  fi
}

setup_npm_auth() {
  TMP_NPMRC="$(mktemp "${TMPDIR:-/tmp}/xon-npmrc.XXXXXX")"

  cat > "$TMP_NPMRC" <<EOF
//registry.npmjs.org/:_authToken=${NPM_TOKEN}
always-auth=true
EOF

  export NPM_CONFIG_USERCONFIG="$TMP_NPMRC"
  mkdir -p "$NPM_CACHE_DIR"
  export NPM_CONFIG_CACHE="$NPM_CACHE_DIR"
}

version_exists() {
  npm view "${PACKAGE_NAME}@${PACKAGE_VERSION}" version >/dev/null 2>&1
}

verify_published_version() {
  local attempts=12
  local delay_seconds=5
  local i
  local observed=""

  for ((i=1; i<=attempts; i+=1)); do
    observed="$(npm view "${PACKAGE_NAME}@${PACKAGE_VERSION}" version 2>/dev/null || true)"
    if [[ "$observed" == "$PACKAGE_VERSION" ]]; then
      log "Confirmed publish: ${PACKAGE_NAME}@${PACKAGE_VERSION}"
      return 0
    fi

    if [[ "$i" -lt "$attempts" ]]; then
      log "Waiting for npm registry propagation (${i}/${attempts})..."
      sleep "$delay_seconds"
    fi
  done

  warn "Publish command succeeded but registry has not reflected ${PACKAGE_NAME}@${PACKAGE_VERSION} yet."
  warn "Verify manually in a minute: npm view ${PACKAGE_NAME}@${PACKAGE_VERSION} version"
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dry-run)
        DRY_RUN=1
        shift
        ;;
      --tag)
        [[ $# -gt 1 ]] || die "--tag requires a value"
        PUBLISH_TAG="$2"
        shift 2
        ;;
      --otp)
        [[ $# -gt 1 ]] || die "--otp requires a value"
        OTP="$2"
        shift 2
        ;;
      --allow-dirty)
        ALLOW_DIRTY=1
        shift
        ;;
      --skip-version-check)
        SKIP_VERSION_CHECK=1
        shift
        ;;
      --help|-h)
        usage
        exit 0
        ;;
      *)
        die "Unknown option: $1"
        ;;
    esac
  done
}

validate_repo_shape() {
  [[ -f "$PACKAGE_JSON" ]] || die "Missing package.json"
  [[ -f "$ROOT_DIR/README.md" ]] || die "Missing README.md"
  [[ -f "$ROOT_DIR/LICENSE" ]] || die "Missing LICENSE"
  [[ -f "$ROOT_DIR/bin/xon" ]] || die "Missing CLI entrypoint: bin/xon"
  [[ -f "$ROOT_DIR/build.sh" ]] || die "Missing build.sh"
  [[ -f "$ROOT_DIR/scripts/test_cli.sh" ]] || die "Missing scripts/test_cli.sh"
}

run_publish() {
  PACKAGE_NAME="$(package_field '.name')" || die "Unable to read package name"
  PACKAGE_VERSION="$(package_field '.version')" || die "Unable to read package version"
  PACKAGE_REPOSITORY="$(package_field '.repository.url')" || die "Unable to read repository.url"

  [[ "$PACKAGE_NAME" == "$EXPECTED_PACKAGE_NAME" ]] || die "This script is locked to ${EXPECTED_PACKAGE_NAME}, found ${PACKAGE_NAME}"
  [[ "$PACKAGE_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]] || die "Version '${PACKAGE_VERSION}' is not a publishable semver value"
  [[ -n "$PACKAGE_REPOSITORY" ]] || die "repository.url is empty"

  log "Package: ${PACKAGE_NAME}@${PACKAGE_VERSION}"
  log "Tag: ${PUBLISH_TAG}"
  log "Repository: ${PACKAGE_REPOSITORY}"

  ensure_git_state
  setup_npm_auth

  log "Checking npm authentication"
  NPM_USER="$(npm whoami 2>/dev/null)" || die "npm authentication failed. Use a granular token with publish permission and bypass-2FA for this package."
  log "Authenticated as ${NPM_USER}"

  if [[ "$SKIP_VERSION_CHECK" -eq 0 ]]; then
    log "Checking if version already exists on npm"
    if version_exists; then
      die "Version already exists: ${PACKAGE_NAME}@${PACKAGE_VERSION}. Bump version before publishing."
    fi
  fi

  log "Running release checks (prepublishOnly)"
  npm run prepublishOnly

  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "Dry-run completed. Package was not published."
    return
  fi

  log "Publishing to npm"
  if [[ -n "$OTP" ]]; then
    npm publish --access public --tag "$PUBLISH_TAG" --otp "$OTP" --cache "$NPM_CACHE_DIR"
  else
    npm publish --access public --tag "$PUBLISH_TAG" --cache "$NPM_CACHE_DIR"
  fi

  verify_published_version
}

main() {
  trap cleanup EXIT

  cd "$ROOT_DIR"
  parse_args "$@"

  require_command node
  require_command npm
  require_command git

  ensure_node_version
  validate_repo_shape
  load_token
  run_publish
}

main "$@"
