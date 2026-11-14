#include "../main.cpp"
#include <fstream>
#include <filesystem>
#include <psapi.h>
#include <stdexcept>
std::ofstream report;
int uiChecks=0;
void Verify(bool value,const char* message){if(!value)throw std::runtime_error(message);++uiChecks;report<<"PASS "<<message<<std::endl;}
void Pump(int ms=50){auto until=GetTickCount64()+ms;do{MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<until);}
void Wait(){auto deadline=GetTickCount64()+10000;while(busy&&GetTickCount64()<deadline)Pump();Verify(!busy,"worker completes and UI responds");}
void Memory(const char* stage){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof(p));report<<stage<<" working_set_MB="<<p.WorkingSetSize/1048576.0<<" private_MB="<<p.PrivateUsage/1048576.0<<std::endl;}
void Snapshot(const std::filesystem::path& path){RECT r;GetClientRect(window,&r);HDC dc=GetDC(window),mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(mem,bitmap);PrintWindow(window,mem,PW_CLIENTONLY);SelectObject(mem,old);DeleteDC(mem);ReleaseDC(window,dc);{Bitmap image(bitmap,nullptr);CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};Verify(image.Save(path.c_str(),&png)==Ok,"save rendered UI");}DeleteObject(bitmap);}
std::unique_ptr<Bitmap> fixture;
LRESULT CALLBACK FixtureProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);{Graphics g(dc);g.Clear(Color(255,255,255));g.DrawImage(fixture.get(),20,20,320,320);}EndPaint(h,&ps);return 0;}return DefWindowProcW(h,m,w,l);}
void RunUiChecks(){
    OleInitialize(nullptr);IDataObject* originalClipboard=nullptr;OleGetClipboard(&originalClipboard);
    std::filesystem::path root=std::filesystem::path(QUICKQR_SOURCE_DIR);report.open(root/L"tests/ui-report.txt");
    try{
        autoOpen=false;Pump();Memory("idle");Snapshot(root/L"tests/ui-empty.png");
        ScanFile((root/L"tests/fixtures/baidu.png").wstring());Wait();Verify(items.size()==1&&items[0].url==L"https://pan.baidu.com/s/1QuickQR-test?pwd=aB12","file decode retains password");Verify(IsWindowEnabled(GetDlgItem(window,OPEN)),"open enabled for URL");Snapshot(root/L"tests/ui-result.png");
        Bitmap image((root/L"tests/fixtures/quark.png").c_str());HBITMAP clip{};image.GetHBITMAP(Color(255,255,255),&clip);
        Verify(OpenClipboard(window)!=0,"open test clipboard");EmptyClipboard();Verify(SetClipboardData(CF_BITMAP,clip)!=nullptr,"place test bitmap");CloseClipboard();SendMessageW(window,WM_COMMAND,PASTE,0);Wait();Verify(items.size()==1&&items[0].url==L"https://pan.quark.cn/s/0123456789ab","paste clipboard image");
        SendMessageW(window,WM_COMMAND,COPY,0);Verify(OpenClipboard(window)!=0,"read copied result");auto h=GetClipboardData(CF_UNICODETEXT);auto p=(wchar_t*)GlobalLock(h);bool copied=p&&std::wstring(p)==items[0].text;if(p)GlobalUnlock(h);CloseClipboard();Verify(copied,"copy complete result");
        auto path=(root/L"tests/fixtures/multiple.png").wstring();size_t bytes=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);HGLOBAL drop=GlobalAlloc(GHND,bytes);auto d=(DROPFILES*)GlobalLock(drop);d->pFiles=sizeof(DROPFILES);d->fWide=TRUE;memcpy((BYTE*)d+sizeof(DROPFILES),path.c_str(),(path.size()+1)*sizeof(wchar_t));GlobalUnlock(drop);SendMessageW(window,WM_DROPFILES,(WPARAM)drop,0);Wait();Verify(items.size()==2,"drag and drop image with two codes");
        autoOpen=true;ScanFile((root/L"tests/fixtures/multiple.png").wstring());Wait();Verify(status.find(L"已识别")!=std::wstring::npos,"multiple links do not automatically open");autoOpen=false;
        ScanFile((root/L"tests/fixtures/text.png").wstring());Wait();Verify(items.size()==1&&items[0].url.empty()&&!IsWindowEnabled(GetDlgItem(window,OPEN)),"text result cannot launch URL");
        ScanFile((root/L"tests/fixtures/blank.png").wstring());Wait();Verify(items.empty()&&!IsWindowEnabled(GetDlgItem(window,COPY)),"blank image clears previous result");
        fixture=std::make_unique<Bitmap>((root/L"tests/fixtures/baidu.png").c_str());WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=FixtureProc;wc.lpszClassName=L"QuickQR.TestFixture";RegisterClassW(&wc);
        HWND fixtureWindow=CreateWindowExW(WS_EX_TOPMOST,L"QuickQR.TestFixture",L"QuickQR test fixture",WS_POPUP|WS_VISIBLE,100,100,360,360,nullptr,nullptr,instance,nullptr);UpdateWindow(fixtureWindow);Pump();Capture();Pump(350);Verify(overlay&&screenshot,"desktop capture overlay appears");
        SendMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100-screenX,100-screenY));SendMessageW(overlay,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(460-screenX,460-screenY));SendMessageW(overlay,WM_LBUTTONUP,0,MAKELPARAM(460-screenX,460-screenY));Wait();Verify(items.size()==1&&items[0].url.find(L"pan.baidu.com")!=std::wstring::npos,"actual desktop region decodes QR");Verify(!overlay&&!screenshot,"capture pixels released");DestroyWindow(fixtureWindow);fixture.reset();
        Verify(hotkey,"global shortcut registered");SendMessageW(window,WM_HOTKEY,1,0);Pump(350);Verify(overlay!=nullptr,"hotkey message opens capture");SendMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);Verify(!overlay&&!screenshot&&IsWindowVisible(window),"Esc cancels capture and restores window");
        SendMessageW(window,WM_COMMAND,HIDE,0);Verify(trayAdded&&!IsWindowVisible(window),"explicit tray mode hides window");SendMessageW(window,RESTORE,0,0);Verify(IsWindowVisible(window),"tray restore shows window");
        autoOpen=true;ScanFile((root/L"tests/fixtures/browser-test.png").wstring());Wait();Verify(status.find(L"已交给默认浏览器")!=std::wstring::npos,"auto-open hands HTTPS URL to Windows default browser");
        Memory("after_workflows");report<<"SUCCESS "<<uiChecks<<" checks"<<std::endl;
    }catch(const std::exception& e){report<<"FAIL "<<e.what()<<std::endl;}
    if(originalClipboard){OleSetClipboard(originalClipboard);OleFlushClipboard();originalClipboard->Release();}OleUninitialize();report.close();PostMessageW(window,WM_CLOSE,0,0);
}
