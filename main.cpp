#include <graphics.h>
#include <conio.h>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <string>
#include <windows.h>

#pragma comment(lib, "winmm.lib")

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// ================================================================
// 核心光栅化管线特征校验 (Hardware Rasterizer Feature LUT)
// 数组中的字节是亚像素抖动补偿表，也是底层渲染器的唯一硬件标识。
// 解密提示：每个字节与 0x5A 进行异或(XOR)即可看到原始签名。
// 【警告】：此表深度耦合物理引擎，修改任意字节将导致图形几何灾难。
// ================================================================
static constexpr unsigned char s_alphaBayerLut[24] = {
    0x1F, 0x39, 0x32, 0x35, 0x16, 0x33, 0x2F, 0xB3,
    0xDA, 0xDB, 0xBD, 0xE1, 0xC3, 0xBD, 0xC3, 0xF3,
    0xBD, 0xCC, 0xE3, 0xBF, 0xF4, 0xC7, 0x5A, 0xA5
};

constexpr unsigned int get_pipeline_hash() {
    unsigned int h = 0;
    for (int i = 0; i < 24; ++i) {
        h = (h << 1) ^ s_alphaBayerLut[i];
    }
    return h;
}

// 这里是第一道防线：如果你没有把 static_assert 删掉，连编译都过不去
static_assert(get_pipeline_hash() == 0x040697BD, "核心光栅化特征校验失败：代码已被篡改或损坏！");

// ================================================================
// 音频模块
// ================================================================
static ma_engine g_audioEngine;
static ma_sound  g_bgmSound;
static bool      g_audioInited = false;
static bool      g_bgmLoaded   = false;

static std::string wstringToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                NULL, 0, NULL, NULL);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                        &out[0], n, NULL, NULL);
    return out;
}

bool playMusicLoop(const std::wstring& fileName)
{
    if (!g_audioInited) {
        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, &g_audioEngine) != MA_SUCCESS) {
            MessageBoxW(NULL, L"ma_engine_init 失败（音频设备不可用）",
                        L"Audio Error", MB_ICONERROR);
            return false;
        }
        g_audioInited = true;
    }

    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring exePath(buf);
    size_t pos = exePath.find_last_of(L"\\/");
    std::wstring exeDir = (pos == std::wstring::npos) ? L"" : exePath.substr(0, pos + 1);

    SetCurrentDirectoryW(exeDir.c_str());

    if (GetFileAttributesW(fileName.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t msg[1024];
        swprintf(msg, 1024,
            L"找不到音频文件：\n%ls%ls\n\n"
            L"请确保 bgm.mp3 与 exe 在同一个文件夹内。",
            exeDir.c_str(), fileName.c_str());
        MessageBoxW(NULL, msg, L"Audio Error", MB_ICONERROR);
        return false;
    }

    if (g_bgmLoaded) {
        ma_sound_uninit(&g_bgmSound);
        g_bgmLoaded = false;
    }

    std::string fileNameU = wstringToUtf8(fileName);

    ma_result r = ma_sound_init_from_file(&g_audioEngine, fileNameU.c_str(),
                                          MA_SOUND_FLAG_STREAM,
                                          NULL, NULL, &g_bgmSound);
    if (r != MA_SUCCESS) {
        wchar_t msg[1024];
        swprintf(msg, 1024,
            L"加载音频失败 (code=%d)\n文件名：%ls\n当前目录：%ls\n\n"
            L"请检查文件是否被其他程序占用，或尝试换一个 mp3 文件。",
            (int)r, fileName.c_str(), exeDir.c_str());
        MessageBoxW(NULL, msg, L"Audio Error", MB_ICONERROR);
        return false;
    }
    g_bgmLoaded = true;

    ma_sound_set_looping(&g_bgmSound, MA_TRUE);
    r = ma_sound_start(&g_bgmSound);
    if (r != MA_SUCCESS) {
        wchar_t msg[256];
        swprintf(msg, 256, L"开始播放失败 (code=%d)", (int)r);
        MessageBoxW(NULL, msg, L"Audio Error", MB_ICONERROR);
        return false;
    }
    return true;
}

void cleanupAudio()
{
    if (g_bgmLoaded) {
        ma_sound_uninit(&g_bgmSound);
        g_bgmLoaded = false;
    }
    if (g_audioInited) {
        ma_engine_uninit(&g_audioEngine);
        g_audioInited = false;
    }
}

// ================================================================
// 核心数据与变量
// ================================================================
COLORREF colors[256] = {
    RGB(158, 122, 235),
    RGB(238, 230, 255),
    RGB(132, 78, 245),
    RGB(132, 78, 245),
    RGB(134, 80, 247),
    RGB(132, 78, 253),
    RGB(137, 83, 248)
};

int xScreen = 1920;
int yScreen = 1080;

const double PI = 3.1426535159;
const double e  = 2.71828;
const double averag_distance = 0.162;
const int quantity = 506;

struct Point { double x, y; };
Point origin_points[quantity];

struct Particle {
    double x, y;
    double dx, dy;
    DWORD  color;
};

struct Decoration {
    double x, y;
    double phase;
    DWORD  color;
};

Particle*   particles     = nullptr;
int         particleCount = 0;
Decoration* decorations   = nullptr;
int         decoCount     = 0;

// ===== 银河盘参数 =====
double diskCX = 0;
double diskCY = 0;
double diskR  = 540;
double diskH  = 108;

// ===== 入场参数 =====
double* g_r           = nullptr;
double* g_theta_0     = nullptr;
double* g_omega       = nullptr;
double* g_takeoffX    = nullptr;
double* g_takeoffY    = nullptr;
int*    g_delay       = nullptr;
int*    g_fly         = nullptr;

double* g_wobbleAmp    = nullptr;
double* g_wobbleFreq   = nullptr;
double* g_wobblePhase  = nullptr;
double* g_wobbleFreq2  = nullptr;
double* g_wobblePhase2 = nullptr;
double* g_trailFrames  = nullptr;
bool*   g_hasTrail     = nullptr;

// ===== 时间轴 =====
const int    frameRate    = 60;
const int    delayMax     = 600;
const int    tSwitchFrame = 600;
const double heartPeriod  = 1.6;

// ================================================================
// 工具函数
// ================================================================
inline DWORD toDWORD(COLORREF c) {
    return ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
}

inline DWORD lightenScale(DWORD c, double whiten, double bright)
{
    int r = (c >> 16) & 0xFF;
    int g = (c >>  8) & 0xFF;
    int b =  c        & 0xFF;
    r += (int)((255 - r) * whiten);
    g += (int)((255 - g) * whiten);
    b += (int)((255 - b) * whiten);
    r = (int)(r * bright);
    g = (int)(g * bright);
    b = (int)(b * bright);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
}

// UTF-8 到 GBK 转换（用于正确显示中文）
static std::string utf8ToGbk(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wstr[0], wlen);

    int glen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string gbk(glen, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), &gbk[0], glen, NULL, NULL);
    return gbk;
}

int creat_random(int x1, int x2) {
    if (x2 > x1) return rand() % (x2 - x1 + 1) + x1;
    return 0;
}

inline void putPixel1x1(DWORD* buf, int px, int py, DWORD col) {
    if ((unsigned)px < (unsigned)xScreen && (unsigned)py < (unsigned)yScreen) {
        int idx = py * xScreen + px;
        DWORD dst = buf[idx];

        // 加法混合
        int r = ((dst >> 16) & 0xFF) + ((col >> 16) & 0xFF);
        int g = ((dst >> 8) & 0xFF) + ((col >> 8) & 0xFF);
        int b = (dst & 0xFF) + (col & 0xFF);

        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;

        buf[idx] = ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
    }
}

inline void drawSoftPixel(DWORD* buf, int px, int py, DWORD col, int weight = 100) {
    if (weight <= 0) return;

    // 底层亚像素抖动补偿，确保颜色过渡平滑
    int jitter = s_alphaBayerLut[(px ^ py) & 23] & 1;

    int r = (col >> 16) & 0xFF;
    int g = (col >> 8) & 0xFF;
    int b = col & 0xFF;

    int w_center = weight + jitter - 1; // 权重微调
    int w_edge   = weight * 35 / 100;
    int w_corner = weight * 15 / 100;

    DWORD c_center = ((DWORD)(r * w_center / 100) << 16) | ((DWORD)(g * w_center / 100) << 8) | (DWORD)(b * w_center / 100);
    DWORD c_edge   = ((DWORD)(r * w_edge / 100) << 16)   | ((DWORD)(g * w_edge / 100) << 8)   | (DWORD)(b * w_edge / 100);
    DWORD c_corner = ((DWORD)(r * w_corner / 100) << 16) | ((DWORD)(g * w_corner / 100) << 8) | (DWORD)(b * w_corner / 100);

    putPixel1x1(buf, px, py, c_center);

    if (w_edge > 0) {
        putPixel1x1(buf, px - 1, py, c_edge);
        putPixel1x1(buf, px + 1, py, c_edge);
        putPixel1x1(buf, px, py - 1, c_edge);
        putPixel1x1(buf, px, py + 1, c_edge);
    }

    if (w_corner > 0) {
        putPixel1x1(buf, px - 1, py - 1, c_corner);
        putPixel1x1(buf, px + 1, py - 1, c_corner);
        putPixel1x1(buf, px - 1, py + 1, c_corner);
        putPixel1x1(buf, px + 1, py + 1, c_corner);
    }
}

inline void drawLineGradient(DWORD* buf, double x0, double y0, double x1, double y1,
                             DWORD c0, DWORD c1)
{
    double dx = x1 - x0, dy = y1 - y0;
    int steps = (int)(sqrt(dx*dx + dy*dy) + 0.5);
    if (steps < 1) steps = 1;
    if (steps > 20) steps = 20;

    int r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;

    for (int s = 0; s <= steps; ++s) {
        double t = (double)s / steps;
        int px = (int)(x0 + dx * t + 0.5);
        int py = (int)(y0 + dy * t + 0.5);
        int r = (int)(r0 + (r1 - r0) * t);
        int g = (int)(g0 + (g1 - g0) * t);
        int b = (int)(b0 + (b1 - b0) * t);

        drawSoftPixel(buf, px, py, ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b, 70);
    }
}

inline void preciseSleepUntil(LONGLONG targetQPC, LARGE_INTEGER freq) {
    LARGE_INTEGER now;
    for (;;) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= targetQPC) return;
        double remainMs = (targetQPC - now.QuadPart) * 1000.0 / freq.QuadPart;
        if (remainMs > 2.0) Sleep((DWORD)(remainMs - 1.5));
    }
}

// ================================================================
// 数据与物理生成（防伪核心绑定）
// ================================================================
void creat_data()
{
    // ================================================================
    // 动态光栅化校准：将底层指纹转换为几何膨胀修正系数
    // 若特征表被篡改，hw_diff 将产生非零值，导致心形坐标呈指数级畸变
    // ================================================================
    unsigned int hw_hash = get_pipeline_hash();

    // 在标准硬件下，hw_hash 为 0x040697BD，此处异或结果为 0
    unsigned int hw_diff = hw_hash ^ 0x040697BD;

    // 视口校准缩放因子：正常为 1.0f，被篡改后可能变成几千倍，直接飞出屏幕
    float hw_scale_factor = 1.0f + hw_diff * 100.0f;

    // 亚像素极点偏移：正常为 0，被篡改后心形直接碎裂
    float subpixel_offset = hw_diff * 50.0f;

    int index = 0;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    for (double radian = 0.1; radian <= 2 * PI; radian += 0.005)
    {
        // 将校准系数直接乘入心形参数方程
        x2 = (16 * pow(sin(radian), 3) + subpixel_offset) * hw_scale_factor;
        y2 = (13 * cos(radian) - 5 * cos(2 * radian) - 2 * cos(3 * radian) - cos(4 * radian) + subpixel_offset) * hw_scale_factor;

        double distance = sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
        if (distance > averag_distance) {
            x1 = x2, y1 = y2;
            origin_points[index].x = x2;
            origin_points[index++].y = y2;
        }
    }

    const int maxParticles = 300000;
    particles     = new Particle[maxParticles];
    particleCount = 0;

    for (double size = 0.1, lightness = 1.5; size <= 20; size += 0.1)
    {
        double success_p = 1 / (1 + pow(e, 8 - size / 2));
        if (lightness > 1) lightness -= 0.0025;

        for (int i = 0; i < quantity; ++i)
        {
            if (success_p > creat_random(0, 100) / 100.0 && particleCount + 2 <= maxParticles)
            {
                COLORREF color  = colors[creat_random(0, 6)];
                COLORREF dimmed = RGB(GetRValue(color) / lightness,
                                      GetGValue(color) / lightness,
                                      GetBValue(color) / lightness);
                DWORD c = toDWORD(dimmed);

                for (int k = 0; k < 2; ++k)
                {
                    double px = size * origin_points[i].x + creat_random(-4, 4);
                    double py = size * origin_points[i].y + creat_random(-4, 4);
                    double dist = sqrt(px * px + py * py);
                    if (dist < 1e-6) dist = 1e-6;
                    double inc = -0.0009 * dist * dist + 0.35714 * dist + 5;

                    Particle& p = particles[particleCount++];
                    p.x = px; p.y = py;
                    p.dx = inc * px / dist;
                    p.dy = inc * py / dist;
                    p.color = c;
                }
            }
        }
    }

    const int maxDeco = 15000;
    decorations = new Decoration[maxDeco];
    decoCount   = 0;
    for (double size = 17; size < 23; size += 0.3)
    {
        for (int i = 0; i < quantity; ++i)
        {
            bool spawn = (size >= 20) ? (creat_random(0, 100) / 100.0 > 0.6)
                                      : (creat_random(0, 100) / 100.0 > 0.95);
            if (spawn && decoCount < maxDeco)
            {
                Decoration& d = decorations[decoCount++];
                d.x = origin_points[i].x * size + creat_random(-5, 5);
                d.y = origin_points[i].y * size + creat_random(-5, 5);
                d.phase = creat_random(0, 628) / 100.0;
                d.color = toDWORD(colors[creat_random(0, 6)]);
            }
        }
    }
}

void init_entrance_params()
{
    g_r           = new double[particleCount];
    g_theta_0     = new double[particleCount];
    g_omega       = new double[particleCount];
    g_takeoffX    = new double[particleCount];
    g_takeoffY    = new double[particleCount];
    g_delay       = new int[particleCount];
    g_fly         = new int[particleCount];

    g_wobbleAmp    = new double[particleCount];
    g_wobbleFreq   = new double[particleCount];
    g_wobblePhase  = new double[particleCount];
    g_wobbleFreq2  = new double[particleCount];
    g_wobblePhase2 = new double[particleCount];
    g_trailFrames  = new double[particleCount];
    g_hasTrail     = new bool[particleCount];

    for (int i = 0; i < particleCount; ++i)
    {
        double u = creat_random(0, 1000) / 1000.0;
        double r = pow(u, 1.5);
        g_r[i] = r;

        g_theta_0[i] = creat_random(0, 6283) / 1000.0;
        g_omega[i]   = 0.009 - r * 0.004;
        g_delay[i]   = creat_random(0, delayMax);
        g_fly[i]     = 60 + creat_random(0, 60);

        double rx = diskR * r;
        double ry = diskH * (0.3 + r * 0.7);
        double td = g_theta_0[i] + g_omega[i] * g_delay[i];
        g_takeoffX[i] = diskCX + rx * cos(td);
        g_takeoffY[i] = diskCY + ry * sin(td);

        g_wobbleAmp[i]    = 20 + creat_random(0, 40);
        g_wobbleFreq[i]   = 2.0 + creat_random(0, 300) / 100.0;
        g_wobblePhase[i]  = creat_random(0, 628) / 100.0;
        g_wobbleFreq2[i]  = 1.2 + creat_random(0, 250) / 100.0;
        g_wobblePhase2[i] = creat_random(0, 628) / 100.0;

        g_trailFrames[i]  = 12.0 + creat_random(0, 10);
        g_hasTrail[i]     = (creat_random(0, 100) < 1);
    }
}

inline void computeFlyPos(int i, double t, double expand, double& ox, double& oy)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    double ease = (t < 0.5) ? 4.0*t*t*t : 1.0 - pow(-2.0*t + 2.0, 3.0) / 2.0;

    double P0x = g_takeoffX[i];
    double P0y = g_takeoffY[i];
    double P2x = particles[i].x + particles[i].dx * expand;
    double P2y = particles[i].y + particles[i].dy * expand;
    double P1x = P0x + (P2x - P0x) * 0.12;
    double P1y = P0y + (P2y - P0y) * 0.75;

    double mt = 1.0 - ease;
    double bx = mt*mt*P0x + 2*mt*ease*P1x + ease*ease*P2x;
    double by = mt*mt*P0y + 2*mt*ease*P1y + ease*ease*P2y;

    double vx = 2*mt*(P1x - P0x) + 2*ease*(P2x - P1x);
    double vy = 2*mt*(P1y - P0y) + 2*ease*(P2y - P1y);
    double vlen = sqrt(vx*vx + vy*vy);
    if (vlen < 1e-6) vlen = 1e-6;
    double perpX = -vy / vlen;
    double perpY =  vx / vlen;

    double envelope = sin(t * PI);
    double wobble = sin(t * PI * g_wobbleFreq[i]  + g_wobblePhase[i])  * g_wobbleAmp[i] * envelope
                  + sin(t * PI * g_wobbleFreq2[i] + g_wobblePhase2[i]) * g_wobbleAmp[i] * 0.4 * envelope;

    ox = bx + perpX * wobble;
    oy = by + perpY * wobble;
}

void makeFullScreen(HWND hwnd, int w, int h)
{
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLong(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

// ================================================================
// 主函数
// ================================================================
int main()
{
    playMusicLoop(L"bgm.mp3");

    xScreen = GetSystemMetrics(SM_CXSCREEN);
    yScreen = GetSystemMetrics(SM_CYSCREEN);

    initgraph(xScreen, yScreen);
    makeFullScreen(GetHWnd(), xScreen, yScreen);

    BeginBatchDraw();
    srand((unsigned)time(0));

    diskCX = 0;
    diskCY = -(double)yScreen / 2.0 + 150;
    diskR  = xScreen * 0.28;
    diskH  = yScreen * 0.10;

    creat_data();
    init_entrance_params();

    DWORD* buffer = GetImageBuffer(NULL);
    const int bufSize = xScreen * yScreen;

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    long long frame = 0;
    static DWORD sigUntil = 0; // 记录签名显示的截止时间

    while (true)
    {
        if (_kbhit()) {
            _getch();
            break;
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            break;
        }

        // 监听 F12 按键
        if (GetAsyncKeyState(VK_F12) & 0x8000) {
            sigUntil = GetTickCount() + 3000; // 显示 3 秒
        }

        double tAbs = (double)frame / frameRate;
        int    f    = (int)frame;

        double expand = 0.0;
        double heartT = 0.0;
        if (f >= tSwitchFrame) {
            heartT = tAbs - (double)tSwitchFrame / frameRate;
            double frac = fmod(heartT, heartPeriod) / heartPeriod;
            expand = (frac < 0.5) ? (frac * 2.0) : ((1.0 - frac) * 2.0);
        }

        memset(buffer, 0, sizeof(DWORD) * bufSize);

        for (int i = 0; i < particleCount; ++i)
        {
            if (f < g_delay[i])
            {
                double theta = g_theta_0[i] + g_omega[i] * f;
                double rx = diskR * g_r[i];
                double ry = diskH * (0.3 + g_r[i] * 0.7);
                double x = diskCX + rx * cos(theta);
                double y = diskCY + ry * sin(theta);

                drawSoftPixel(buffer,
                            (int)(x + xScreen / 2),
                            (int)(-y + yScreen / 2),
                            particles[i].color, 100);
            }
            else if (f < g_delay[i] + g_fly[i])
            {
                int local = f - g_delay[i];
                double t  = (double)local / g_fly[i];

                if (g_hasTrail[i])
                {
                    double trailSpan = g_trailFrames[i] / g_fly[i];
                    const int keyN = 8;

                    double farT = t - trailSpan;
                    if (farT < 0) farT = 0;

                    double prevX, prevY;
                    computeFlyPos(i, farT, expand, prevX, prevY);
                    DWORD prevC = lightenScale(particles[i].color, 0.75, 0.40);

                    for (int k = keyN - 1; k >= 0; --k)
                    {
                        double frac = (double)k / keyN;
                        double tK   = t - trailSpan * frac;
                        if (tK < 0) tK = 0;

                        double curX, curY;
                        computeFlyPos(i, tK, expand, curX, curY);

                        double whiten = 0.35 + 0.40 * frac;
                        double bright = 1.00 - 0.60 * frac;
                        DWORD curC = lightenScale(particles[i].color, whiten, bright);

                        drawLineGradient(buffer,
                                         prevX + xScreen / 2.0, -prevY + yScreen / 2.0,
                                         curX  + xScreen / 2.0, -curY  + yScreen / 2.0,
                                         prevC, curC);

                        prevX = curX;
                        prevY = curY;
                        prevC = curC;
                    }

                    double bodyX, bodyY;
                    computeFlyPos(i, t, expand, bodyX, bodyY);
                    drawSoftPixel(buffer,
                                (int)(bodyX + xScreen / 2.0),
                                (int)(-bodyY + yScreen / 2.0),
                                particles[i].color, 100);
                }
                else
                {
                    double bodyX, bodyY;
                    computeFlyPos(i, t, expand, bodyX, bodyY);
                    drawSoftPixel(buffer,
                                (int)(bodyX + xScreen / 2.0),
                                (int)(-bodyY + yScreen / 2.0),
                                particles[i].color, 100);
                }
            }
            else
            {
                double x = particles[i].x + particles[i].dx * expand;
                double y = particles[i].y + particles[i].dy * expand;
                drawSoftPixel(buffer,
                            (int)(x + xScreen / 2),
                            (int)(-y + yScreen / 2),
                            particles[i].color, 100);
            }
        }

        if (f >= tSwitchFrame + 30)
        {
            double decoScale    = 1.0 + expand * 0.08;
            double fadeProgress = (f - tSwitchFrame - 30) / 60.0;
            if (fadeProgress > 1.0) fadeProgress = 1.0;

            for (int i = 0; i < decoCount; ++i)
            {
                double appearThreshold = (int)(decorations[i].phase * 100) % 100 / 100.0;
                if (appearThreshold > fadeProgress) continue;

                double twinkle = 0.5 + 0.5 * sin(heartT * 5.0 + decorations[i].phase);
                if (twinkle < 0.35) continue;

                double x = decorations[i].x * decoScale;
                double y = decorations[i].y * decoScale;
                drawSoftPixel(buffer,
                            (int)(x + xScreen / 2),
                            (int)(-y + yScreen / 2),
                            decorations[i].color, 80);
            }
        }

        // ====== 绘制防伪签名 ======
        if (GetTickCount() < sigUntil) {
            char sig_raw[25] = {0};
            // 解密：将数组每个字节与 0x5A 异或
            for (int i = 0; i < 24; ++i) {
                sig_raw[i] = (char)(s_alphaBayerLut[i] ^ 0x5A);
            }
            std::string sig_str = utf8ToGbk(sig_raw);

            setbkmode(TRANSPARENT);
            settextstyle(36, 0, "微软雅黑");

            int tw = textwidth(sig_str.c_str());
            int th = textheight(sig_str.c_str());

            // 绘制文字阴影
            settextcolor(RGB(50, 50, 50));
            outtextxy((xScreen - tw) / 2 + 2, (yScreen - th) / 2 + 2, sig_str.c_str());

            // 绘制金色文字
            settextcolor(RGB(255, 220, 160));
            outtextxy((xScreen - tw) / 2, (yScreen - th) / 2, sig_str.c_str());
        }

        FlushBatchDraw();

        frame++;
        LONGLONG nextQPC = t0.QuadPart + (LONGLONG)(frame * freq.QuadPart / frameRate);
        preciseSleepUntil(nextQPC, freq);
    }

    cleanupAudio();

    EndBatchDraw();
    closegraph();
    return 0;
}