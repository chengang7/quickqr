#include "core.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <thread>
#include <functional>
#include <set>
#include <sstream>
using namespace Gdiplus;
constexpr UINT DONE = WM_APP + 1, TRAY = WM_APP + 2, RESTORE = WM_APP + 3;
constexpr int CAPTURE = 101, PASTE = 102, FILEOPEN = 103, OPEN = 104, COPY = 105, AUTO = 106, HIDE = 107, LIST = 108, DETAIL = 109;
HINSTANCE instance;
HWND window, overlay, list, detail;
HFONT font, smallFont;
HICON appIcon, trayIcon;
float dpiScale = 1;
bool busy = false, autoOpen = true, trayAdded = false, hotkey = false, selecting = false;
std::thread worker;
std::unique_ptr<ScanResult> pending;
std::vector<Item> items;
std::wstring status = L"准备就绪 · 支持百度网盘、夸克、阿里云盘等分享二维码";
std::wstring settingsPath;
std::unique_ptr<Bitmap> screenshot;
POINT startPoint{}, endPoint{};
int screenX, screenY, screenW, screenH;
UINT taskbarCreated;
int S(int x) { return int(x * dpiScale + .5f); }
float FitScale(UINT dpi,HMONITOR monitor) { MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(monitor,&mi);float availableH=(float)(mi.rcWork.bottom-mi.rcWork.top-GetSystemMetricsForDpi(SM_CYCAPTION,dpi)-GetSystemMetricsForDpi(SM_CYFRAME,dpi)*2-24);float availableW=(float)(mi.rcWork.right-mi.rcWork.left-32);return std::min({dpi/96.f,availableH/730.f,availableW/660.f}); }
Color ink(28, 37, 55), muted(112, 123, 141), purple(99, 83, 227);
void Round(Graphics& g, RectF r, float radius, Color color) {
    GraphicsPath p; float d = radius * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.GetRight()-d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight()-d, r.GetBottom()-d, d, d, 0, 90); p.AddArc(r.X, r.GetBottom()-d, d, d, 90, 90); p.CloseFigure();
    SolidBrush b(color); g.FillPath(&b, &p);
}
void Text(Graphics& g, const std::wstring& t, float x, float y, float w, float h, float size, Color color, bool bold = false, bool center = false) {
    Font f(L"Microsoft YaHei UI", size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat fmt; fmt.SetLineAlignment(StringAlignmentCenter); if (center) fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter); SolidBrush b(color); g.DrawString(t.c_str(), -1, &f, RectF(x,y,w,h), &fmt, &b);
}
void SetStatus(const std::wstring& s) { status=s; InvalidateRect(window,nullptr,FALSE); }
void ShowMain() { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
void SaveSettings() { WritePrivateProfileStringW(L"Settings",L"AutoOpen",autoOpen?L"1":L"0",settingsPath.c_str()); }
void AddTray() {
    NOTIFYICONDATAW n{sizeof(n)}; n.hWnd=window; n.uID=1; n.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    n.uCallbackMessage=TRAY; n.hIcon=trayIcon; wcscpy_s(n.szTip,L"轻扫 QuickQR · Alt + Shift + Q 截图扫码");
    trayAdded = Shell_NotifyIconW(NIM_ADD,&n)!=FALSE;
}
void RemoveTray() { if(trayAdded) { NOTIFYICONDATAW n{sizeof(n)};n.hWnd=window;n.uID=1;Shell_NotifyIconW(NIM_DELETE,&n);trayAdded=false; } }
int Selected() { int i=(int)SendMessageW(list,LB_GETCURSEL,0,0); return i>=0&&i<(int)items.size()?i:-1; }
void UpdateSelected() {
    int i=Selected(); SetWindowTextW(detail,i>=0?items[i].text.c_str():L""); ShowWindow(list,items.empty()?SW_HIDE:SW_SHOW); ShowWindow(detail,items.empty()?SW_HIDE:SW_SHOW);
    EnableWindow(GetDlgItem(window,OPEN),!busy&&i>=0&&!items[i].url.empty());
    EnableWindow(GetDlgItem(window,COPY),!busy&&i>=0);
}
void OpenSelected() {
    int i=Selected(); if(i<0||!IsWebLink(items[i].url)) return;
    auto rc=(INT_PTR)ShellExecuteW(window,L"open",items[i].url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    SetStatus(rc>32?L"已交给默认浏览器打开 · 提取码请查看下方原文":L"打开失败，请设置默认浏览器，或复制识别内容。");
}
void CopySelected() {
    int i=Selected();if(i<0) return; auto& t=items[i].text;
    HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,(t.size()+1)*sizeof(wchar_t)); if(!h)return;
    void* p=GlobalLock(h);if(!p){GlobalFree(h);return;}memcpy(p,t.c_str(),(t.size()+1)*sizeof(wchar_t));GlobalUnlock(h);
    if(!OpenClipboard(window)){GlobalFree(h);SetStatus(L"剪贴板被占用，请再试一次。");return;}
    EmptyClipboard(); if(!SetClipboardData(CF_UNICODETEXT,h))GlobalFree(h); CloseClipboard();SetStatus(L"已复制完整识别内容，包括链接和文字提取码。");
}
void StartWork(std::function<ScanResult()> job) {
    if(busy)return;
    if(worker.joinable())worker.join(); busy=true;
    items.clear();SendMessageW(list,LB_RESETCONTENT,0,0);UpdateSelected();
    for(int id : {CAPTURE,PASTE,FILEOPEN,OPEN,COPY,AUTO})EnableWindow(GetDlgItem(window,id),FALSE);
    SetStatus(L"正在本地识别…");
    worker=std::thread([job=std::move(job)] {
        try { pending=std::make_unique<ScanResult>(job()); }
        catch(...) { pending=std::make_unique<ScanResult>(ScanResult{{},L"读取失败，图片可能过大或格式不受支持。"}); }
        PostMessageW(window,DONE,0,0);
    });
}
void ScanBitmap(std::unique_ptr<Bitmap> b) {
    if(!b||b->GetLastStatus()!=Ok){SetStatus(L"无法读取图片。");return;}
    auto shared=std::shared_ptr<Bitmap>(std::move(b));StartWork([shared]{return Decode(*shared);});
}
void ScanFile(const std::wstring& path) { StartWork([path]{return DecodeFile(path);}); }
void FileDialog() {
    if(busy)return;
    wchar_t path[32768]{};OPENFILENAMEW f{sizeof(f)}; f.hwndOwner=window;f.lpstrFilter=L"图片（PNG / JPG / BMP / GIF / TIFF）\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0所有文件\0*.*\0";
    f.lpstrFile=path;f.nMaxFile=32768;f.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;f.lpstrTitle=L"选择包含二维码的图片";
    if(GetOpenFileNameW(&f))ScanFile(path);
}
void Paste() {
    if(busy)return;
    if(!OpenClipboard(window)){SetStatus(L"剪贴板暂时被占用，请再试一次。");return;}
    std::unique_ptr<Bitmap> b; std::wstring path;
    // Prefer PNG clipboard data so transparent browser images keep their pixels.
    UINT png=RegisterClipboardFormatW(L"PNG");
    if(auto h=GetClipboardData(png)) {
        SIZE_T size=GlobalSize(h);void* p=GlobalLock(h);
        if(p&&size&&size<100*1024*1024) {
            HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,size);void* dst=copy?GlobalLock(copy):nullptr;
            if(dst) { memcpy(dst,p,size);GlobalUnlock(copy);IStream* stream=nullptr;
                if(SUCCEEDED(CreateStreamOnHGlobal(copy,TRUE,&stream))) { { Bitmap source(stream); if(source.GetLastStatus()==Ok)b.reset(source.Clone(0,0,source.GetWidth(),source.GetHeight(),PixelFormat32bppARGB)); } stream->Release(); }
                else GlobalFree(copy);
            } else if(copy)GlobalFree(copy);
        }
        if(p)GlobalUnlock(h);
    }
    if(!b)if(auto h=GetClipboardData(CF_BITMAP)) {
        Bitmap source((HBITMAP)h,nullptr);
        if(source.GetLastStatus()==Ok)b.reset(source.Clone(0,0,source.GetWidth(),source.GetHeight(),PixelFormat32bppRGB));
    }
    if(!b)if(auto h=GetClipboardData(CF_HDROP)) { wchar_t p[32768]{};DragQueryFileW((HDROP)h,0,p,32768);path=p; }
    CloseClipboard();
    if(b)ScanBitmap(std::move(b));else if(!path.empty())ScanFile(path);else SetStatus(L"剪贴板没有图片。先复制二维码图片，或使用截图识别。");
}
void CancelCapture() {
    selecting=false; if(GetCapture()==overlay)ReleaseCapture();
    if(overlay){DestroyWindow(overlay);overlay=nullptr;}screenshot.reset();ShowMain();
}
RECT Selection() {return {std::min(startPoint.x,endPoint.x),std::min(startPoint.y,endPoint.y),std::max(startPoint.x,endPoint.x),std::max(startPoint.y,endPoint.y)};}
LRESULT CALLBACK OverlayProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);HDC mem=CreateCompatibleDC(dc);HBITMAP back=CreateCompatibleBitmap(dc,screenW,screenH);auto old=SelectObject(mem,back);
        {Graphics g(mem);if(screenshot)g.DrawImage(screenshot.get(),0,0,screenW,screenH);
        SolidBrush shade(Color(125,15,20,35));g.FillRectangle(&shade,0,0,screenW,screenH);
        if(selecting){RECT r=Selection();if(r.right>r.left&&r.bottom>r.top){g.DrawImage(screenshot.get(),Rect(r.left,r.top,r.right-r.left,r.bottom-r.top),r.left,r.top,r.right-r.left,r.bottom-r.top,UnitPixel);Pen p(Color(170,158,255),2);g.DrawRectangle(&p,Rect(r.left,r.top,r.right-r.left,r.bottom-r.top));}}
        POINT c{};GetCursorPos(&c);HMONITOR monitor=MonitorFromPoint(c,MONITOR_DEFAULTTOPRIMARY);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
        int x=info.rcMonitor.left-screenX+(info.rcMonitor.right-info.rcMonitor.left-S(450))/2,y=info.rcMonitor.top-screenY+S(36);
        Round(g,RectF((float)x,(float)y,(float)S(450),(float)S(48)),(float)S(12),Color(245,30,36,53));
        Text(g,L"拖动框选二维码  ·  松开识别  ·  Esc / 右键取消",(float)x,(float)y,(float)S(450),(float)S(48),(float)S(14),Color(255,255,255),false,true);}
        BitBlt(dc,0,0,screenW,screenH,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(back);DeleteDC(mem);EndPaint(h,&ps);return 0;}
    case WM_LBUTTONDOWN:startPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};endPoint=startPoint;selecting=true;SetCapture(h);InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_MOUSEMOVE:if(selecting){endPoint={std::clamp(GET_X_LPARAM(l),0,screenW),std::clamp(GET_Y_LPARAM(l),0,screenH)};InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_LBUTTONUP:if(selecting){endPoint={std::clamp(GET_X_LPARAM(l),0,screenW),std::clamp(GET_Y_LPARAM(l),0,screenH)};RECT r=Selection();selecting=false;ReleaseCapture();
        std::unique_ptr<Bitmap> crop;if(r.right-r.left>=12&&r.bottom-r.top>=12)crop.reset(screenshot->Clone(Rect(r.left,r.top,r.right-r.left,r.bottom-r.top),PixelFormat32bppRGB));
        DestroyWindow(h);overlay=nullptr;screenshot.reset();ShowMain();if(crop)ScanBitmap(std::move(crop));else SetStatus(L"选区太小，请重新框选完整二维码。");}return 0;
    case WM_KEYDOWN:if(w==VK_ESCAPE)CancelCapture();return 0;
    case WM_RBUTTONDOWN:CancelCapture();return 0;
    case WM_DISPLAYCHANGE:PostMessageW(h,WM_KEYDOWN,VK_ESCAPE,0);return 0;
    case WM_CAPTURECHANGED:if(selecting){selecting=false;PostMessageW(h,WM_KEYDOWN,VK_ESCAPE,0);}return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
void Capture() {
    if(busy||overlay)return;ShowWindow(window,SW_HIDE);SetTimer(window,1,180,nullptr);
}
void CaptureNow() {
    screenX=GetSystemMetrics(SM_XVIRTUALSCREEN);screenY=GetSystemMetrics(SM_YVIRTUALSCREEN);screenW=GetSystemMetrics(SM_CXVIRTUALSCREEN);screenH=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(screenW<=0||screenH<=0||uint64_t(screenW)*screenH>80000000){ShowMain();SetStatus(L"桌面尺寸过大，请改用粘贴图片识别。");return;}
    HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,screenW,screenH);auto old=SelectObject(mem,bitmap);
    BOOL ok=BitBlt(mem,0,0,screenW,screenH,screen,screenX,screenY,SRCCOPY|CAPTUREBLT);
    SelectObject(mem,old);DeleteDC(mem);ReleaseDC(nullptr,screen);
    if(ok&&bitmap)screenshot=std::make_unique<Bitmap>(bitmap,nullptr);if(bitmap)DeleteObject(bitmap);
    if(!screenshot||screenshot->GetLastStatus()!=Ok){screenshot.reset();ShowMain();SetStatus(L"无法截取桌面，请改用粘贴图片。");return;}
    overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"QuickQR.Overlay",L"轻扫 · 框选二维码",WS_POPUP,screenX,screenY,screenW,screenH,nullptr,nullptr,instance,nullptr);
    if(!overlay){screenshot.reset();ShowMain();return;}ShowWindow(overlay,SW_SHOW);SetForegroundWindow(overlay);SetFocus(overlay);
}
void Layout() {
    if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);
    font=CreateFontW(-S(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    smallFont=CreateFontW(-S(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto place=[](int id,int x,int y,int w,int h){HWND c=GetDlgItem(window,id);SetWindowPos(c,nullptr,S(x),S(y),S(w),S(h),SWP_NOZORDER);SendMessageW(c,WM_SETFONT,(WPARAM)(id==DETAIL?smallFont:font),TRUE);};
    place(CAPTURE,48,180,564,52);place(PASTE,48,244,276,42);place(FILEOPEN,336,244,276,42);
    place(AUTO,32,320,400,28);place(LIST,48,402,564,65);place(DETAIL,48,478,564,76);
    place(OPEN,48,570,170,38);place(COPY,230,570,170,38);place(HIDE,500,35,128,32);
    SendMessageW(list,LB_SETITEMHEIGHT,0,S(29));
}
void PaintMain(HDC dc) {
    RECT rc;GetClientRect(window,&rc);HDC mem=CreateCompatibleDC(dc);HBITMAP back=CreateCompatibleBitmap(dc,rc.right,rc.bottom);auto old=SelectObject(mem,back);
    {Graphics g(mem);g.ScaleTransform(dpiScale,dpiScale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.Clear(Color(246,247,251));
    { HDC iconDC=g.GetHDC();DrawIconEx(iconDC,S(32),S(30),appIcon,S(48),S(48),0,nullptr,DI_NORMAL);g.ReleaseHDC(iconDC); }
    Text(g,L"轻扫",94,27,100,31,24,ink,true);Text(g,L"QuickQR · 轻一点，快一步",94,58,280,22,12,muted);
    Round(g,RectF(32,108,596,198),18,Color(255,255,255));
    Text(g,L"框一下，打开分享",48,122,400,32,21,ink,true);
    Text(g,hotkey?L"全局快捷键  Alt + Shift + Q":L"快捷键被占用 · 请点击下方按钮截图",48,152,500,23,12,muted);
    Round(g,RectF(32,366,596,262),18,Color(255,255,255));
    Text(g,L"识别结果",48,375,240,25,15,ink,true);
    if(items.empty()&&!busy)Text(g,L"识别后在这里查看链接和完整内容",48,482,564,65,14,muted,false,true);
    Text(g,L"支持粘贴与拖入图片  ·  图片仅在本机处理",32,643,596,23,12,muted);
    Text(g,status,32,674,596,40,12,busy?purple:muted);
    }
    BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(back);DeleteDC(mem);
}
void DrawButton(const DRAWITEMSTRUCT* d) {
    RECT rc=d->rcItem;Graphics g(d->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    bool enabled=!(d->itemState&ODS_DISABLED),pressed=d->itemState&ODS_SELECTED;
    int id=d->CtlID;bool primary=id==CAPTURE||id==OPEN;
    SolidBrush base(id==HIDE||id==AUTO?Color(246,247,251):Color(255,255,255));g.FillRectangle(&base,Rect(rc.left,rc.top,rc.right,rc.bottom));
    Color bg=primary?purple:Color(242,242,250);if(pressed)bg=primary?Color(79,62,201):Color(225,225,240);if(!enabled)bg=Color(238,239,245);
    wchar_t title[160]{};GetWindowTextW(d->hwndItem,title,160);
    if(id==AUTO){Round(g,RectF((float)S(1),(float)S(5),(float)S(32),(float)S(18)),(float)S(9),autoOpen?purple:Color(189,195,207));SolidBrush white(Color(255,255,255));g.FillEllipse(&white,(float)S(autoOpen?17:4),(float)S(8),(float)S(12),(float)S(12));Text(g,title,(float)S(44),0,(float)(rc.right-S(44)),(float)rc.bottom,(float)S(13),ink);}
    else {Round(g,RectF(1,1,(float)rc.right-2,(float)rc.bottom-2),(float)S(10),bg);Text(g,title,0,0,(float)rc.right,(float)rc.bottom,(float)S(id==CAPTURE?16:13),enabled?(primary?Color(255,255,255):ink):Color(164,170,182),id==CAPTURE,true);}
    if(d->itemState&ODS_FOCUS){RECT f=rc;InflateRect(&f,-S(4),-S(4));DrawFocusRect(d->hDC,&f);}
}
LRESULT CALLBACK MainProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==taskbarCreated){trayAdded=false;if(!IsWindowVisible(h))AddTray();return 0;}
    switch(m) {
    case WM_CREATE: {
        window=h;dpiScale=FitScale(GetDpiForWindow(h),MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST));
        for(auto [id,title] : {std::pair{CAPTURE,L"截图识别"}, {PASTE,L"粘贴图片   Ctrl + V"},{FILEOPEN,L"选择图片"},{OPEN,L"打开链接"},{COPY,L"复制完整内容"},{AUTO,L"识别到单个网页链接后，自动打开"},{HIDE,L"收起到托盘"}})
            CreateWindowW(L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)(INT_PTR)id,instance,nullptr);
        list=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL,0,0,0,0,h,(HMENU)(INT_PTR)LIST,instance,nullptr);
        detail=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,h,(HMENU)(INT_PTR)DETAIL,instance,nullptr);
        SendMessageW(detail,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(S(8),S(8)));
        DragAcceptFiles(h,TRUE);hotkey=RegisterHotKey(h,1,MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,'Q')!=FALSE;
        Layout();UpdateSelected();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;auto dc=BeginPaint(h,&p);PaintMain(dc);EndPaint(h,&p);return 0;}
    case WM_DRAWITEM:{auto d=(DRAWITEMSTRUCT*)l;if(d->CtlID==LIST){Graphics g(d->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);RECT r=d->rcItem;SolidBrush white(Color(255,255,255));g.FillRectangle(&white,Rect(r.left,r.top,r.right-r.left,r.bottom-r.top));if(d->itemID<items.size()){bool selected=d->itemState&ODS_SELECTED;Round(g,RectF((float)r.left,(float)r.top+1,(float)(r.right-r.left),(float)(r.bottom-r.top)-2),(float)S(6),selected?Color(240,237,255):Color(255,255,255));auto& item=items[d->itemID];std::wstring label=std::to_wstring(d->itemID+1)+L".  "+(item.url.empty()?L"文本二维码":item.url);Text(g,label,(float)(r.left+S(8)),(float)r.top,(float)(r.right-r.left-S(16)),(float)(r.bottom-r.top),(float)S(13),selected?purple:ink);}}else DrawButton(d);return TRUE;}
    case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT: {
        HDC dc=(HDC)w;SetTextColor(dc,RGB(44,53,71));SetBkColor(dc,RGB(255,255,255));return (LRESULT)GetStockObject(WHITE_BRUSH);}
    case WM_COMMAND: {
        int id=LOWORD(w);if(id==LIST&&HIWORD(w)==LBN_SELCHANGE){UpdateSelected();return 0;}if(id==LIST&&HIWORD(w)==LBN_DBLCLK){OpenSelected();return 0;}
        switch(id){case CAPTURE:Capture();break;case PASTE:Paste();break;case FILEOPEN:FileDialog();break;case OPEN:OpenSelected();break;case COPY:CopySelected();break;
        case AUTO:autoOpen=!autoOpen;SaveSettings();InvalidateRect(GetDlgItem(h,AUTO),nullptr,FALSE);break;
        case HIDE:if(!trayAdded)AddTray();if(trayAdded)ShowWindow(h,SW_HIDE);else SetStatus(L"系统托盘暂时不可用，窗口保持打开。");break;}
        return 0;}
    case WM_HOTKEY:if(w==1)Capture();return 0;
    case WM_TIMER:if(w==1){KillTimer(h,1);CaptureNow();}return 0;
    case WM_DROPFILES: {wchar_t path[32768]{};UINT n=DragQueryFileW((HDROP)w,0xffffffff,nullptr,0);DragQueryFileW((HDROP)w,0,path,32768);DragFinish((HDROP)w);ShowMain();if(!busy){ScanFile(path);if(n>1)SetStatus(L"正在识别第一张图片 · 请一次拖入一张图片");}return 0;}
    case DONE: {
        if(worker.joinable())worker.join();busy=false;
        for(int id:{CAPTURE,PASTE,FILEOPEN,AUTO})EnableWindow(GetDlgItem(h,id),TRUE);
        if(pending){items=std::move(pending->items);auto error=pending->error;pending.reset();SendMessageW(list,LB_RESETCONTENT,0,0);
            for(size_t i=0;i<items.size();++i){std::wstring label=std::to_wstring(i+1)+L".  "+(items[i].url.empty()?L"文本二维码":items[i].url);SendMessageW(list,LB_ADDSTRING,0,(LPARAM)label.c_str());}
            if(!items.empty())SendMessageW(list,LB_SETCURSEL,0,0);UpdateSelected();
            SetStatus(error.empty()?L"已识别 "+std::to_wstring(items.size())+L" 项 · 可选择链接打开，或复制完整内容":error);
            if(autoOpen&&items.size()==1&&!items[0].url.empty())OpenSelected();
        }return 0;}
    case TRAY:if(l==WM_LBUTTONUP||l==WM_LBUTTONDBLCLK)ShowMain();else if(l==WM_RBUTTONUP){POINT p;GetCursorPos(&p);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"截图识别");AppendMenuW(menu,MF_STRING,2,L"显示主窗口");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,L"退出轻扫");SetForegroundWindow(h);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);PostMessageW(h,WM_NULL,0,0);if(cmd==1)Capture();if(cmd==2)ShowMain();if(cmd==3)SendMessageW(h,WM_CLOSE,0,0);}return 0;
    case RESTORE:ShowMain();return 0;
    case WM_DPICHANGED: {RECT* r=(RECT*)l;dpiScale=FitScale(HIWORD(w),MonitorFromRect(r,MONITOR_DEFAULTTONEAREST));RECT fit{0,0,S(660),S(730)};AdjustWindowRectExForDpi(&fit,(DWORD)GetWindowLongPtrW(h,GWL_STYLE),FALSE,0,HIWORD(w));SetWindowPos(h,nullptr,r->left,r->top,fit.right-fit.left,fit.bottom-fit.top,SWP_NOZORDER|SWP_NOACTIVATE);Layout();InvalidateRect(h,nullptr,TRUE);return 0;}
    case WM_CLOSE:if(overlay){DestroyWindow(overlay);overlay=nullptr;screenshot.reset();}KillTimer(h,1);DestroyWindow(h);return 0;
    case WM_DESTROY:if(worker.joinable())worker.join();pending.reset();RemoveTray();if(hotkey)UnregisterHotKey(h,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
#ifdef QUICKQR_TEST_BUILD
void RunUiChecks();
#endif
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int) {
    instance=inst;HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\QuickQR.SingleInstance.v1");
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto other=FindWindowW(L"QuickQR.Main",nullptr))PostMessageW(other,RESTORE,0,0);if(mutex)CloseHandle(mutex);return 0;}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);GdiplusStartupInput gi;ULONG_PTR token;GdiplusStartup(&token,&gi,nullptr);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    PWSTR local=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){settingsPath=std::wstring(local)+L"\\QuickQR";CreateDirectoryW(settingsPath.c_str(),nullptr);settingsPath+=L"\\settings.ini";CoTaskMemFree(local);}
    autoOpen=GetPrivateProfileIntW(L"Settings",L"AutoOpen",1,settingsPath.c_str())!=0;
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    appIcon=(HICON)LoadImageW(inst,MAKEINTRESOURCEW(1),IMAGE_ICON,256,256,0);trayIcon=(HICON)LoadImageW(inst,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0);
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=inst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=appIcon;wc.hIconSm=trayIcon;wc.lpfnWndProc=MainProc;wc.lpszClassName=L"QuickQR.Main";RegisterClassExW(&wc);
    wc.lpfnWndProc=OverlayProc;wc.lpszClassName=L"QuickQR.Overlay";wc.hCursor=LoadCursor(nullptr,IDC_CROSS);RegisterClassExW(&wc);
    UINT dpi=GetDpiForSystem();dpiScale=FitScale(dpi,MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY));RECT r{0,0,S(660),S(730)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&r,style,FALSE,0,dpi);
    window=CreateWindowExW(0,L"QuickQR.Main",L"轻扫 QuickQR",style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,inst,nullptr);
    if(!window){GdiplusShutdown(token);CoUninitialize();if(mutex)CloseHandle(mutex);return 1;}
    DWORD corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc>1&&argv[1][0]!=L'-')ScanFile(argv[1]);LocalFree(argv);
#ifdef QUICKQR_TEST_BUILD
    RunUiChecks();
#endif
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){
        if(!overlay&&msg.message==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&msg.wParam=='V'){Paste();continue;}
        if(!overlay&&IsDialogMessageW(window,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);if(appIcon)DestroyIcon(appIcon);if(trayIcon)DestroyIcon(trayIcon);GdiplusShutdown(token);CoUninitialize();if(mutex)CloseHandle(mutex);return 0;
}
