#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
struct Item { std::wstring text; std::wstring url; };
struct ScanResult { std::vector<Item> items; std::wstring error; };
std::wstring Wide(const std::string& value);
std::vector<std::wstring> FindLinks(const std::wstring& text);
bool IsWebLink(const std::wstring& text);
ScanResult Decode(Gdiplus::Bitmap& bitmap);
ScanResult DecodeFile(const std::wstring& path);
