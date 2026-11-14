# 开发与验证

## 构建

需要 Visual Studio 2022 C++ 工作负载、Windows SDK 和 CMake 3.24+。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

main.cpp 负责窗口及系统交互，core.cpp 负责图像解码和 URL 提取。第三方库固定在 third_party/zxing-cpp-3.1.1，保持上游原样。

## 桌面集成测试

需要已解锁的 Windows 桌面，运行前先退出 QuickQR。测试会操作自身窗口，临时使用剪贴板，结束时尽力恢复剪贴板原内容，显示合成二维码并从真实桌面截图识别。不会写入自动打开设置。

```powershell
cmake --build build --config Release --target qr_ui_tests
.\build\Release\qr_tests.exe .\tests\fixtures
Start-Process .\build\Release\qr_ui_tests.exe -Wait
Get-Content .\tests\ui-report.txt
```

浏览器检查会打开 https://example.com/?quickqr-test=1#local-check。报告必须以 SUCCESS 结束；当前通过报告表达断言结果，不应仅依据进程退出码判断通过。CI 仅运行不需要桌面的核心测试。

测试图片、报告和截图不纳入 Git。测试中的网盘链接是合成数据，不是真实分享。

## 验证记录（2026-11-14）

34 项核心检查和 30 项窗口集成检查通过（含图片保存及工作线程完成检查）。覆盖百度/夸克链接参数、中文提取码、普通文本、非网页协议、旋转/反色/透明图片、多二维码、空白和不存在的图片；粘贴、复制、拖放、实际桌面框选、快捷键消息、取消、托盘恢复和 ShellExecute 默认浏览器调用。

局限：集成驱动直接触发自身窗口事件；快捷键已注册且消息处理通过验证，但未通过真实键盘注入验证。浏览器调用成功不代表网页加载成功。混合 DPI 多屏组合未完整验证。

## 图标

二维码定位标记搭配右上箭头，以紫蓝渐变和薄荷色突出“扫码打开”。assets/app.svg 是矢量版本；scripts/generate-icon.ps1 使用 Windows System.Drawing 生成 PNG 和九尺寸 ICO，在 16/20/24 像素下简化细节。ICO 内嵌于 EXE。
