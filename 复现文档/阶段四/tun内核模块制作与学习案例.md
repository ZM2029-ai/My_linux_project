# i.MX6ULL 上制作 tun.ko 的内核学习案例

本案例记录 2026 年 11 月 7 日为 Debian 开发板补充 TUN 驱动的过程。学习重点是识别正在运行的内核、选择源码和配置、准备 ARM 交叉编译工具、生成模块以及在开发板上验证加载。此前的完整问题记录见 [SSH 与 Tailscale 排查记录](<E:/GPT skill/ssh/SSH与Tailscale排查记录.md>)。

## 1. 问题从哪里出现

开发板安装 Tailscale 后，`tailscale up` 返回 `503 Service Unavailable: no backend`。检查 `tailscaled` 服务日志，发现它创建 `tailscale0` 时无法打开 `/dev/net/tun`。继续检查发现板端没有可加载的 TUN 模块。

TUN 是一种虚拟网络设备接口。用户态程序可以通过 `/dev/net/tun` 与内核交换 IP 数据包，内核中对应的 TUN 驱动负责把它接入网络栈。`tailscale0` 是 Tailscale 使用该机制创建的网络接口；`/dev/net/tun` 是程序访问驱动的字符设备节点，二者承担不同作用。 [Linux TUN/TAP 文档](https://docs.kernel.org/networking/tuntap.html)

```text
tailscaled 进程 ↔ /dev/net/tun ↔ 内核 TUN 驱动 ↔ tailscale0 ↔ Linux 网络栈
```

## 2. 先观察运行中的内核

下面是在板上定位问题时使用的只读检查方法：

```bash
uname -r
cat /proc/version
if [ -r /proc/config.gz ]; then
  zcat /proc/config.gz
else
  cat "/boot/config-$(uname -r)"
fi | grep -E '^(CONFIG_(TUN|MODULES|MODVERSIONS)=|# CONFIG_(TUN|MODULES|MODVERSIONS) is not set)'
grep -w tun /proc/misc
find "/lib/modules/$(uname -r)" -name 'tun.ko*'
ls -ld "/lib/modules/$(uname -r)/build"
```

本次检查到内核版本为 `4.19.35-imx6`，镜像包版本 `1.2307stable`，原构建使用 GCC 7.5.0。内核配置和检查结果如下：

| 项目 | 本次结果 | 含义 |
| --- | --- | --- |
| `CONFIG_MODULES=y` | 已开启 | 内核支持加载模块。 |
| `CONFIG_MODVERSIONS=y` | 已开启 | 模块导入的符号使用版本信息参与兼容性检查。 |
| `# CONFIG_TUN is not set` | 关闭 | 当前内核配置没有启用 TUN。 |
| `/proc/misc`、模块目录 | 没有找到 TUN | 板上没有现成可用的 TUN 模块。 |
| 内核 build 目录和 `Module.symvers` | 缺失 | 需要准备相匹配的源码、配置和构建产物。 |

Kconfig 中 `y` 表示编入内核，`m` 表示构建成模块，未设置表示关闭。内核版本字符串只是兼容性线索，还需要考虑配置、导出符号版本及构建条件。 [Linux 外部模块构建文档](https://docs.kernel.org/kbuild/modules.html)

## 3. 寻找材料和准备构建

1. 从开发板的 `/proc/config.gz` 或 `/boot/config-$(uname -r)` 导出完整配置为 `npi-kernel.config`，传到 Ubuntu 虚拟机，并用 SHA-256 核对传输前后的文件。
2. 选择野火内核源码分支 `ebf_4.19.35_imx6ul` 的候选提交 [`a88cfd52fc79c6923510c8536b734ec3497155b8`](https://github.com/Embedfire/ebf_linux_kernel/commit/a88cfd52fc79c6923510c8536b734ec3497155b8)。当时无法证明它就是板上镜像的原始提交，后续通过实际加载验证了生成模块可用于当时运行的内核。
3. 在 Ubuntu 的独立目录准备 GCC 7.5 ARM 交叉工具链和 Binutils 2.30。源码和内核构建输出使用临时内存文件系统，最终 `tun.ko` 保存在 `/home/zm/tun-module-build/tun.ko`。
4. 将板端配置复制为构建输出目录的 `.config`，把 TUN 设置成 `m`，运行 `olddefconfig`。本次配置比较的结果是 `CONFIG_TUN` 从关闭变为模块，`CONFIG_MODVERSIONS` 保持开启。

本次还遇到三个具体问题。GCC 曾选到宿主机汇编器，导致 `invalid -march=armv7-a`，通过指定 ARM 工具前缀和 GCC `-B` 路径解决。编译器缺少 `stdarg.h` 时，补齐 GCC 7.5 对应的 ARM 交叉开发文件。Ubuntu 24.04 的 Binutils 2.42 对旧 ARM 汇编报错，最终使用 Ubuntu Bionic 的 ARM Binutils 2.30。

## 4. 构建过程和每一步的意义

以下展示当时实际构建脚本的核心内容。`work` 下的配置、工具链和 `scratch-path` 是当次准备好的文件。`scratch-path` 指向临时源码与输出目录，清理或重启后可能失效，因此这段命令用于理解和核对原过程，重新操作前需要准备对应材料与路径。

```bash
work="$HOME/tun-module-build"
scratch=$(cat "$work/scratch-path")
source="$scratch/kernel"
out="$scratch/out"
mkdir -p "$out"
export LD_LIBRARY_PATH="$work/toolchain/usr/lib/x86_64-linux-gnu"
gcc="$work/toolchain/usr/bin/arm-linux-gnueabihf-gcc-7"

build=(make -C "$source" O="$out" ARCH=arm \
  CROSS_COMPILE="$work/toolchain/usr/bin/arm-linux-gnueabihf-" \
  "CC=$gcc -B$work/toolchain/usr/arm-linux-gnueabihf/bin/" \
  LOCALVERSION=-imx6 HOSTCFLAGS=-fcommon)

cp "$work/npi-kernel.config" "$out/.config"
"$source/scripts/config" --file "$out/.config" --module TUN
"${build[@]}" olddefconfig
python3 "$source/scripts/diffconfig" "$work/npi-kernel.config" "$out/.config"
"${build[@]}" -s kernelrelease
"${build[@]}" -j4 vmlinux
"${build[@]}" -j4 drivers/net/tun.ko
install -m 644 "$out/drivers/net/tun.ko" "$work/tun.ko"
file "$work/tun.ko"
modinfo "$work/tun.ko"
modprobe --show-modversions "$work/tun.ko"
sha256sum "$work/tun.ko"
```

`ARCH=arm` 指定目标架构，`CROSS_COMPILE` 指向 ARM 工具前缀，`O` 把生成文件放在独立输出目录。`olddefconfig` 根据现有配置补齐默认项，`kernelrelease` 用来检查输出内核版本。由于本板启用了 `CONFIG_MODVERSIONS`，这次先构建 `vmlinux` 以生成内核符号版本信息，再构建 `drivers/net/tun.ko`。内核文档说明单独运行 `modules_prepare` 不会生成 `Module.symvers`。 [Linux 模块版本说明](https://docs.kernel.org/kbuild/modules.html#module-versioning)

实际生成的文件为 32 位 ARM EABI5，`modinfo` 显示：

```text
vermagic: 4.19.35-imx6 SMP preempt mod_unload modversions ARMv7 p2v8
SHA-256: 22bc024afc00e9561d20a43ca8920d9db446bb91a18738a52ea062fcefe4f089
```

`vermagic` 是检查模块构建目标的线索，实际加载仍需要由内核完成检查。`modprobe --show-modversions` 当时列出了 177 条导入符号版本记录。

## 5. 传输、加载与持久配置

当时先从 Ubuntu 将 `tun.ko` 复制到 Windows U 盘，再接入开发板。用 `lsblk` 确认 U 盘分区后，只读挂载并复制到 `/tmp`。当次 U 盘分区是 `/dev/sda1`，设备名可能随插入顺序变化。

```bash
lsblk -o NAME,TRAN,SIZE,FSTYPE,LABEL,MOUNTPOINT
# 核对实际 U 盘分区后执行，下面的 /dev/sda1 是本次记录。
sudo mkdir -p /mnt/usb
sudo mount -o ro /dev/sda1 /mnt/usb
cp /mnt/usb/tun.ko /tmp/tun.ko
sha256sum /tmp/tun.ko
sudo insmod /tmp/tun.ko
ls -l /dev/net/tun
sudo dmesg | tail -n 30
```

`insmod` 按给定文件路径加载模块。本次 `dmesg` 出现 `tun: Universal TUN/TAP device driver, 1.6`，随后 Tailscale 建立了 `tailscale0`，Windows 经 Tailscale 成功 SSH 登录板端 `npi`。

临时加载成功后，当时按以下方式安装模块并登记开机加载：

```bash
sudo install -D -m 0644 /tmp/tun.ko "/lib/modules/$(uname -r)/updates/tun.ko"
sudo depmod -a
echo tun | sudo tee /etc/modules-load.d/npi-tun.conf
```

`depmod` 更新模块依赖索引，`modules-load.d` 配置为开机加载提供模块名。配置已记录，但当时没有完成重启后的自动加载验收。原始记录还包括重启 `tailscaled` 和 `tailscale up` 的步骤。

## 6. 学习时可以重点追踪

- **配置和构建系统**：从 `drivers/net/Kconfig` 中的 `TUN` 配置追到 `drivers/net/Makefile` 的构建规则，再看 `drivers/net/tun.c`。
- **模块生命周期**：在源码里查 `module_init`、`module_exit` 和设备注册代码，理解加载与卸载时分别发生什么。
- **用户态接口**：查 `TUNSETIFF`、`ioctl`、文件读写和网络设备收发路径，跟踪用户进程如何建立并使用虚拟网卡。
- **内核兼容性**：对比 `.config`、`uname -r`、`modinfo`、`Module.symvers`、内核日志，理解为什么同一个 `.ko` 需要针对目标内核构建。
- **复盘证据**：本案例的运行日志、构建日志和原内核配置应优先从 `/home/zm/tun-module-build/` 查找。临时源码目录是否仍存在取决于虚拟机后续状态。

相关阅读：[Linux TUN/TAP 官方文档](https://docs.kernel.org/networking/tuntap.html)、[Linux 内核模块构建文档](https://docs.kernel.org/kbuild/modules.html)、[野火 i.MX6ULL 内核开发指南](https://doc.embedfire.com/linux/imx6/driver/zh/latest/linux_driver/exper_env.html)。
