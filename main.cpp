// ============================================================
//  Android-style OS for ESP32 + TFT 320x240
//  - Fullscreen apps (no floating windows)
//  - Collapsible bottom navigation bar
//  - Swipe-up gesture to show/hide nav
//  - Apps: Files, Calc, Paint, Clock, Settings, Terminal, 
//          Notepad, Snake, Reaction game
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <math.h>

// ==================== ПИНЫ ====================
#define SCREEN_WIDTH   240
#define SCREEN_HEIGHT  320
#define TOUCH_CS       33
#define TOUCH_CLK      25
#define TOUCH_MOSI     32
#define TOUCH_MISO     39
#define BACKLIGHT_PIN  21
#define BUZZER_PIN     22

// ==================== КАЛИБРОВКА ====================
#define TS_MIN_X  200
#define TS_MAX_X  3750
#define TS_MIN_Y  250
#define TS_MAX_Y  3800

// ==================== ANDROID MATERIAL YOU DARK ====================
#define AND_BG         0x1082   // #101010 чёрный фон
#define AND_SURFACE    0x18E3   // #181818 поверхность
#define AND_SURFACE2   0x2124   // #212121
#define AND_SURFACE3   0x2945   // #292929
#define AND_CARD       0x2945   // карточка
#define AND_ACCENT     0x03DF   // #0396FF Google Blue
#define AND_ACCENT2    0x025A   // темнее
#define AND_GREEN      0x07E0   // зелёный
#define AND_RED        0xF800   // красный
#define AND_YELLOW     0xFFE0   // жёлтый
#define AND_ORANGE     0xFC60   // оранжевый
#define AND_PURPLE     0x801F   // фиолетовый
#define AND_TEAL       0x07FF   // голубой
#define AND_TEXT       0xFFFF   // белый
#define AND_TEXT2      0xBDF7   // светло-серый
#define AND_TEXT3      0x7BEF   // серый
#define AND_TEXT4      0x4A69   // тусклый
#define AND_BORDER     0x2945   // граница
#define AND_STATUSBAR  0x0841   // статусбар
#define AND_NAVBAR     0x0841   // нижняя навигация
#define AND_RIPPLE     0x3186   // ripple эффект
#define AND_DIVIDER    0x2104   // разделитель
#define AND_SHADOW     0x0000   // тень

// ==================== РАЗМЕРЫ ИНТЕРФЕЙСА ====================
#define STATUSBAR_H    20       // Статусбар
#define NAVBAR_H       40       // Нижняя навигация
#define CONTENT_Y      STATUSBAR_H
#define CONTENT_H      (SCREEN_HEIGHT - STATUSBAR_H - NAVBAR_H)
#define CONTENT_H_FULL (SCREEN_HEIGHT - STATUSBAR_H) // без навбара

// Когда навбар скрыт, контент занимает всё место до низа
bool navbarVisible = true;
bool navbarAnimating = false;
uint8_t navbarAlpha = 255; // для анимации

// ==================== ОБЪЕКТЫ ====================
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS);
Preferences prefs;

// ==================== ПРИЛОЖЕНИЯ ====================
enum AppID {
    APP_HOME = 0,
    APP_FILES,
    APP_CALC,
    APP_PAINT,
    APP_CLOCK,
    APP_SETTINGS,
    APP_TERMINAL,
    APP_NOTEPAD,
    APP_SNAKE,
    APP_REACTION,
    APP_COUNT
};

AppID currentApp = APP_HOME;
AppID prevApp    = APP_HOME;
bool inApp = false;

// ==================== НАСТРОЙКИ ====================
struct Settings {
    uint8_t  wallpaper;
    uint8_t  brightness;
    bool     soundEnabled;
    bool     showSeconds;
    uint32_t uptimeOffset;
    uint8_t  accentIdx;
} cfg;

uint16_t accentColors[] = {0x03DF, 0x07E0, 0xF800, 0xFFE0, 0xF81F, 0x07FF};
const char* accentNames[] = {"Blue","Green","Red","Yellow","Pink","Cyan"};
#define ACCENT_COUNT 6

void loadSettings() {
    prefs.begin("andos", false);
    cfg.wallpaper    = prefs.getUChar("wp",     0);
    cfg.brightness   = prefs.getUChar("bright", 200);
    cfg.soundEnabled = prefs.getBool("sound",   true);
    cfg.showSeconds  = prefs.getBool("secs",    true);
    cfg.uptimeOffset = prefs.getULong("uptime", 43200);
    cfg.accentIdx    = prefs.getUChar("accent", 0);
    prefs.end();
}
void saveSettings() {
    prefs.begin("andos", false);
    prefs.putUChar("wp",     cfg.wallpaper);
    prefs.putUChar("bright", cfg.brightness);
    prefs.putBool("sound",   cfg.soundEnabled);
    prefs.putBool("secs",    cfg.showSeconds);
    prefs.putULong("uptime", cfg.uptimeOffset + millis()/1000);
    prefs.putUChar("accent", cfg.accentIdx);
    prefs.end();
}
uint16_t getAccent() { return accentColors[cfg.accentIdx]; }

// ==================== ЗВУК / ПОДСВЕТКА ====================
void setBrightness(uint8_t v) { analogWrite(BACKLIGHT_PIN, v); }
void beep(uint16_t f=800, uint16_t d=25) {
    if (!cfg.soundEnabled) return;
    tone(BUZZER_PIN, f, d);
}
void sfxOpen()  { beep(660,30); delay(40); beep(880,40); }
void sfxClose() { beep(500,30); delay(40); beep(350,40); }
void sfxClick() { beep(1000,15); }
void sfxError() { beep(200,150); }
void sfxWin()   { beep(880,20); delay(30); beep(1100,20); delay(30); beep(1320,40); }

// ==================== ВРЕМЯ ====================
uint32_t getUptime() { return cfg.uptimeOffset + millis()/1000; }
void getHMS(uint32_t s, uint8_t &h, uint8_t &m, uint8_t &sec) {
    h   = (s/3600)%24; m = (s/60)%60; sec = s%60;
}
String timeStr() {
    uint8_t h,m,s; getHMS(getUptime(),h,m,s);
    char b[12];
    if(cfg.showSeconds) sprintf(b,"%02d:%02d:%02d",h,m,s);
    else                sprintf(b,"%02d:%02d",h,m);
    return String(b);
}
String dateStr() {
    const char* days[]   = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
    uint32_t s=getUptime();
    uint32_t day=(s/86400)%7;
    uint32_t mon=(s/2592000)%12;
    uint32_t dom=((s/86400)%28)+1;
    char b[20]; sprintf(b,"%s %lu %s",days[day],(unsigned long)dom,months[mon]);
    return String(b);
}

// ==================== UI ПРИМИТИВЫ ====================
void fillRR(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c) {
    tft.fillRoundRect(x,y,w,h,r,c);
}
void drawRR(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c) {
    tft.drawRoundRect(x,y,w,h,r,c);
}

// Material You кнопка
void drawMButton(int16_t x,int16_t y,int16_t w,int16_t h,
                  const char* text, bool filled=true, bool danger=false) {
    uint16_t bg = danger ? AND_RED : (filled ? getAccent() : AND_SURFACE3);
    fillRR(x,y,w,h,8,bg);
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
    tft.drawString(text, x+w/2, y+h/2);
}

// Слайдер
void drawSlider(int16_t x,int16_t y,int16_t w,uint8_t val,
                uint8_t vmin=0,uint8_t vmax=255) {
    fillRR(x,y+3,w,6,3,AND_SURFACE3);
    int fw=map(val,vmin,vmax,0,w-4);
    if(fw>0) fillRR(x+2,y+4,fw,4,2,getAccent());
    int hx=x+2+fw-6;
    tft.fillCircle(hx+6,y+6,8,AND_TEXT);
    tft.fillCircle(hx+6,y+6,5,getAccent());
}

// Toggle
void drawToggle(int16_t x,int16_t y,bool on) {
    uint16_t bg = on ? getAccent() : AND_SURFACE3;
    fillRR(x,y,44,24,12,bg);
    int kx = on ? x+22 : x+2;
    tft.fillCircle(kx+10,y+12,9,AND_TEXT);
}

// Ripple эффект (упрощённый)
void drawRipple(int16_t x,int16_t y) {
    for(int r2=2;r2<=12;r2+=4) {
        tft.drawCircle(x,y,r2,AND_RIPPLE);
        delay(10);
    }
}

// ==================== СТАТУСБАР ====================
void drawStatusBar() {
    tft.fillRect(0,0,SCREEN_WIDTH,STATUSBAR_H,AND_STATUSBAR);
    // Время
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    tft.drawString(timeStr(), 6, 10);
    // Иконки справа
    int rx = SCREEN_WIDTH - 4;
    // Батарея
    tft.drawRect(rx-20,4,18,12,AND_TEXT2);
    tft.fillRect(rx-1,7,3,6,AND_TEXT2);
    tft.fillRect(rx-19,5,15,10,AND_GREEN);
    // WiFi
    rx -= 28;
    for(int i=0;i<3;i++) {
        int bh=(i+1)*3;
        uint16_t wc=(i==2)?AND_TEXT:AND_TEXT3;
        tft.fillRect(rx-i*6,STATUSBAR_H-bh-2,4,bh,wc);
    }
    // Имя приложения по центру если открыто
    if(inApp) {
        const char* appNames[] = {"Home","Files","Calc","Paint","Clock","Settings","Terminal","Notepad","Snake","Reaction"};
        tft.setTextColor(AND_TEXT2); tft.setTextDatum(MC_DATUM);
        tft.drawString(appNames[currentApp], SCREEN_WIDTH/2, 10);
    }
}

// ==================== НАВБАР (ANDROID-СТИЛЬ) ====================
// 3 кнопки: < (назад), O (домой), [] (последние)
void drawNavBar() {
    if(!navbarVisible) return;
    int ny = SCREEN_HEIGHT - NAVBAR_H;
    tft.fillRect(0,ny,SCREEN_WIDTH,NAVBAR_H,AND_NAVBAR);
    tft.drawFastHLine(0,ny,SCREEN_WIDTH,AND_BORDER);
    // Кнопка Назад (<)
    int bx = SCREEN_WIDTH/2 - 80;
    fillRR(bx-18,ny+8,36,24,12,AND_SURFACE2);
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
    tft.drawString("<", bx, ny+20);
    // Кнопка Домой (O)
    tft.fillCircle(SCREEN_WIDTH/2,ny+20,14,AND_SURFACE2);
    tft.drawCircle(SCREEN_WIDTH/2,ny+20,14,AND_TEXT3);
    tft.drawCircle(SCREEN_WIDTH/2,ny+20,9,AND_TEXT2);
    // Кнопка Последние (квадрат)
    int rx2 = SCREEN_WIDTH/2 + 80;
    fillRR(rx2-18,ny+8,36,24,12,AND_SURFACE2);
    fillRR(rx2-10,ny+12,20,16,4,AND_TEXT3);
    // Индикатор скрытия (полоска)
    tft.drawFastHLine(SCREEN_WIDTH/2-20,ny+2,40,AND_TEXT4);
}

// ==================== ДОМАШНИЙ ЭКРАН ====================
struct HomeApp {
    const char* name;
    const char* sym;
    AppID app;
    uint16_t color;
};
HomeApp homeApps[] = {
    {"Files",    "F",  APP_FILES,    0x3B4C},
    {"Calc",     "C",  APP_CALC,     0x2DC5},
    {"Paint",    "P",  APP_PAINT,    0xD8A4},
    {"Clock",    "T",  APP_CLOCK,    0x3392},
    {"Settings", "S",  APP_SETTINGS, 0x5145},
    {"Terminal", ">",  APP_TERMINAL, 0x0841},
    {"Notepad",  "N",  APP_NOTEPAD,  0x4A09},
    {"Snake",    "SN", APP_SNAKE,    0x07E0},
    {"Reaction", "!",  APP_REACTION, 0xFC00},
};
#define HOME_APP_COUNT 9

uint16_t wpColors[] = {0x18C3, 0x0010, 0x4000, 0x3000, 0x2008, AND_BG};

void drawHomeScreen() {
    // Фон
    tft.fillRect(0, STATUSBAR_H, SCREEN_WIDTH,
        navbarVisible ? CONTENT_H : CONTENT_H_FULL,
        wpColors[cfg.wallpaper < 6 ? cfg.wallpaper : 0]);

    // Большие часы по центру вверху
    int cy = STATUSBAR_H + 18;
    tft.setTextColor(AND_TEXT); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
    tft.drawString(timeStr(), SCREEN_WIDTH/2, cy);
    tft.setTextSize(1); tft.setTextColor(AND_TEXT2);
    tft.drawString(dateStr(), SCREEN_WIDTH/2, cy+18);

    // Сетка иконок 3x3
    int iconW = 64, iconH = 64;
    int startX = (SCREEN_WIDTH - 3*iconW) / 2;
    int startY = cy + 34;

    for(int i=0; i<HOME_APP_COUNT; i++) {
        int col = i%3, row = i/3;
        int ix = startX + col*iconW + (iconW-44)/2;
        int iy = startY + row*iconH;

        // Иконка
        fillRR(ix, iy, 44, 44, 12, homeApps[i].color);
        // Тень
        tft.drawFastHLine(ix+3, iy+45, 38, AND_SHADOW);
        tft.setTextColor(AND_TEXT); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
        tft.drawString(homeApps[i].sym, ix+22, iy+22);
        // Подпись
        tft.setTextSize(1); tft.setTextColor(AND_TEXT);
        // Тень текста
        tft.setTextColor(AND_SHADOW);
        tft.drawString(homeApps[i].name, ix+23, iy+52);
        tft.setTextColor(AND_TEXT);
        tft.drawString(homeApps[i].name, ix+22, iy+51);
    }

    // Подсказка: свайп вверх
    if(!navbarVisible) {
        tft.setTextColor(AND_TEXT4); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString("^ swipe up for nav", SCREEN_WIDTH/2, SCREEN_HEIGHT-8);
    }
}

// ==================== ВСПОМОГАТЕЛЬНАЯ ФУНКЦИЯ ====================
// Получить рабочую высоту экрана (с навбаром или без)
int16_t getContentH() {
    return navbarVisible ? CONTENT_H : CONTENT_H_FULL;
}
int16_t getNavbarY() {
    return navbarVisible ? (SCREEN_HEIGHT - NAVBAR_H) : SCREEN_HEIGHT;
}

// Очистить контентную область
void clearContent() {
    tft.fillRect(0, STATUSBAR_H, SCREEN_WIDTH, getContentH(), AND_BG);
}

// Шапка приложения (Toolbar)
void drawAppToolbar(const char* title, bool showBack=true) {
    int ty = STATUSBAR_H;
    fillRR(0, ty, SCREEN_WIDTH, 36, 0, AND_SURFACE);
    tft.drawFastHLine(0, ty+36, SCREEN_WIDTH, AND_BORDER);
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
    tft.drawString(title, SCREEN_WIDTH/2, ty+18);
    if(showBack) {
        tft.setTextColor(getAccent()); tft.setTextDatum(ML_DATUM);
        tft.drawString("< Back", 8, ty+18);
    }
}

// ==================== FILES APP ====================
struct FileEntry { const char* name; const char* icon; bool isDir; uint32_t size; };
FileEntry files[] = {
    {"Documents", "D", true,  0},
    {"Pictures",  "P", true,  0},
    {"Music",     "M", true,  0},
    {"Downloads", "G", true,  0},
    {"notes.txt", "T", false, 1024},
    {"photo.png", "I", false, 204800},
    {"data.csv",  "C", false, 512},
    {"backup.zip","Z", false, 102400},
};
#define FILE_COUNT 8
int8_t fileSelected=-1;
int16_t fileScrollY=0;

void renderFiles() {
    clearContent();
    drawAppToolbar("Files");
    int cy = STATUSBAR_H + 40;

    // Путь
    fillRR(8, cy, SCREEN_WIDTH-16, 22, 6, AND_SURFACE2);
    tft.setTextColor(AND_ACCENT); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    tft.drawString("~/Home", 16, cy+11);
    cy += 28;

    // Список файлов
    for(int i=0; i<FILE_COUNT; i++) {
        int fy = cy + i*32 - fileScrollY;
        if(fy < STATUSBAR_H+40 || fy > getNavbarY()-4) continue;
        bool sel = (fileSelected==i);
        fillRR(8, fy, SCREEN_WIDTH-16, 28, 6,
               sel ? AND_SURFACE3 : AND_SURFACE2);
        if(sel) drawRR(8,fy,SCREEN_WIDTH-16,28,6,getAccent());
        // Иконка
        uint16_t icol = files[i].isDir ? AND_ACCENT : AND_TEXT3;
        tft.fillCircle(24, fy+14, 10, icol);
        tft.setTextColor(AND_BG); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString(files[i].icon, 24, fy+14);
        // Имя
        tft.setTextColor(AND_TEXT); tft.setTextDatum(ML_DATUM);
        tft.drawString(files[i].name, 40, fy+10);
        // Размер
        tft.setTextColor(AND_TEXT3); tft.setTextSize(1);
        if(!files[i].isDir) {
            char sb[16];
            if(files[i].size>=1024) sprintf(sb,"%luK",(unsigned long)files[i].size/1024);
            else sprintf(sb,"%luB",(unsigned long)files[i].size);
            tft.setTextDatum(MR_DATUM);
            tft.drawString(sb, SCREEN_WIDTH-12, fy+14);
        } else {
            tft.setTextDatum(MR_DATUM);
            tft.drawString("Folder", SCREEN_WIDTH-12, fy+14);
        }
    }
}

// ==================== CALC APP ====================
char calcDisp[20] = "0";
double calcV1=0,calcV2=0;
char calcOp=0;
bool calcNew=true,calcErr=false;
bool calcHasDecimal=false;

const char* calcBtns[5][4]={
    {"AC","±","%","/"},
    {"7","8","9","×"},
    {"4","5","6","−"},
    {"1","2","3","+"},
    {"0",".","=","="}
};

void calcPress(const char* b) {
    sfxClick();
    if(strcmp(b,"AC")==0){
        strcpy(calcDisp,"0");calcV1=0;calcV2=0;calcOp=0;
        calcNew=true;calcErr=false;calcHasDecimal=false;
    } else if(strcmp(b,"±")==0){
        double v=atof(calcDisp); v=-v;
        dtostrf(v,1,4,calcDisp);
        char* d=strchr(calcDisp,'.');
        if(d){char* e=calcDisp+strlen(calcDisp)-1;while(*e=='0'&&e>d){*e=0;e--;}if(*e=='.')*e=0;}
    } else if(strcmp(b,"%")==0){
        double v=atof(calcDisp)/100.0;
        dtostrf(v,1,6,calcDisp);
        char* d=strchr(calcDisp,'.');
        if(d){char* e=calcDisp+strlen(calcDisp)-1;while(*e=='0'&&e>d){*e=0;e--;}if(*e=='.')*e=0;}
    } else if(strcmp(b,"=")==0){
        calcV2=atof(calcDisp);double res=0;calcErr=false;
        if(calcOp){
            switch(calcOp){
                case '+':res=calcV1+calcV2;break;
                case '-':res=calcV1-calcV2;break;
                case '*':res=calcV1*calcV2;break;
                case '/':if(calcV2!=0)res=calcV1/calcV2;else calcErr=true;break;
            }
        } else res=calcV2;
        if(!calcErr){
            dtostrf(res,1,6,calcDisp);
            char* d=strchr(calcDisp,'.');
            if(d){char* e=calcDisp+strlen(calcDisp)-1;while(*e=='0'&&e>d){*e=0;e--;}if(*e=='.')*e=0;}
        } else strcpy(calcDisp,"Error");
        calcNew=true;calcHasDecimal=false;
    } else if(b[0]=='+'||b[0]=='-'||b[0]=='*'||b[0]=='/'||
              strcmp(b,"×")==0||strcmp(b,"−")==0){
        calcV1=atof(calcDisp);
        if(strcmp(b,"×")==0) calcOp='*';
        else if(strcmp(b,"−")==0) calcOp='-';
        else calcOp=b[0];
        calcNew=true;calcHasDecimal=false;
    } else if(strcmp(b,".")==0){
        if(!calcHasDecimal&&strlen(calcDisp)<15){
            strcat(calcDisp,".");calcHasDecimal=true;calcNew=false;
        }
    } else {
        if(calcNew){strcpy(calcDisp,b);calcNew=false;}
        else if(strlen(calcDisp)<14) strcat(calcDisp,b);
    }
}

void renderCalc() {
    clearContent();
    int cw = SCREEN_WIDTH;
    int cy = STATUSBAR_H;

    // Большой дисплей
    tft.fillRect(0, cy, cw, 70, AND_SURFACE);
    // Подсказка оператора
    if(calcOp && !calcNew) {
        char ob[3]={calcOp,0};
        tft.setTextColor(getAccent()); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
        tft.drawString(ob, 8, cy+12);
    }
    tft.setTextColor(calcErr ? AND_RED : AND_TEXT);
    tft.setTextSize(3); tft.setTextDatum(MR_DATUM);
    tft.drawString(calcDisp, cw-8, cy+40);
    tft.drawFastHLine(0, cy+68, cw, AND_BORDER);

    cy += 72;
    int bh=(getNavbarY()-cy)/5-2;
    int bw=cw/4-2;

    // Кнопки 5x4
    for(int r=0;r<5;r++) {
        for(int c=0;c<4;c++) {
            // Строка 4: "0" широкая, "=" широкая
            if(r==4 && c==2) continue; // пропуск
            int bx2,bw2=bw;
            if(r==4&&c==0) bw2=bw*2+2; // "0" - двойная
            if(r==4&&c==2) continue;
            bx2=c*(bw+2)+1;
            if(r==4&&c>=2) bx2=(c+1)*(bw+2)+1; // "=" справа
            int by2=cy+r*(bh+2);
            uint16_t bg;
            if(r==0) bg=AND_SURFACE3; // функциональные
            else if(c==3) bg=getAccent(); // операторы
            else bg=AND_SURFACE2; // цифры
            fillRR(bx2,by2,bw2,bh,10,bg);
            tft.setTextColor(AND_TEXT); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
            tft.drawString(calcBtns[r][c],bx2+bw2/2,by2+bh/2);
        }
    }
    // Перерисуем правильно
    for(int r=0;r<5;r++) {
        for(int c=0;c<4;c++) {
            if(r==4&&c==2) continue;
            int bx2,bw2=bw;
            if(r==4&&c==0) {bx2=1;bw2=bw*2+2;}
            else if(r==4&&c==1) {bx2=bw*2+5;bw2=bw;}
            else if(r==4&&c==3) {bx2=bw*3+7;bw2=bw;}
            else bx2=c*(bw+2)+1;
            int by2=cy+r*(bh+2);
            uint16_t bg;
            if(r==0) bg=AND_SURFACE3;
            else if(c==3) bg=getAccent();
            else bg=AND_SURFACE2;
            fillRR(bx2,by2,bw2,bh,10,bg);
            const char* lbl;
            if(r==4&&c==0) lbl="0";
            else if(r==4&&c==1) lbl=".";
            else if(r==4&&c==3) lbl="=";
            else lbl=calcBtns[r][c];
            tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
            tft.drawString(lbl,bx2+bw2/2,by2+bh/2);
        }
    }
}

// ==================== PAINT APP ====================
uint8_t paintColor=0, paintSize=3;
bool paintNeedClear=false, paintWasTouching=false;
int16_t paintLastX=-1, paintLastY=-1;
bool paintToolbarOpen=true;

#define PAINT_TOOLBAR_H 60
uint16_t paintPalette[]={
    TFT_BLACK,TFT_WHITE,AND_RED,AND_ORANGE,
    AND_YELLOW,AND_GREEN,AND_TEAL,AND_ACCENT,
    AND_PURPLE,AND_TEXT3,0xFC10,0x07E0
};
#define PAINT_PAL 12

int16_t paintContentX() { return 0; }
int16_t paintContentY() { return STATUSBAR_H+36+PAINT_TOOLBAR_H; }
int16_t paintContentW() { return SCREEN_WIDTH; }
int16_t paintContentH() { return getNavbarY() - paintContentY(); }

void renderPaintToolbar() {
    fillRR(0, STATUSBAR_H+36, SCREEN_WIDTH, PAINT_TOOLBAR_H, 0, AND_SURFACE2);
    tft.drawFastHLine(0, STATUSBAR_H+36+PAINT_TOOLBAR_H, SCREEN_WIDTH, AND_BORDER);
    // Palette 2 rows
    for(int i=0;i<PAINT_PAL;i++) {
        int col=i%6,row=i/6;
        int px=2+col*39,py=STATUSBAR_H+40+row*20;
        fillRR(px,py,37,18,4,paintPalette[i]);
        if(i==paintColor) {
            drawRR(px-1,py-1,39,20,5,AND_TEXT);
        }
    }
    // Размер кисти и Clear в одном ряду
    int sz_y=STATUSBAR_H+40+40;
    tft.setTextColor(AND_TEXT3); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
    tft.drawString("Sz:", 20, sz_y);
    for(int i=1;i<=4;i++) {
        int bx2=38+(i-1)*15, by2=sz_y;
        tft.fillCircle(bx2,by2,i,(i==paintSize)?AND_TEXT:AND_TEXT4);
    }
    // Clear
    fillRR(SCREEN_WIDTH-40,sz_y-10,38,20,8,AND_RED);
    tft.setTextColor(AND_TEXT); tft.setTextDatum(MC_DATUM);
    tft.drawString("CLR", SCREEN_WIDTH-21, sz_y);
}

void renderPaint() {
    drawAppToolbar("Paint");
    renderPaintToolbar();
    // Рамка холста
    tft.drawRect(paintContentX()-1, paintContentY()-1,
                 paintContentW()+2, paintContentH()+2, AND_BORDER);
    if(paintNeedClear) {
        tft.fillRect(paintContentX(), paintContentY(),
                     paintContentW(), paintContentH(), TFT_WHITE);
        paintNeedClear=false;
    }
}

// ==================== CLOCK APP ====================
void renderClock() {
    clearContent();
    drawAppToolbar("Clock");
    int ccy = STATUSBAR_H+36;
    int ccH = getNavbarY()-ccy;

    int cx=SCREEN_WIDTH/2, cy=ccy+ccH/2-10;
    int r=min((SCREEN_WIDTH-40)/2, (ccH-50)/2);

    tft.fillCircle(cx,cy,r+3,AND_SURFACE2);
    tft.fillCircle(cx,cy,r,AND_SURFACE);

    uint32_t s=getUptime();
    for(int i=0;i<60;i++) {
        float a=i*6*DEG_TO_RAD;
        bool isH=(i%5==0);
        int r1=r-(isH?10:5),r2=r-1;
        tft.drawLine(cx+(int)(r1*sin(a)),cy-(int)(r1*cos(a)),
                     cx+(int)(r2*sin(a)),cy-(int)(r2*cos(a)),
                     isH?AND_TEXT:AND_TEXT4);
    }
    for(int i=1;i<=12;i++) {
        float a=i*30*DEG_TO_RAD;
        char nb[3]; sprintf(nb,"%d",i);
        tft.setTextColor(AND_TEXT2); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString(nb,cx+(int)((r-18)*sin(a)),cy-(int)((r-18)*cos(a)));
    }
    float sa=(s%60)*6*DEG_TO_RAD;
    float ma=(s/60%60)*6*DEG_TO_RAD+sa/60;
    float ha=(s/3600%12)*30*DEG_TO_RAD+ma/12;
    for(int d=-1;d<=1;d++) {
        float da=d*DEG_TO_RAD*1.5;
        tft.drawLine(cx,cy,cx+(int)((r*0.55)*sin(ha+da)),cy-(int)((r*0.55)*cos(ha+da)),AND_TEXT);
    }
    tft.drawLine(cx,cy,cx+(int)((r*0.78)*sin(ma)),cy-(int)((r*0.78)*cos(ma)),AND_TEXT2);
    tft.drawLine(cx,cy,cx+(int)((r*0.9)*sin(sa)),cy-(int)((r*0.9)*cos(sa)),getAccent());
    tft.drawLine(cx,cy,cx-(int)((r*0.2)*sin(sa)),cy+(int)((r*0.2)*cos(sa)),getAccent());
    tft.fillCircle(cx,cy,5,getAccent());
    tft.fillCircle(cx,cy,3,AND_TEXT);

    tft.setTextColor(AND_TEXT); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
    tft.drawString(timeStr(),cx,getNavbarY()-18);
    tft.setTextColor(AND_TEXT3); tft.setTextSize(1);
    tft.drawString(dateStr(),cx,getNavbarY()-34);
}

// ==================== TERMINAL APP ====================
char termLines[8][48];
uint8_t termLineCount=0;
char termInput[32]="";
uint8_t termInputLen=0;

const char* termKB[3][6]={
    {"q","w","e","r","t","y"},
    {"a","s","d","f","g","h"},
    {"z","x","c","v","b","n"}
};

void termExec(const char* cmd) {
    if(termLineCount>=8){for(int i=0;i<7;i++)strcpy(termLines[i],termLines[i+1]);termLineCount=7;}
    sprintf(termLines[termLineCount++],"> %s",cmd);
    if(strcmp(cmd,"help")==0){if(termLineCount<8)strcpy(termLines[termLineCount++],"help uname uptime clear ls");}
    else if(strcmp(cmd,"uname")==0){if(termLineCount<8)strcpy(termLines[termLineCount++],"ESP32 AndroidOS 2.0");}
    else if(strcmp(cmd,"uptime")==0){char b[30];sprintf(b,"up %lus",(unsigned long)getUptime());if(termLineCount<8)strcpy(termLines[termLineCount++],b);}
    else if(strcmp(cmd,"clear")==0){termLineCount=0;}
    else if(strcmp(cmd,"ls")==0){if(termLineCount<8)strcpy(termLines[termLineCount++],"Documents Pictures Music");}
    else{if(termLineCount<8){char b[48];sprintf(b,"bash: %s: not found",cmd);strcpy(termLines[termLineCount++],b);}}
}

void renderTerminal() {
    clearContent();
    drawAppToolbar("Terminal");
    tft.setTextColor(AND_GREEN); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
    int ty=STATUSBAR_H+40;
    for(int i=0;i<termLineCount;i++) tft.drawString(termLines[i],4,ty+i*11);
    // Ввод
    int iy=ty+8*11;
    tft.setTextColor(getAccent()); tft.drawString("$ ",4,iy);
    tft.setTextColor(AND_TEXT); tft.drawString(termInput,16,iy);
    static bool cv=false; static uint32_t ct=0;
    if(millis()-ct>500){cv=!cv;ct=millis();}
    if(cv) tft.drawChar(16+termInputLen*6,iy,'_',AND_TEXT,AND_BG,1);
    // Клавиатура
    int kbY=iy+14;
    if(kbY>getNavbarY()-78) kbY=getNavbarY()-76;
    fillRR(0,kbY,SCREEN_WIDTH,78,0,AND_SURFACE2);
    tft.drawFastHLine(0,kbY,SCREEN_WIDTH,AND_BORDER);
    for(int r=0;r<3;r++) for(int c=0;c<6;c++) {
        const char* k=termKB[r][c];
        char lbl[4]; strcpy(lbl,k);
        if(strcmp(k,"_")==0) strcpy(lbl,"SP");
        if(strcmp(k,"<")==0) strcpy(lbl,"BS");
        fillRR(2+c*39,kbY+2+r*24,37,20,6,AND_SURFACE3);
        tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString(lbl,2+c*39+18,kbY+2+r*24+10);
    }
    fillRR(2,kbY+2+3*24,118,18,6,AND_SURFACE3);
    tft.setTextColor(AND_TEXT); tft.drawString("SPACE",62,kbY+2+3*24+9);
    fillRR(122,kbY+2+3*24,116,18,6,getAccent());
    tft.drawString("ENTER",180,kbY+2+3*24+9);
}

// ==================== NOTEPAD APP ====================
char noteText[512]="";
uint16_t noteLen=0;
const char* noteKB[4][10]={
    {"1","2","3","4","5","6","7","8","9","0"},
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l","<"},
    {"z","x","c","v","b","n","m",","," ","."},
};
#define NOTE_KB_ROWS 4

void renderNotepad() {
    clearContent();
    drawAppToolbar("Notepad");
    // Текст
    int ty=STATUSBAR_H+40;
    int kbH=NOTE_KB_ROWS*24+6;
    int textH=getNavbarY()-ty-kbH-4;
    fillRR(4,ty,SCREEN_WIDTH-8,textH,6,AND_SURFACE2);
    // Вывод текста
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
    // Простой word-wrap
    int px=8,py=ty+4,lineH=11;
    for(uint16_t i=0;i<noteLen;i++) {
        char c=noteText[i];
        if(c=='\n'||px>SCREEN_WIDTH-16){px=8;py+=lineH;}
        if(py>ty+textH-lineH) break;
        if(c!='\n'){char cb[2]={c,0};tft.drawString(cb,px,py);px+=6;}
    }
    // Курсор
    static bool cv=false; static uint32_t ct=0;
    if(millis()-ct>500){cv=!cv;ct=millis();}
    if(cv) tft.drawChar(px,py,'|',getAccent(),AND_SURFACE2,1);
    // Клавиатура
    int kbY=getNavbarY()-kbH-2;
    fillRR(0,kbY,SCREEN_WIDTH,kbH+2,0,AND_SURFACE2);
    tft.drawFastHLine(0,kbY,SCREEN_WIDTH,AND_BORDER);
    for(int r=0;r<NOTE_KB_ROWS;r++) {
        for(int c=0;c<6;c++) {
            const char* k=noteKB[r][c];
            char lbl[4]; strcpy(lbl,k);
            if(strcmp(k,"<")==0) strcpy(lbl,"<-");
            fillRR(2+c*39,kbY+2+r*24,37,20,6,AND_SURFACE3);
            tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
            tft.drawString(lbl,2+c*39+18,kbY+2+r*24+10);
        }
    }
}

// ==================== SETTINGS APP ====================
int8_t settingsSection=0; // 0=main, 1=display, 2=sound, 3=about
int16_t settingsScroll=0;

void renderSettingsMain() {
    clearContent();
    drawAppToolbar("Settings");
    int sy=STATUSBAR_H+44;
    int sw=SCREEN_WIDTH-16;

    struct SettItem {const char* icon; const char* title; const char* sub; uint16_t ic;};
    SettItem items[]={
        {"D","Display","Brightness, Wallpaper",AND_ACCENT},
        {"V","Sound","Volume, Notifications",AND_GREEN},
        {"C","Clock","Format, Seconds",AND_PURPLE},
        {"A","Accent Color","UI Color Theme",AND_ORANGE},
        {"I","About Device","System Info",AND_TEXT3},
    };
    for(int i=0;i<5;i++) {
        int iy=sy+i*44;
        if(iy>getNavbarY()-4) break;
        fillRR(8,iy,sw,38,10,AND_SURFACE2);
        // Иконка
        tft.fillCircle(28,iy+19,12,items[i].ic);
        tft.setTextColor(AND_BG); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString(items[i].icon,28,iy+19);
        // Текст
        tft.setTextColor(AND_TEXT); tft.setTextDatum(ML_DATUM);
        tft.drawString(items[i].title,46,iy+13);
        tft.setTextColor(AND_TEXT3);
        tft.drawString(items[i].sub,46,iy+25);
        // Стрелка
        tft.setTextColor(AND_TEXT4); tft.setTextDatum(MR_DATUM);
        tft.drawString(">",SCREEN_WIDTH-12,iy+19);
    }
}

void renderSettingsDisplay() {
    clearContent();
    fillRR(0,STATUSBAR_H,SCREEN_WIDTH,36,0,AND_SURFACE);
    tft.drawFastHLine(0,STATUSBAR_H+36,SCREEN_WIDTH,AND_BORDER);
    tft.setTextColor(getAccent()); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    tft.drawString("< Settings",8,STATUSBAR_H+18);
    tft.setTextColor(AND_TEXT); tft.setTextDatum(MC_DATUM);
    tft.drawString("Display",SCREEN_WIDTH/2,STATUSBAR_H+18);

    int sy=STATUSBAR_H+44;
    // Яркость
    fillRR(8,sy,SCREEN_WIDTH-16,48,10,AND_SURFACE2);
    tft.setTextColor(AND_TEXT); tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
    tft.drawString("Brightness",16,sy+10);
    drawSlider(16,sy+26,SCREEN_WIDTH-40,cfg.brightness);
    char bv[8]; sprintf(bv,"%d%%",(int)map(cfg.brightness,0,255,0,100));
    tft.setTextColor(AND_TEXT3); tft.setTextDatum(MR_DATUM);
    tft.drawString(bv,SCREEN_WIDTH-12,sy+10);
    sy+=56;

    // Обои
    tft.setTextColor(AND_TEXT2); tft.setTextDatum(ML_DATUM);
    tft.drawString("Wallpaper",16,sy);
    sy+=16;
    uint16_t wps[]={0x18C3,0x0010,0x4000,0x3000,0x2008,AND_BG};
    const char* wpn[]={"Teal","Navy","Forest","Rust","Midnight","Black"};
    for(int i=0;i<6;i++) {
        int wx2=8+i*(SCREEN_WIDTH-16-6*6)/6+i*4+3;
        fillRR(wx2,sy,36,36,8,wps[i]);
        if(i==cfg.wallpaper) drawRR(wx2-2,sy-2,40,40,10,AND_TEXT);
        tft.setTextColor(AND_TEXT2); tft.setTextDatum(MC_DATUM);
        tft.drawString(wpn[i],wx2+18,sy+44);
    }
    sy+=58;

    // Акцентный цвет
    tft.setTextColor(AND_TEXT2); tft.setTextDatum(ML_DATUM);
    tft.drawString("Accent Color",16,sy);
    sy+=16;
    for(int i=0;i<ACCENT_COUNT;i++) {
        int ax=8+i*(SCREEN_WIDTH-16-6*6)/6+i*4+3;
        tft.fillCircle(ax+18,sy+14,14,accentColors[i]);
        if(i==cfg.accentIdx) tft.drawCircle(ax+18,sy+14,16,AND_TEXT);
        tft.setTextColor(AND_TEXT3); tft.setTextDatum(MC_DATUM);
        tft.drawString(accentNames[i],ax+18,sy+32);
    }
    sy+=44;

    // Кнопка Save
    drawMButton(8,sy,SCREEN_WIDTH-16,32,"Save Settings",true,false);
}

void renderSettingsSound() {
    clearContent();
    fillRR(0,STATUSBAR_H,SCREEN_WIDTH,36,0,AND_SURFACE);
    tft.drawFastHLine(0,STATUSBAR_H+36,SCREEN_WIDTH,AND_BORDER);
    tft.setTextColor(getAccent()); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    tft.drawString("< Settings",8,STATUSBAR_H+18);
    tft.setTextColor(AND_TEXT); tft.setTextDatum(MC_DATUM);
    tft.drawString("Sound",SCREEN_WIDTH/2,STATUSBAR_H+18);
    int sy=STATUSBAR_H+48;
    fillRR(8,sy,SCREEN_WIDTH-16,42,10,AND_SURFACE2);
    tft.setTextColor(AND_TEXT); tft.setTextDatum(ML_DATUM);
    tft.drawString("Sound Effects",16,sy+12);
    tft.setTextColor(AND_TEXT3); tft.drawString("Beeper sound",16,sy+26);
    drawToggle(SCREEN_WIDTH-56,sy+9,cfg.soundEnabled);
    sy+=50;
    fillRR(8,sy,SCREEN_WIDTH-16,42,10,AND_SURFACE2);
    tft.setTextColor(AND_TEXT); tft.drawString("Show Seconds",16,sy+12);
    tft.setTextColor(AND_TEXT3); tft.drawString("Clock display",16,sy+26);
    drawToggle(SCREEN_WIDTH-56,sy+9,cfg.showSeconds);
}

void renderSettingsAbout() {
    clearContent();
    fillRR(0,STATUSBAR_H,SCREEN_WIDTH,36,0,AND_SURFACE);
    tft.drawFastHLine(0,STATUSBAR_H+36,SCREEN_WIDTH,AND_BORDER);
    tft.setTextColor(getAccent()); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    tft.drawString("< Settings",8,STATUSBAR_H+18);
    tft.setTextColor(AND_TEXT); tft.setTextDatum(MC_DATUM);
    tft.drawString("About",SCREEN_WIDTH/2,STATUSBAR_H+18);
    int sy=STATUSBAR_H+48;
    // Лого
    tft.fillCircle(SCREEN_WIDTH/2,sy+24,28,getAccent());
    tft.fillCircle(SCREEN_WIDTH/2,sy+24,20,AND_SURFACE);
    tft.fillCircle(SCREEN_WIDTH/2-8,sy+18,5,getAccent());
    tft.fillCircle(SCREEN_WIDTH/2+8,sy+18,5,getAccent());
    tft.fillCircle(SCREEN_WIDTH/2,sy+28,5,getAccent());
    sy+=60;
    struct InfoRow{const char* k;const char* v;};
    InfoRow rows[]={
        {"OS","AndroidOS ESP32 2.0"},
        {"Device","ESP32-WROOM"},
        {"Display","320x240 TFT"},
        {"Free Heap",""},
        {"Uptime",""},
    };
    char heapBuf[16]; sprintf(heapBuf,"%luB",(unsigned long)ESP.getFreeHeap());
    char uptBuf[16]; sprintf(uptBuf,"%lus",(unsigned long)getUptime());
    rows[3].v=heapBuf; rows[4].v=uptBuf;
    for(int i=0;i<5;i++){
        fillRR(8,sy+i*32,SCREEN_WIDTH-16,28,8,AND_SURFACE2);
        tft.setTextColor(AND_TEXT3); tft.setTextDatum(ML_DATUM);
        tft.drawString(rows[i].k,16,sy+i*32+14);
        tft.setTextColor(AND_TEXT); tft.setTextDatum(MR_DATUM);
        tft.drawString(rows[i].v,SCREEN_WIDTH-12,sy+i*32+14);
    }
    sy+=5*32+6;
    drawMButton(8,sy,SCREEN_WIDTH-16,28,"Restart Device",false,true);
}

void renderSettings() {
    switch(settingsSection) {
        case 0: renderSettingsMain();    break;
        case 1: renderSettingsDisplay(); break;
        case 2: renderSettingsSound();   break;
        case 4: renderSettingsAbout();   break;
        default: settingsSection=0; renderSettingsMain(); break;
    }
}

// ==================== SNAKE GAME ====================
#define SNAKE_COLS 20
#define SNAKE_ROWS 13
#define SNAKE_SZ   11

struct SnakePoint { int8_t x, y; };
SnakePoint snakeBody[SNAKE_COLS*SNAKE_ROWS];
uint16_t snakeLen=4;
int8_t snakeDX=1, snakeDY=0;
SnakePoint snakeFood;
bool snakeAlive=true, snakeStarted=false;
uint16_t snakeScore=0;
uint32_t snakeLastMove=0;
uint16_t snakeSpeed=300;

int16_t snakeOffX, snakeOffY;

void snakePlaceFood() {
    do {
        snakeFood.x=random(0,SNAKE_COLS);
        snakeFood.y=random(0,SNAKE_ROWS);
        bool ok=true;
        for(int i=0;i<snakeLen;i++)
            if(snakeBody[i].x==snakeFood.x&&snakeBody[i].y==snakeFood.y){ok=false;break;}
        if(ok) break;
    } while(1);
}

void snakeInit() {
    snakeLen=4; snakeDX=1; snakeDY=0;
    snakeAlive=true; snakeScore=0; snakeSpeed=300;
    for(int i=0;i<snakeLen;i++){snakeBody[i].x=snakeLen-1-i;snakeBody[i].y=SNAKE_ROWS/2;}
    snakePlaceFood();
}

void snakeDrawCell(int8_t x,int8_t y,uint16_t c) {
    tft.fillRect(snakeOffX+x*SNAKE_SZ, snakeOffY+y*SNAKE_SZ, SNAKE_SZ-1, SNAKE_SZ-1, c);
}

void renderSnake(bool full=false) {
    if(full) {
        clearContent();
        drawAppToolbar("Snake");
        // Поле
        int fw=SNAKE_COLS*SNAKE_SZ, fh=SNAKE_ROWS*SNAKE_SZ;
        snakeOffX=(SCREEN_WIDTH-fw)/2;
        snakeOffY=STATUSBAR_H+40;
        tft.fillRect(snakeOffX,snakeOffY,fw,fh,AND_SURFACE2);
        drawRR(snakeOffX-1,snakeOffY-1,fw+2,fh+2,2,AND_BORDER);
        // Счёт
        tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
        char sc[20]; sprintf(sc,"Score: %d",snakeScore);
        tft.drawString(sc,8,STATUSBAR_H+48+fh+8);
        // Управление (кнопки)
        int bY=snakeOffY+fh+2;
        if(bY+76<=getNavbarY()) {
            int bsz=30;
            int bX=SCREEN_WIDTH/2-bsz/2;
            fillRR(bX,bY,bsz,bsz,6,AND_SURFACE3); tft.setTextColor(AND_TEXT); tft.setTextDatum(MC_DATUM); tft.drawString("^",bX+bsz/2,bY+bsz/2);
            fillRR(bX-bsz-2,bY+bsz+2,bsz,bsz,6,AND_SURFACE3); tft.drawString("<",bX-bsz-2+bsz/2,bY+bsz+2+bsz/2);
            fillRR(bX,bY+bsz+2,bsz,bsz,6,AND_SURFACE3); tft.drawString("v",bX+bsz/2,bY+bsz+2+bsz/2);
            fillRR(bX+bsz+2,bY+bsz+2,bsz,bsz,6,AND_SURFACE3); tft.drawString(">",bX+bsz+2+bsz/2,bY+bsz+2+bsz/2);
        }
    }
    if(!snakeStarted) {
        fillRR(snakeOffX+10,snakeOffY+SNAKE_ROWS*SNAKE_SZ/2-15,SNAKE_COLS*SNAKE_SZ-20,30,10,AND_BG);
        tft.setTextColor(getAccent()); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString("Tap to Start",snakeOffX+SNAKE_COLS*SNAKE_SZ/2,snakeOffY+SNAKE_ROWS*SNAKE_SZ/2);
        return;
    }
    if(!snakeAlive) {
        fillRR(snakeOffX+10,snakeOffY+SNAKE_ROWS*SNAKE_SZ/2-20,SNAKE_COLS*SNAKE_SZ-20,40,10,AND_BG);
        tft.setTextColor(AND_RED); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString("GAME OVER",snakeOffX+SNAKE_COLS*SNAKE_SZ/2,snakeOffY+SNAKE_ROWS*SNAKE_SZ/2-6);
        char sc[20]; sprintf(sc,"Score: %d",snakeScore);
        tft.setTextColor(AND_TEXT);
        tft.drawString(sc,snakeOffX+SNAKE_COLS*SNAKE_SZ/2,snakeOffY+SNAKE_ROWS*SNAKE_SZ/2+8);
        return;
    }
    // Рисуем тело (только изменённые ячейки при инкрементальном обновлении)
    for(int i=0;i<snakeLen;i++) {
        uint16_t c=(i==0)?getAccent():AND_GREEN;
        snakeDrawCell(snakeBody[i].x,snakeBody[i].y,c);
    }
    snakeDrawCell(snakeFood.x,snakeFood.y,AND_RED);
    // Счёт
    tft.fillRect(0,snakeOffY+SNAKE_ROWS*SNAKE_SZ+2,SCREEN_WIDTH,14,AND_BG);
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    char sc[20]; sprintf(sc,"Score: %d",snakeScore);
    tft.drawString(sc,8,snakeOffY+SNAKE_ROWS*SNAKE_SZ+8);
}

void snakeStep() {
    if(!snakeAlive||!snakeStarted) return;
    if(millis()-snakeLastMove<snakeSpeed) return;
    snakeLastMove=millis();
    // Сдвиг
    SnakePoint newHead;
    newHead.x = snakeBody[0].x + snakeDX;
    newHead.y = snakeBody[0].y + snakeDY;
    // Стены
    if(newHead.x<0||newHead.x>=SNAKE_COLS||newHead.y<0||newHead.y>=SNAKE_ROWS){snakeAlive=false;sfxError();renderSnake();return;}
    // Себя
    for(int i=0;i<snakeLen;i++) if(snakeBody[i].x==newHead.x&&snakeBody[i].y==newHead.y){snakeAlive=false;sfxError();renderSnake();return;}
    // Еда?
    bool eat=(newHead.x==snakeFood.x&&newHead.y==snakeFood.y);
    // Стираем хвост
    if(!eat) {
        snakeDrawCell(snakeBody[snakeLen-1].x,snakeBody[snakeLen-1].y,AND_SURFACE2);
        snakeLen--;
    }
    // Сдвиг тела
    for(int i=snakeLen;i>0;i--) snakeBody[i]=snakeBody[i-1];
    snakeLen++;
    snakeBody[0]=newHead;
    if(eat){snakeScore+=10;snakePlaceFood();if(snakeSpeed>100)snakeSpeed-=10;sfxClick();}
    // Рисуем голову и еду
    snakeDrawCell(snakeBody[0].x,snakeBody[0].y,getAccent());
    if(snakeLen>1) snakeDrawCell(snakeBody[1].x,snakeBody[1].y,AND_GREEN);
    snakeDrawCell(snakeFood.x,snakeFood.y,AND_RED);
    // Счёт
    tft.fillRect(0,snakeOffY+SNAKE_ROWS*SNAKE_SZ+2,120,14,AND_BG);
    char sc[20]; sprintf(sc,"Score: %d",snakeScore);
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
    tft.drawString(sc,8,snakeOffY+SNAKE_ROWS*SNAKE_SZ+8);
}

// ==================== REACTION GAME ====================
enum ReactionState { REACT_IDLE, REACT_WAITING, REACT_GO, REACT_RESULT, REACT_TOOSOON };
ReactionState reactState=REACT_IDLE;
uint32_t reactWaitStart, reactGoTime, reactReactTime;
uint32_t reactDelay=2000;
uint16_t reactBest=9999;
uint8_t reactRound=0;
#define REACT_ROUNDS 5
uint16_t reactTimes[REACT_ROUNDS];

void renderReaction() {
    clearContent();
    drawAppToolbar("Reaction Test");
    int cy=STATUSBAR_H+40;
    int ch=getNavbarY()-cy;

    if(reactState==REACT_IDLE||reactState==REACT_RESULT) {
        // Результаты
        fillRR(8,cy,SCREEN_WIDTH-16,ch,10,AND_SURFACE2);
        tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        if(reactState==REACT_IDLE) {
            tft.drawString("Reaction Speed Test",SCREEN_WIDTH/2,cy+16);
            tft.setTextColor(AND_TEXT3);
            tft.drawString("Tap when screen turns GREEN",SCREEN_WIDTH/2,cy+32);
            tft.drawString("5 rounds",SCREEN_WIDTH/2,cy+44);
        } else {
            tft.setTextColor(AND_YELLOW);
            tft.drawString("Results!",SCREEN_WIDTH/2,cy+16);
            uint32_t avg=0; uint16_t best=9999, worst=0;
            for(int i=0;i<REACT_ROUNDS;i++){avg+=reactTimes[i];if(reactTimes[i]<best)best=reactTimes[i];if(reactTimes[i]>worst)worst=reactTimes[i];}
            avg/=REACT_ROUNDS;
            char b[32];
            sprintf(b,"Best:  %dms",best); tft.setTextColor(AND_GREEN); tft.drawString(b,SCREEN_WIDTH/2,cy+40);
            sprintf(b,"Worst: %dms",worst); tft.setTextColor(AND_RED); tft.drawString(b,SCREEN_WIDTH/2,cy+54);
            sprintf(b,"Avg:   %dms",(int)avg); tft.setTextColor(AND_TEXT); tft.drawString(b,SCREEN_WIDTH/2,cy+68);
            if(best<=200) tft.setTextColor(AND_GREEN);
            else if(best<=350) tft.setTextColor(AND_YELLOW);
            else tft.setTextColor(AND_ORANGE);
            const char* grade = best<=200?"Pro! Lightning fast":best<=350?"Good! Average human":"Keep practicing!";
            tft.drawString(grade,SCREEN_WIDTH/2,cy+88);
        }
        drawMButton(SCREEN_WIDTH/2-60,cy+ch-40,120,30,"START",true,false);
    }
    else if(reactState==REACT_WAITING) {
        tft.fillRect(8,cy,SCREEN_WIDTH-16,ch,AND_RED);
        tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString("Wait...",SCREEN_WIDTH/2,cy+ch/2-12);
        char b[20]; sprintf(b,"Round %d/%d",reactRound+1,REACT_ROUNDS);
        tft.setTextColor(AND_TEXT2); tft.drawString(b,SCREEN_WIDTH/2,cy+ch/2+6);
        tft.setTextColor(AND_TEXT3); tft.drawString("Do NOT tap yet!",SCREEN_WIDTH/2,cy+ch/2+22);
    }
    else if(reactState==REACT_GO) {
        tft.fillRect(8,cy,SCREEN_WIDTH-16,ch,AND_GREEN);
        tft.setTextColor(AND_BG); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
        tft.drawString("TAP!",SCREEN_WIDTH/2,cy+ch/2);
        tft.setTextSize(1); tft.setTextColor(AND_SURFACE);
        char b[20]; sprintf(b,"Round %d/%d",reactRound+1,REACT_ROUNDS);
        tft.drawString(b,SCREEN_WIDTH/2,cy+ch/2+22);
    }
    else if(reactState==REACT_TOOSOON) {
        tft.fillRect(8,cy,SCREEN_WIDTH-16,ch,AND_ORANGE);
        tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
        tft.drawString("TOO SOON!",SCREEN_WIDTH/2,cy+ch/2-8);
        tft.drawString("Tap to retry",SCREEN_WIDTH/2,cy+ch/2+8);
    }
}

// ==================== ПЕРЕКЛЮЧЕНИЕ ПРИЛОЖЕНИЙ ====================
void openApp(AppID app) {
    if(app==APP_HOME) {
        inApp=false;
        currentApp=APP_HOME;
        tft.fillScreen(AND_BG);
        drawStatusBar();
        drawHomeScreen();
        drawNavBar();
        return;
    }
    prevApp=currentApp;
    currentApp=app;
    inApp=true;
    tft.fillScreen(AND_BG);
    drawStatusBar();

    switch(app) {
        case APP_FILES:    renderFiles();     break;
        case APP_CALC:     renderCalc();      break;
        case APP_PAINT:    paintNeedClear=true; renderPaint(); break;
        case APP_CLOCK:    renderClock();     break;
        case APP_SETTINGS: settingsSection=0; renderSettings(); break;
        case APP_TERMINAL: renderTerminal();  break;
        case APP_NOTEPAD:  renderNotepad();   break;
        case APP_SNAKE:    snakeInit(); renderSnake(true); break;
        case APP_REACTION: reactState=REACT_IDLE; renderReaction(); break;
        default: break;
    }
    drawNavBar();
    sfxOpen();
}

void goBack() {
    if(inApp) {
        if(currentApp==APP_SETTINGS && settingsSection!=0) {
            settingsSection=0;
            renderSettings();
            drawNavBar();
        } else {
            sfxClose();
            openApp(APP_HOME);
        }
    }
}

// ==================== ПОЛНЫЙ ПЕРЕРИСОВ ====================
void fullRedraw() {
    tft.fillScreen(AND_BG);
    drawStatusBar();
    if(!inApp) {
        drawHomeScreen();
    } else {
        switch(currentApp) {
            case APP_FILES:    renderFiles();     break;
            case APP_CALC:     renderCalc();      break;
            case APP_PAINT:    renderPaint();     break;
            case APP_CLOCK:    renderClock();     break;
            case APP_SETTINGS: renderSettings();  break;
            case APP_TERMINAL: renderTerminal();  break;
            case APP_NOTEPAD:  renderNotepad();   break;
            case APP_SNAKE:    renderSnake(true); break;
            case APP_REACTION: renderReaction();  break;
            default: break;
        }
    }
    drawNavBar();
}

// ==================== ОБРАБОТКА TOUCH ====================

void handleCalcTouch(int16_t tx, int16_t ty) {
    int cy=STATUSBAR_H+72;
    int bw=SCREEN_WIDTH/4-2, bh=(getNavbarY()-cy)/5-2;
    // Строка 4
    int r4y=cy+4*(bh+2);
    if(ty>=r4y&&ty<=r4y+bh) {
        if(tx<=bw*2+3) calcPress("0");
        else if(tx<=bw*3+5) calcPress(".");
        else calcPress("=");
        renderCalc(); drawNavBar(); return;
    }
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) {
        int bx2=c*(bw+2)+1, by2=cy+r*(bh+2);
        if(tx>=bx2&&tx<=bx2+bw&&ty>=by2&&ty<=by2+bh) {
            calcPress(calcBtns[r][c]);
            // Обновить только дисплей
            tft.fillRect(0,STATUSBAR_H,SCREEN_WIDTH,70,AND_SURFACE);
            tft.setTextColor(calcErr?AND_RED:AND_TEXT);
            tft.setTextSize(3); tft.setTextDatum(MR_DATUM);
            tft.drawString(calcDisp,SCREEN_WIDTH-8,STATUSBAR_H+40);
            if(calcOp){char ob[3]={calcOp,0};tft.setTextColor(getAccent());tft.setTextSize(1);tft.setTextDatum(ML_DATUM);tft.drawString(ob,8,STATUSBAR_H+12);}
            return;
        }
    }
}

void handleSettingsTouch(int16_t tx, int16_t ty) {
    if(settingsSection==0) {
        int sy=STATUSBAR_H+44;
        for(int i=0;i<5;i++) {
            int iy=sy+i*44;
            if(tx>=8&&tx<=SCREEN_WIDTH-8&&ty>=iy&&ty<=iy+38) {
                sfxClick();
                if(i==0) settingsSection=1;
                else if(i==1) settingsSection=2;
                else if(i==4) settingsSection=4;
                renderSettings(); drawNavBar(); return;
            }
        }
    }
    else if(settingsSection==1) {
        int sy=STATUSBAR_H+44;
        // Слайдер яркости
        if(tx>=16&&tx<=SCREEN_WIDTH-24&&ty>=sy+24&&ty<=sy+40) {
            cfg.brightness=map(tx-16,0,SCREEN_WIDTH-40,10,255);
            setBrightness(cfg.brightness);
            renderSettingsDisplay(); drawNavBar(); return;
        }
        sy+=56;
        // Обои
        sy+=16;
        for(int i=0;i<6;i++) {
            int wx2=8+i*(SCREEN_WIDTH-16-6*6)/6+i*4+3;
            if(tx>=wx2&&tx<=wx2+36&&ty>=sy&&ty<=sy+36) {cfg.wallpaper=i;sfxClick();renderSettingsDisplay();drawNavBar();return;}
        }
        sy+=58;
        // Акцент
        sy+=16;
        for(int i=0;i<ACCENT_COUNT;i++) {
            int ax=8+i*(SCREEN_WIDTH-16-6*6)/6+i*4+3;
            if(abs(tx-(ax+18))<18&&abs(ty-(sy+14))<18){cfg.accentIdx=i;sfxClick();renderSettingsDisplay();drawNavBar();return;}
        }
        sy+=44;
        // Save
        if(tx>=8&&tx<=SCREEN_WIDTH-8&&ty>=sy&&ty<=sy+32){saveSettings();sfxWin();renderSettingsDisplay();drawNavBar();}
        // Назад
        if(tx<=80&&ty<=STATUSBAR_H+36){settingsSection=0;renderSettings();drawNavBar();}
    }
    else if(settingsSection==2) {
        int sy=STATUSBAR_H+48;
        // Звук toggle
        if(tx>=SCREEN_WIDTH-56&&tx<=SCREEN_WIDTH-12&&ty>=sy+9&&ty<=sy+33){
            cfg.soundEnabled=!cfg.soundEnabled;sfxClick();renderSettingsSound();drawNavBar();return;
        }
        sy+=50;
        // Секунды toggle
        if(tx>=SCREEN_WIDTH-56&&tx<=SCREEN_WIDTH-12&&ty>=sy+9&&ty<=sy+33){
            cfg.showSeconds=!cfg.showSeconds;sfxClick();renderSettingsSound();drawNavBar();return;
        }
        if(tx<=80&&ty<=STATUSBAR_H+36){settingsSection=0;renderSettings();drawNavBar();}
    }
    else if(settingsSection==4) {
        // Restart
        int rY=STATUSBAR_H+48+5*32+6;
        if(tx>=8&&tx<=SCREEN_WIDTH-8&&ty>=rY&&ty<=rY+28){saveSettings();ESP.restart();}
        if(tx<=80&&ty<=STATUSBAR_H+36){settingsSection=0;renderSettings();drawNavBar();}
    }
}

void handleSnakeTouch(int16_t tx, int16_t ty) {
    if(!snakeStarted||!snakeAlive) {
        snakeInit(); snakeStarted=true;
        snakeLastMove=millis();
        renderSnake(true); drawNavBar(); return;
    }
    // Кнопки управления
    int fw=SNAKE_COLS*SNAKE_SZ;
    int bY=snakeOffY+SNAKE_ROWS*SNAKE_SZ+2;
    int bsz=30;
    int bX=SCREEN_WIDTH/2-bsz/2;
    // Вверх
    if(tx>=bX&&tx<=bX+bsz&&ty>=bY&&ty<=bY+bsz&&snakeDY!=1){snakeDX=0;snakeDY=-1;return;}
    // Вниз
    if(tx>=bX&&tx<=bX+bsz&&ty>=bY+bsz+2&&ty<=bY+bsz*2+2&&snakeDY!=-1){snakeDX=0;snakeDY=1;return;}
    // Влево
    if(tx>=bX-bsz-2&&tx<=bX-2&&ty>=bY+bsz+2&&ty<=bY+bsz*2+2&&snakeDX!=1){snakeDX=-1;snakeDY=0;return;}
    // Вправо
    if(tx>=bX+bsz+2&&tx<=bX+bsz*2+2&&ty>=bY+bsz+2&&ty<=bY+bsz*2+2&&snakeDX!=-1){snakeDX=1;snakeDY=0;return;}
    // Поле — свайп-управление
    if(tx>=snakeOffX&&tx<=snakeOffX+fw&&ty>=snakeOffY&&ty<=snakeOffY+SNAKE_ROWS*SNAKE_SZ) {
        static int16_t lastTX=-1,lastTY=-1;
        if(lastTX>=0) {
            int dx=tx-lastTX, dy=ty-lastTY;
            if(abs(dx)>abs(dy)){if(dx>5&&snakeDX!=-1){snakeDX=1;snakeDY=0;}else if(dx<-5&&snakeDX!=1){snakeDX=-1;snakeDY=0;}}
            else{if(dy>5&&snakeDY!=-1){snakeDX=0;snakeDY=1;}else if(dy<-5&&snakeDY!=1){snakeDX=0;snakeDY=-1;}}
        }
        lastTX=tx; lastTY=ty;
    }
}

void handleReactionTouch(int16_t tx, int16_t ty) {
    if(reactState==REACT_IDLE||reactState==REACT_RESULT) {
        // Кнопка Start
        if(tx>=SCREEN_WIDTH/2-60&&tx<=SCREEN_WIDTH/2+60&&ty>=getNavbarY()-80) {
            reactRound=0; reactState=REACT_WAITING;
            reactWaitStart=millis();
            reactDelay=random(1000,4000);
            renderReaction(); drawNavBar();
        }
    } else if(reactState==REACT_WAITING) {
        reactState=REACT_TOOSOON;
        sfxError();
        renderReaction(); drawNavBar();
    } else if(reactState==REACT_GO) {
        reactReactTime=millis()-reactGoTime;
        reactTimes[reactRound]=reactReactTime;
        if(reactReactTime<reactBest) reactBest=reactReactTime;
        sfxClick();
        reactRound++;
        if(reactRound>=REACT_ROUNDS) {
            reactState=REACT_RESULT; sfxWin();
        } else {
            reactState=REACT_WAITING;
            reactWaitStart=millis();
            reactDelay=random(1000,4000);
        }
        renderReaction(); drawNavBar();
    } else if(reactState==REACT_TOOSOON) {
        reactState=REACT_WAITING;
        reactWaitStart=millis();
        reactDelay=random(1000,4000);
        renderReaction(); drawNavBar();
    }
}

void handleFilesTouch(int16_t tx, int16_t ty) {
    int cy=STATUSBAR_H+68;
    for(int i=0;i<FILE_COUNT;i++) {
        int fy=cy+i*32-fileScrollY;
        if(tx>=8&&tx<=SCREEN_WIDTH-8&&ty>=fy&&ty<=fy+28){
            fileSelected=i; sfxClick(); renderFiles(); drawNavBar(); return;
        }
    }
}

void handleTerminalTouch(int16_t tx, int16_t ty) {
    int iy=STATUSBAR_H+40+8*11;
    int kbY=iy+14;
    if(kbY>getNavbarY()-78) kbY=getNavbarY()-78;
    if(ty<kbY) return;
    if(ty<kbY+2+3*24) {
        int row=(ty-kbY-2)/24;
        int col=(tx-2)/39;
        if(row>=0&&row<3&&col>=0&&col<6) {
            const char* k=termKB[row][col];
            if(strcmp(k,"<")==0){if(termInputLen>0){termInputLen--;termInput[termInputLen]=0;}}
            else if(strcmp(k,"_")==0){if(termInputLen<31){termInput[termInputLen++]=' ';termInput[termInputLen]=0;}}
            else{if(termInputLen<31){termInput[termInputLen++]=k[0];termInput[termInputLen]=0;}}
            sfxClick(); renderTerminal(); drawNavBar();
        }
    } else {
        if(tx<=120){if(termInputLen<31){termInput[termInputLen++]=' ';termInput[termInputLen]=0;}}
        else{termExec(termInput);termInput[0]=0;termInputLen=0;sfxClick();}
        renderTerminal(); drawNavBar();
    }
}

void handleNotepadTouch(int16_t tx, int16_t ty) {
    int kbH=NOTE_KB_ROWS*24+6;
    int kbY=getNavbarY()-kbH-2;
    if(ty<kbY) return;
    int row=(ty-kbY-2)/24;
    int col=(tx-2)/39;
    if(row>=0&&row<NOTE_KB_ROWS&&col>=0&&col<6) {
        const char* k=noteKB[row][col];
        if(strcmp(k,"<")==0){if(noteLen>0)noteText[--noteLen]=0;}
        else if(noteLen<511){noteText[noteLen++]=k[0];noteText[noteLen]=0;}
        sfxClick(); renderNotepad(); drawNavBar();
    }
}

void handlePaintTouch(int16_t tx, int16_t ty) {
    // Выбор цвета
    for(int ci=0;ci<PAINT_PAL;ci++) {
        int col=ci%6,row=ci/6;
        int px=2+col*39,py=STATUSBAR_H+40+row*20;
        if(tx>=px&&tx<=px+37&&ty>=py&&ty<=py+18){paintColor=ci;sfxClick();renderPaintToolbar();return;}
    }
    // Размер и Clear
    int sz_y=STATUSBAR_H+40+40;
    for(int sz=1;sz<=4;sz++) {
        int bx2=38+(sz-1)*15;
        if(abs(tx-bx2)<7&&abs(ty-sz_y)<8){paintSize=sz;sfxClick();renderPaintToolbar();return;}
    }
    // Clear
    if(tx>=SCREEN_WIDTH-40&&tx<=SCREEN_WIDTH-2&&ty>=sz_y-10&&ty<=sz_y+10){
        paintNeedClear=true;sfxClick();
        tft.fillRect(paintContentX(),paintContentY(),paintContentW(),paintContentH(),TFT_WHITE);
        return;
    }
}

// Свайп-жесты для навбара
int16_t swipeStartY=-1;
uint32_t swipeStartT=0;

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    loadSettings();
    pinMode(BACKLIGHT_PIN, OUTPUT);
    setBrightness(cfg.brightness);
    if(BUZZER_PIN>=0) pinMode(BUZZER_PIN, OUTPUT);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(AND_BG);

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(0);

    // Загрузочный экран
    tft.fillScreen(AND_BG);
    // Лого
    int lx=SCREEN_WIDTH/2, ly=SCREEN_HEIGHT/2-20;
    tft.fillCircle(lx,ly,32,AND_ACCENT);
    tft.fillCircle(lx,ly,24,AND_BG);
    tft.fillCircle(lx-10,ly-8,6,AND_ACCENT);
    tft.fillCircle(lx+10,ly-8,6,AND_ACCENT);
    tft.fillCircle(lx,ly+6,6,AND_ACCENT);
    tft.setTextColor(AND_TEXT); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
    tft.drawString("AndroidOS for ESP32", lx, ly+48);
    tft.setTextColor(AND_TEXT3);
    tft.drawString("v2.0  |  Loading...", lx, ly+62);
    // Прогресс
    tft.fillRoundRect(lx-60,ly+76,120,6,3,AND_SURFACE2);
    for(int i=0;i<=120;i+=3) {
        tft.fillRoundRect(lx-60,ly+76,i,6,3,AND_ACCENT);
        delay(10);
    }
    delay(300);
    sfxOpen();

    tft.fillScreen(AND_BG);
    drawStatusBar();
    drawHomeScreen();
    drawNavBar();
}

// ==================== LOOP ====================
void loop() {
    static bool wasTouched=false;
    static uint32_t clockTick=0;
    static int16_t prevTX=-1, prevTY=-1;
    static bool longTriggered=false;
    static uint32_t touchDownT=0;
    static int16_t touchDownX=-1, touchDownY=-1;

    // Обновление часов каждую секунду
    if(millis()-clockTick>1000) {
        clockTick=millis();
        drawStatusBar();
        if(inApp && currentApp==APP_CLOCK) renderClock();
    }

    // Обновление Snake
    if(inApp && currentApp==APP_SNAKE && snakeStarted && snakeAlive) {
        snakeStep();
    }

    // Reaction — переход GO
    if(inApp && currentApp==APP_REACTION && reactState==REACT_WAITING) {
        if(millis()-reactWaitStart>reactDelay) {
            reactState=REACT_GO;
            reactGoTime=millis();
            renderReaction(); drawNavBar();
        }
    }

    // Курсор терминала
    if(inApp && (currentApp==APP_TERMINAL||currentApp==APP_NOTEPAD)) {
        static uint32_t cur=0;
        if(millis()-cur>500) {
            if(currentApp==APP_TERMINAL) {
                // только перерисовать строку ввода
                int iy=STATUSBAR_H+40+8*11;
                tft.fillRect(0,iy,SCREEN_WIDTH,12,AND_BG);
                tft.setTextColor(getAccent()); tft.setTextSize(1); tft.setTextDatum(ML_DATUM);
                tft.drawString("$ ",4,iy);
                tft.setTextColor(AND_TEXT); tft.drawString(termInput,16,iy);
                static bool cv=false; cv=!cv;
                if(cv) tft.drawChar(16+termInputLen*6,iy,'_',AND_TEXT,AND_BG,1);
            }
            cur=millis();
        }
    }

    bool touched=touch.touched();

    if(!touched) {
        if(wasTouched) {
            // Свайп вверх снизу — показать/скрыть навбар
            if(touchDownY>=0) {
                int16_t swipeDY=touchDownY-prevTY;
                if(swipeDY>30 && touchDownY>SCREEN_HEIGHT-60) {
                    navbarVisible=!navbarVisible;
                    sfxClick();
                    fullRedraw();
                }
            }
        }
        wasTouched=false;
        longTriggered=false;
        prevTX=prevTY=-1;
        touchDownX=touchDownY=-1;
        // Сброс рисования Paint
        paintWasTouching=false;
        paintLastX=paintLastY=-1;
        delay(8);
        return;
    }

    TS_Point p=touch.getPoint();
    int16_t tx=map(p.x,TS_MIN_X,TS_MAX_X,0,SCREEN_WIDTH);
    int16_t ty=map(p.y,TS_MIN_Y,TS_MAX_Y,0,SCREEN_HEIGHT);
    tx=constrain(tx,0,SCREEN_WIDTH-1);
    ty=constrain(ty,0,SCREEN_HEIGHT-1);

    if(!wasTouched) {
        touchDownT=millis();
        touchDownX=tx; touchDownY=ty;
        longTriggered=false;
    }
    prevTX=tx; prevTY=ty;

    // Paint — рисование в реальном времени
    if(inApp && currentApp==APP_PAINT) {
        int pcx=paintContentX(), pcy=paintContentY();
        int pcw=paintContentW(), pch=paintContentH();
        if(tx>=pcx&&tx<pcx+pcw&&ty>=pcy&&ty<pcy+pch) {
            if(paintWasTouching&&paintLastX>=0) {
                tft.drawLine(paintLastX,paintLastY,tx,ty,paintPalette[paintColor]);
                for(int d=1;d<paintSize;d++) {
                    tft.drawLine(paintLastX+d,paintLastY,tx+d,ty,paintPalette[paintColor]);
                    tft.drawLine(paintLastX,paintLastY+d,tx,ty+d,paintPalette[paintColor]);
                }
            } else {
                tft.fillCircle(tx,ty,paintSize,paintPalette[paintColor]);
            }
            paintLastX=tx; paintLastY=ty;
            paintWasTouching=true;
            wasTouched=true;
            delay(8);
            return;
        }
    }

    if(!wasTouched) {
        wasTouched=true;

        // === Навбар ===
        if(navbarVisible && ty>=SCREEN_HEIGHT-NAVBAR_H) {
            int ny=SCREEN_HEIGHT-NAVBAR_H;
            // Назад
            if(abs(tx-(SCREEN_WIDTH/2-80))<22 && ty>=ny) { goBack(); delay(60); return; }
            // Домой
            if(abs(tx-SCREEN_WIDTH/2)<18 && ty>=ny) {
                sfxClick();
                if(inApp) openApp(APP_HOME);
                else { sfxClick(); drawHomeScreen(); drawNavBar(); }
                delay(60); return;
            }
            // Последние (сворачивание)
            if(abs(tx-(SCREEN_WIDTH/2+80))<22 && ty>=ny) {
                sfxClick();
                navbarVisible=!navbarVisible;
                fullRedraw();
                delay(60); return;
            }
            return;
        }

        // === Статусбар тап — показать навбар ===
        if(ty<=STATUSBAR_H && !navbarVisible) {
            navbarVisible=true; sfxClick(); fullRedraw(); return;
        }

        // === Домашний экран — нажатие на иконку ===
        if(!inApp) {
            int iconW=64, iconH=64;
            int startX=(SCREEN_WIDTH-3*iconW)/2;
            int cy2=STATUSBAR_H+18+34+18; // после часов
            for(int i=0;i<HOME_APP_COUNT;i++) {
                int col=i%3, row=i/3;
                int ix=startX+col*iconW+(iconW-44)/2;
                int iy=cy2+row*iconH;
                if(tx>=ix&&tx<=ix+44&&ty>=iy&&ty<=iy+44) {
                    drawRipple(ix+22,iy+22);
                    openApp(homeApps[i].app);
                    return;
                }
            }
            return;
        }

        // === Обработка нажатий в приложениях ===
        // Кнопка Back в тулбаре
        if(ty>=STATUSBAR_H && ty<=STATUSBAR_H+36 && tx<=60 && inApp) {
            goBack(); delay(60); return;
        }

        switch(currentApp) {
            case APP_FILES:    handleFilesTouch(tx,ty);    break;
            case APP_CALC:     handleCalcTouch(tx,ty);     break;
            case APP_SETTINGS: handleSettingsTouch(tx,ty); break;
            case APP_TERMINAL: handleTerminalTouch(tx,ty); break;
            case APP_NOTEPAD:  handleNotepadTouch(tx,ty);  break;
            case APP_PAINT:    handlePaintTouch(tx,ty);    break;
            case APP_SNAKE:    handleSnakeTouch(tx,ty);    break;
            case APP_REACTION: handleReactionTouch(tx,ty); break;
            default: break;
        }
    }

    delay(10);
}
