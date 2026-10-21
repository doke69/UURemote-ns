"""Create SD-card-ready packages (Switch and Switch Lite) from a built NRO."""
import hashlib
import pathlib
import re
import shutil
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
version = re.search(r'project\(UURemote-ns VERSION ([\d.]+)',
                    (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
source = ROOT/'build-nx/UURemote-ns.nro'
data = source.read_bytes()
if data[16:20] != b'NRO0':
    raise ValueError('Not a valid NRO executable')
destination = ROOT/'dist/switch/UURemote-ns/UURemote-ns.nro'
destination.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, destination)
shutil.copy2(ROOT/'README.md', ROOT/'dist/README.md')
shutil.copy2(ROOT/'THIRD_PARTY.md', ROOT/'dist/THIRD_PARTY.md')
shutil.copy2(ROOT/'LICENSE', ROOT/'dist/LICENSE')
shutil.copytree(ROOT/'docs', ROOT/'dist/docs', dirs_exist_ok=True)
notices = {
    'libdatachannel-MPL-2.0.txt':'external/libdatachannel/LICENSE',
    'libjuice-MPL-2.0.txt':'external/libdatachannel/deps/libjuice/LICENSE',
    'usrsctp-BSD-3-Clause.txt':'external/libdatachannel/deps/usrsctp/LICENSE.md',
    'libsrtp-BSD-3-Clause.txt':'external/libdatachannel/deps/libsrtp/LICENSE',
    'plog-MIT.txt':'external/libdatachannel/deps/plog/LICENSE',
    'json-MIT.txt':'external/json/LICENSE.MIT',
    'qrcodegen-MIT.txt':'external/qrcodegen/Readme.markdown',
    'mbedtls-Apache-2.0.txt':'external/mbedtls/LICENSE',
}
notice_dir = ROOT/'dist/licenses'
notice_dir.mkdir(parents=True, exist_ok=True)
for name,path in notices.items():
    if (ROOT/path).is_file():
        shutil.copy2(ROOT/path,notice_dir/name)

COMMON_NOTES = """UU Remote NS {version} — 面向 23.0.1 / 大气层(AMS) 1.12.1 的适配版

安装
1. 确认 Switch / Switch Lite 已升级到系统 23.0.1（或 23.0.0），Atmosphère 为 1.12.1。
   若整合包仍是 1.12.0，请先升级 AMS：1.12.0 的 dns.mitm / fs.mitm 尚不支持 23.0.x 的新命令。
2. 将压缩包内的 switch 目录解压到 SD 卡根目录，得到：
   switch/UURemote-ns/UURemote-ns.nro
3. 在主机上按住 R 键启动任意游戏，进入完整内存模式 hbmenu，再运行 UU Remote NS。
   不支持相册小程序模式（内存不足），程序会给出提示。
4. 首页底部会显示机型 · 模式 · 系统版本，请确认显示正确。

注意
- 本程序为实验版，需要原版 UU远程 电脑端和 UU 账号；签名材料不随包提供。
- 连接异常时把 switch/UURemote-ns/connection.log 提供给开发者；该日志不含账号或令牌。
"""

SWITCH_NOTES = COMMON_NOTES + """
Switch（含 V2 / OLED）
- 掌机模式 720p，插入底座自动切换到 TV 1080p（UI 自动缩放）。
- TV 模式下触屏不可用，请使用 Joy-Con / Pro 手柄操作。
"""

LITE_NOTES = COMMON_NOTES + """
Switch Lite（HDH-001）
- 无 TV 输出，程序恒为掌机 720p 模式。
- 触屏始终可用；一体化手柄可直接操作。
- 建议保持 720p30 串流，以获得更好的解码与续航表现。
"""

(ROOT/'dist/安装说明-Switch.txt').write_text(
    SWITCH_NOTES.format(version=version), encoding='utf-8')
(ROOT/'dist/安装说明-SwitchLite.txt').write_text(
    LITE_NOTES.format(version=version), encoding='utf-8')

digest = hashlib.sha256(data).hexdigest()
(ROOT/'dist/SHA256SUMS').write_text(f'{digest}  switch/UURemote-ns/UURemote-ns.nro\n')
archives = []
for target, name, note in (
        ('switch', f'UURemote-ns-{version}-switch.zip', '安装说明-Switch.txt'),
        ('switch-lite', f'UURemote-ns-{version}-switch-lite.zip', '安装说明-SwitchLite.txt')):
    archive = ROOT/'dist'/name
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        paths = [destination, ROOT/'dist/README.md', ROOT/'dist/THIRD_PARTY.md',
                 ROOT/'dist/LICENSE', ROOT/'dist/SHA256SUMS']
        paths += list((ROOT/'dist/docs').rglob('*')) + list(notice_dir.glob('*'))
        paths.append(ROOT/'dist'/note)
        for path in sorted(set(paths), key=lambda p: p.relative_to(ROOT/'dist').as_posix()):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(path.relative_to(ROOT/'dist').as_posix(), (1980,1,1,0,0,0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, path.read_bytes(), compresslevel=9)
    archives.append(archive)
    print(archive)
(ROOT/'dist/ARCHIVE-SHA256SUMS').write_text(
    f'{digest}  switch/UURemote-ns/UURemote-ns.nro\n' +
    ''.join(f'{hashlib.sha256(a.read_bytes()).hexdigest()}  {a.name}\n' for a in archives))
print('NRO SHA256:', digest)
