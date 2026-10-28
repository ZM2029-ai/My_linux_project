#!/usr/bin/env bash
# ============================================================
# EdgeLink 部署脚本（把交叉编译产物送到 i.MX6ULL 开发板上运行）
#
# 可在虚拟机的 bash 里运行；Windows 上可用 Git Bash 运行。
# 需要能 ssh 到开发板（会提示输入密码，或事先配好公钥）。
#
#   ./scripts/deploy.sh                    # 部署并试运行
#   BOARD_HOST=10.192.110.142 ./scripts/deploy.sh
# ============================================================
set -euo pipefail

# ---------- 可覆盖配置 ----------
BOARD_USER="${BOARD_USER:-debian}"
BOARD_HOST="${BOARD_HOST:-10.192.110.142}"
BOARD_PORT="${BOARD_PORT:-22}"
REMOTE_DIR="${REMOTE_DIR:-/home/debian/edgelink}"   # 板上运行目录
LOCAL_BIN="${LOCAL_BIN:-$HOME/edgelink-bin/edgelink}" # 本地交叉编译产物
LOCAL_CONF="${LOCAL_CONF:-}"                        # 可选：要一起上传的配置文件
RUN_AFTER_DEPLOY="${RUN_AFTER_DEPLOY:-1}"           # 1=部署后立即试运行
# -------------------------------

info() { echo "[deploy] $*"; }
die()  { echo "[deploy][错误] $*" >&2; exit 1; }

[[ -f "$LOCAL_BIN" ]] || die "找不到本地产物: $LOCAL_BIN（先运行 scripts/build.sh）"

SSH_OPTS=(-p "$BOARD_PORT")
SCP_OPTS=(-P "$BOARD_PORT")
TARGET="${BOARD_USER}@${BOARD_HOST}"

info "目标: ${TARGET}:${REMOTE_DIR}"

# 建目录（日志、配置都放这里）
ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$REMOTE_DIR/logs'"

# 先传成新名字，成功后再替换，避免传输中断留下半个文件
info "上传可执行文件"
scp "${SCP_OPTS[@]}" "$LOCAL_BIN" "${TARGET}:${REMOTE_DIR}/edgelink.new"
ssh "${SSH_OPTS[@]}" "$TARGET" \
  "mv -f '$REMOTE_DIR/edgelink.new' '$REMOTE_DIR/edgelink' && chmod +x '$REMOTE_DIR/edgelink'"

if [[ -n "$LOCAL_CONF" ]]; then
    [[ -f "$LOCAL_CONF" ]] || die "配置文件不存在: $LOCAL_CONF"
    info "上传配置"
    scp "${SCP_OPTS[@]}" "$LOCAL_CONF" "${TARGET}:${REMOTE_DIR}/edgelink.conf"
fi

if [[ "$RUN_AFTER_DEPLOY" == "1" ]]; then
    info "板上试运行"
    ssh "${SSH_OPTS[@]}" "$TARGET" "cd '$REMOTE_DIR' && ./edgelink"
fi

info "完成"
