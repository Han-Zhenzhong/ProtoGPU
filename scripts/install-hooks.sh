#!/usr/bin/env bash
set -e

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
HOOK_SRC_DIR="$REPO_ROOT/scripts/git-hooks"
HOOK_DST_DIR="$REPO_ROOT/.git/hooks"

echo "[install-hooks] Installing git hooks..."

# 1. 确保在 git 仓库中
if [ -z "$REPO_ROOT" ]; then
  echo "❌ Not inside a git repository."
  exit 1
fi

# 2. 检查 hooks 源目录
if [ ! -d "$HOOK_SRC_DIR" ]; then
  echo "❌ Hook source directory not found:"
  echo "   $HOOK_SRC_DIR"
  exit 1
fi

# 3. 确保 .git/hooks 存在
mkdir -p "$HOOK_DST_DIR"

install_hook() {
  local hook_name="$1"
  local src="$HOOK_SRC_DIR/$hook_name"
  local dst="$HOOK_DST_DIR/$hook_name"

  if [ ! -f "$src" ]; then
    echo "⚠️  $hook_name not found in scripts/git-hooks/, skipping."
    return
  fi

  # 可执行
  chmod +x "$src"

  # 如果已存在 hook
  if [ -e "$dst" ] && [ ! -L "$dst" ]; then
    echo "⚠️  Existing $hook_name found, backing up to ${hook_name}.bak"
    mv "$dst" "$dst.bak"
  fi

  # 创建软链接
  ln -sf "$src" "$dst"
  echo "✅ Installed $hook_name"
}

# 4. 安装 hooks
install_hook pre-commit
install_hook prepare-commit-msg
install_hook commit-msg

echo ""
echo "[install-hooks] Done."
echo ""
echo "If hooks do not run, make sure:"
echo "  - You are not using --no-verify"
echo "  - File system supports symlinks (WSL/Linux OK)"