# v52 构建边界

这是源码审核候选，公开整理后的源码尚未重新构建。原52曾用本地SDK成功交叉编译；本说明不是新的构建通过记录。

## 依赖

使用devkitPro/libnx、Switch SDL2/SDL2_ttf、libcurl、Opus、带NVTEGRA扩展的FFmpeg、Deko3D及uam。普通桌面FFmpeg没有 `libavutil/hwcontext_nvtegra.h`，不能替代Switch对应版本。

1. `python scripts/fetch_deps.py` 获取sources.lock.json固定版本并核对档案哈希。
2. `python scripts/patch_deps.py` 先应用原Horizon兼容修改，再应用dependency-overrides内4个经哈希校验的网络源码覆盖。不得只复制主程序source而遗漏这些更改。
3. 安装现有uam后运行 `python scripts/build_shaders.py --uam /path/to/uam`，把四份GLSL编为本地 `romfs/shaders/*.dksh`。
4. 本地提供所需的 `romfs/signing.key`。本PR没有提供或读取你的文件；不要将它提交到Git，也不要公开包含它的NRO。
5. 配置 `DEVKITPRO`，使用仓库Switch工具链。Deko3D可通过 `-DUU_DEKO3D_ROOT=/path/to/prefix` 指定，或由SDK默认include/lib路径查找。
6. 配置／构建使用CMake+Ninja；完整操作可参考scripts/build.ps1。构建前检查SDK、GPU库及着色器版本一致，生成的资源仍在本地。

此前工作用到的参考源码：deko3d `373bfd43d8b2d805fa2eb7d406dbb823032df958`、dekotools `c15523cea963c04fad77a46f23c3a96d26a86ab9`，来源均为devkitPro上游。这是来源记录，不自动下载或安装这些工具。

参考APK纹理、Nintendo固件、密钥、用户SD备份、桌面forwarder及诊断数据不属于依赖分发。本版自有几何图标可在没有可选纹理时绘制；不把原参考素材纳入仓库。

## 本轮未执行

未运行测试套件、未做公开源码全新环境构建，未签发二进制发布。合并前需审核这些缺口，不能复用旧产物哈希冒充新构建。
