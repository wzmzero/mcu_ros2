#!/usr/bin/env bash
set -euo pipefail
tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
web_python="${ROS_WEB_PYTHON:-python3}"
if [[ -z "${ROS_WEB_PYTHON:-}" && -x "$tools_dir/build/web/venv/bin/python" ]]; then
  web_python="$tools_dir/build/web/venv/bin/python"
fi
if ! "$web_python" -c 'import aiohttp' 2>/dev/null; then
  echo '缺少 aiohttp。安装 sudo apt install python3-aiohttp，或使用 venv 安装 Tools/runtime/web/requirements.txt 并设置 ROS_WEB_PYTHON。' >&2
  exit 1
fi
exec "$web_python" "$tools_dir/runtime/web/server.py" "$@"
