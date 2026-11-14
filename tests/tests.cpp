#include "core.h"
#include <MultiFormatWriter.h>
#include <BitMatrix.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Gdiplus;
int checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::unique_ptr<Bitmap> Make(const std::wstring& text) {
    auto bits=ZXing::MultiFormatWriter(ZXing::BarcodeFormat::QRCode).setEncoding(ZXing::CharacterSet::UTF8).setMargin(4).encode(text,320,320);
    auto b=std::make_unique<Bitmap>(bits.width(),bits.height(),PixelFormat32bppRGB);
    BitmapData d{};Rect r(0,0,bits.width(),bits.height());b->LockBits(&r,ImageLockModeWrite,PixelFormat32bppRGB,&d);
    for(int y=0;y<bits.height();++y)for(int x=0;x<bits.width();++x){auto p=(unsigned char*)d.Scan0+y*d.Stride+x*4;p[0]=p[1]=p[2]=bits.get(x,y)?0:255;p[3]=255;}
    b->UnlockBits(&d);return b;
}
void Save(Bitmap& b,const std::filesystem::path& p) { CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};Check(b.Save(p.c_str(),&png)==Ok,"save fixture"); }
int wmain(int argc,wchar_t** argv) {
    GdiplusStartupInput in;ULONG_PTR token;GdiplusStartup(&token,&in,nullptr);int result=0;
    try {
        auto path=std::filesystem::path(argc>1?argv[1]:L"fixtures");std::filesystem::create_directories(path);
        std::wstring baidu=L"https://pan.baidu.com/s/1QuickQR-test?pwd=aB12",quark=L"https://pan.quark.cn/s/0123456789ab";
        Check(FindLinks(baidu)==std::vector{baidu},"preserve Baidu password query");
        Check(FindLinks(L"分享链接："+quark+L"，提取码：1234")==std::vector{quark},"extract URL from Chinese text");
        Check(FindLinks(L"链接 ("+baidu+L")")==std::vector{baidu},"strip wrapping punctuation");
        Check(FindLinks(quark+L"\n"+quark).size()==1,"deduplicate URLs");
        Check(FindLinks(quark+L" "+baidu).size()==2,"multiple URLs");
        Check(FindLinks(L"javascript:alert(1)").empty(),"reject script");
        Check(!IsWebLink(L"file:///C:/Windows/notepad.exe"),"reject file");
        Check(!IsWebLink(L"ms-settings:display"),"reject protocol");
        Check(!IsWebLink(L"https://"),"reject empty host");
        Check(!IsWebLink(L"https://user@example.com/x"),"reject userinfo");
        Check(!IsWebLink(L"https://example.com\\x"),"reject backslash");
        Check(!IsWebLink(L"https://example.com:99999999/x"),"reject invalid port");
        Check(IsWebLink(L"HTTPS://example.com:443/a#x"),"uppercase scheme and fragment");
        auto b=Make(baidu);Save(*b,path/L"baidu.png");auto r=Decode(*b);Check(r.items.size()==1&&r.items[0].url==baidu,"decode Baidu");
        auto q=Make(quark);Save(*q,path/L"quark.png");r=Decode(*q);Check(r.items.size()==1&&r.items[0].url==quark,"decode Quark");
        q->RotateFlip(Rotate90FlipNone);r=Decode(*q);Check(r.items.size()==1&&r.items[0].url==quark,"rotated QR");Save(*q,path/L"rotated.png");
        auto prose=Make(L"百度网盘分享\n"+baidu+L"\n提取码：aB12");r=Decode(*prose);Check(r.items.size()==1&&r.items[0].url==baidu&&r.items[0].text.find(L"提取码")!=std::wstring::npos,"Chinese text and code");Save(*prose,path/L"chinese.png");
        auto plain=Make(L"这是一段普通文字，没有链接");r=Decode(*plain);Check(r.items.size()==1&&r.items[0].url.empty(),"plain text no auto open");Save(*plain,path/L"text.png");
        auto invalid=Make(L"javascript:alert(1)");r=Decode(*invalid);Check(r.items.size()==1&&r.items[0].url.empty(),"QR unsafe scheme not opened");
        Bitmap multi(720,370,PixelFormat32bppRGB);{Graphics g(&multi);g.Clear(Color(255,255,255));g.DrawImage(b.get(),20,25);g.DrawImage(q.get(),380,25);}r=Decode(multi);Check(r.items.size()==2,"two QR codes");Save(multi,path/L"multiple.png");
        Bitmap blank(400,300,PixelFormat32bppRGB);{Graphics g(&blank);g.Clear(Color(255,255,255));}r=Decode(blank);Check(r.items.empty()&&!r.error.empty(),"blank image");Save(blank,path/L"blank.png");
        Bitmap inverted(320,320,PixelFormat32bppRGB);for(UINT y=0;y<b->GetHeight();++y)for(UINT x=0;x<b->GetWidth();++x){Color c;b->GetPixel(x,y,&c);inverted.SetPixel(x,y,Color(255-c.GetR(),255-c.GetG(),255-c.GetB()));}
        r=Decode(inverted);Check(r.items.size()==1&&r.items[0].url==baidu,"inverted QR");Save(inverted,path/L"inverted.png");
        auto safe=Make(L"https://example.com/?quickqr-test=1#local-check");Save(*safe,path/L"browser-test.png");
        r=DecodeFile((path/L"baidu.png").wstring());Check(r.items.size()==1&&r.items[0].url==baidu,"file decode");
        r=DecodeFile((path/L"missing.png").wstring());Check(r.items.empty()&&!r.error.empty(),"missing file");
        Bitmap transparent(360,360,PixelFormat32bppARGB);{Graphics g(&transparent);g.Clear(Color(0,0,0,0));g.DrawImage(b.get(),20,20);}r=Decode(transparent);Check(r.items.size()==1&&r.items[0].url==baidu,"transparent image");
        std::cout<<"PASS "<<checks<<" checks\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";result=1;}
    GdiplusShutdown(token);return result;
}
