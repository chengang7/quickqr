#include "core.h"
#include <ReadBarcode.h>
#include <algorithm>
#include <cwctype>
#include <set>
#include <stdexcept>

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}
static bool Prefix(const std::wstring& s, size_t pos, const wchar_t* p) {
    return pos + wcslen(p) <= s.size() && _wcsnicmp(s.data() + pos, p, wcslen(p)) == 0;
}
bool IsWebLink(const std::wstring& s) {
    size_t start = Prefix(s, 0, L"https://") ? 8 : Prefix(s, 0, L"http://") ? 7 : 0;
    if (!start || s.size() > 16384) return false;
    for (auto c : s) if (c <= 32 || c == 127 || c == L'\\' || c == L'"' || c == L'<' || c == L'>') return false;
    auto end = s.find_first_of(L"/?#", start);
    auto host = s.substr(start, end == std::wstring::npos ? end : end - start);
    if (host.empty() || host.find(L'@') != std::wstring::npos) return false;
    if (host.front() == L'[') return host.find(L']') != std::wstring::npos;
    auto colon = host.find(L':');
    if (colon != std::wstring::npos) {
        auto port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), iswdigit)) return false;
        if (std::stoul(port) > 65535) return false;
        host.resize(colon);
    }
    return !host.empty() && host.front() != L'.' && host.back() != L'.';
}
std::vector<std::wstring> FindLinks(const std::wstring& s) {
    std::vector<std::wstring> out;
    for (size_t pos = 0; pos < s.size(); ++pos) {
        if (!Prefix(s, pos, L"https://") && !Prefix(s, pos, L"http://")) continue;
        if (pos && (iswalnum(s[pos - 1]) && s[pos - 1] < 128)) continue;
        size_t end = pos;
        while (end < s.size() && s[end] > 32 && s[end] != 127 &&
               std::wstring(L"<>\"'，。；！、【】（）《》“”\u200b").find(s[end]) == std::wstring::npos) ++end;
        auto u = s.substr(pos, end - pos);
        while (!u.empty() && std::wstring(L".,;").find(u.back()) != std::wstring::npos) u.pop_back();
        for (auto pair : {std::pair{L'(', L')'}, std::pair{L'[', L']'}, std::pair{L'{', L'}'}}) {
            while (!u.empty() && u.back() == pair.second &&
                   std::count(u.begin(), u.end(), pair.second) > std::count(u.begin(), u.end(), pair.first)) u.pop_back();
        }
        if (IsWebLink(u) && std::find(out.begin(), out.end(), u) == out.end()) out.push_back(u);
        pos = end ? end - 1 : pos;
    }
    return out;
}
ScanResult Decode(Gdiplus::Bitmap& source) {
    using namespace Gdiplus;
    ScanResult result;
    const UINT sw = source.GetWidth(), sh = source.GetHeight();
    if (source.GetLastStatus() != Ok || !sw || !sh) return {{}, L"无法读取图片，请使用 PNG、JPG、BMP 或 GIF 图片。"};
    if (sw > 24000 || sh > 24000 || uint64_t(sw) * sh > 80000000) return {{}, L"图片过大，请先裁剪到二维码附近。"};
    const double scale = std::min(1.0, 4096.0 / std::max(sw, sh));
    const int w = std::max(1, int(sw * scale)), h = std::max(1, int(sh * scale));
    Bitmap image(w, h, PixelFormat32bppRGB);
    { Graphics g(&image); g.Clear(Color(255,255,255)); g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      if (g.DrawImage(&source, Rect(0, 0, w, h), 0, 0, sw, sh, UnitPixel) != Ok) return {{}, L"图片转换失败。"}; }
    BitmapData data{}; Rect rect(0, 0, w, h);
    if (image.LockBits(&rect, ImageLockModeRead, PixelFormat32bppRGB, &data) != Ok) return {{}, L"无法读取图片像素。"};
    try {
        auto options = ZXing::ReaderOptions().setFormats(ZXing::BarcodeFormat::QRCode)
            .setTryHarder(true).setTryRotate(true).setTryInvert(true).setMaxNumberOfSymbols(32)
            .setTextMode(ZXing::TextMode::Plain);
        auto codes = ZXing::ReadBarcodes({(const uint8_t*)data.Scan0, w, h, ZXing::ImageFormat::BGRA, data.Stride}, options);
        std::set<std::wstring> seen;
        for (const auto& code : codes) if (code.isValid()) {
            auto text = Wide(code.text());
            if (!seen.insert(text).second) continue;
            auto links = FindLinks(text);
            if (links.empty()) result.items.push_back({text, {}});
            else for (auto& link : links) result.items.push_back({text, link});
        }
    } catch (...) { result.error = L"识别失败，请换一张图片或重新框选。"; }
    image.UnlockBits(&data);
    if (result.items.empty() && result.error.empty()) result.error = L"没有找到二维码。请框选完整二维码，保留四周白边后再试。";
    return result;
}
ScanResult DecodeFile(const std::wstring& path) {
    Gdiplus::Bitmap image(path.c_str());
    return Decode(image);
}
