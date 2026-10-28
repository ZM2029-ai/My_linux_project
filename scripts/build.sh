#!/usr/bin/env bash
# ============================================================
# EdgeLink 交叉构建脚本（在 Ubuntu 虚拟机宿主机上执行）
#
# 前置条件：已按 复现文档/阶段一/阶段一-交叉编译环境搭建与验证.md
#          搭好 Debian 10 chroot（默认 /opt/buster，内含 GCC 8.3）
#
# 做的事：
#   1) 把源码目录和构建目录 bind mount 进 chroot
#   2) 在 chroot 内用 GCC 8.3 交叉编译 gateway
#   3) 把产物取回虚拟机本地目录，并做兼容性检查
#
# 所有路径都可用环境变量覆盖，例如：
#   REPO_DIR=/mnt/hgfs/demo1 ./scripts/build.sh
# ============================================================
set -euo pipefail

# ---------- 可覆盖配置 ----------
REPO_DIR="${REPO_DIR:-$HOME/edgelink}"          # 源码目录（共享文件夹挂载点或 rsync 目标）
CHROOT_DIR="${CHROOT_DIR:-/opt/buster}"         # Debian 10 chroot 根目录
SRC_MOUNT="${CHROOT_DIR}/work"                  # chroot 内访问源码的路径
BUILD_DIR="${BUILD_DIR:-$HOME/build/edgelink}"  # 构建目录（放虚拟机本地盘，勿放共享盘）
BUILD_MOUNT="${CHROOT_DIR}/build"               # chroot 内访问构建产物的路径
OUT_DIR="${OUT_DIR:-$HOME/edgelink-bin}"        # 产物取回目录
JOBS="${JOBS:-2}"                               # 编译并行度（单核板子无关，这里指编译机）
# -------------------------------

info() { echo "[build] $*"; }
die()  { echo "[build][错误] $*" >&2; exit 1; }

[[ -d "$REPO_DIR" ]]  || die "源码目录不存在: $REPO_DIR（用 REPO_DIR=... 指定）"
[[ -d "$CHROOT_DIR" ]] || die "chroot 不存在: $CHROOT_DIR（参见复现文档）"
[[ -x "$CHROOT_DIR/usr/bin/arm-linux-gnueabihf-g++-8" ]] \
  || die "chroot 内缺少 GCC 8.3 交叉编译器（复现文档第 5 章）"
[[ -f "$REPO_DIR/gateway/CMakeLists.txt" ]] || die "找不到 $REPO_DIR/gateway/CMakeLists.txt"

info "源码目录 : $REPO_DIR"
info "chroot   : $CHROOT_DIR"

# ---------- 挂载 ----------
sudo mkdir -p "$SRC_MOUNT" "$BUILD_MOUNT" "$BUILD_DIR"

if mountpoint -q "$SRC_MOUNT"; then
    info "源码已挂载，跳过"
else
    info "挂载源码 -> $SRC_MOUNT"
    sudo mount --bind "$REPO_DIR" "$SRC_MOUNT"
fi

if mountpoint -q "$BUILD_MOUNT"; then
    info "构建目录已挂载，跳过"
else
    info "挂载构建目录 -> $BUILD_MOUNT"
    sudo mount --bind "$BUILD_DIR" "$BUILD_MOUNT"
fi

# ---------- 编译 ----------
info "开始交叉编译（GCC 8.3 / arm-linux-gnueabihf）"
sudo chroot "$CHROOT_DIR" /bin/bash -c "
set -e
cmake -S /work/gateway -B /build \
      -DCMAKE_TOOLCHAIN_FILE=/work/gateway/cmake/toolchain-arm-linux-gnueabihf.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build /build -- -j${JOBS}
"

# ---------- 取回产物 ----------
mkdir -p "$OUT_DIR"
cp "$BUILD_DIR/edgelink" "$OUT_DIR/edgelink"
chmod +x "$OUT_DIR/edgelink"

info "产物: $OUT_DIR/edgelink"

# ---------- 兼容性自检 ----------
echo "--- 文件类型 ---"
file "$OUT_DIR/edgelink" || true
echo "--- 需要的库 ---"
readelf -d "$OUT_DIR/edgelink" | grep NEEDED || true
echo "--- 需要的符号版本（上限：GLIBC_2.28 / GLIBCXX_3.4.25）---"
readelf -V "$OUT_DIR/edgelink" | grep -E 'Name: (GLIBC|GLIBCXX|CXXABI)' | sort -u || true

info "完成。下一步：scripts/deploy.sh 部署到开发板"
