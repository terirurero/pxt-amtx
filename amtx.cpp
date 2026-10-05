#include "pxt.h"

#if defined(__has_include)
#if __has_include("nrf.h")
#include "nrf.h"
#endif
#endif

// micro:bit V2 (nRF52833) 専用。PWMで搬送波を作り、ADCで読んだ音声で
// デューティ比を変えてAM風の変調をかける。V1ビルドでは何もしない。

namespace amtx {

#if defined(NRF_PWM3) && defined(NRF_SAADC)

static const int N = 32;  // 1音声サンプルあたりのPWM周期数
static uint16_t bufA[N] __attribute__((aligned(4)));
static uint16_t bufB[N] __attribute__((aligned(4)));
static volatile int16_t adcRaw = 0;

static int32_t dcQ8 = 0;     // 音声のDC成分(Q8)
static bool dcInit = false;
static int32_t acc = 0;      // ディザ用の誤差蓄積

static int g_top = 24, g_gain = 2, g_depth = 60;

// 使われていないPWMを後ろから探す
static NRF_PWM_Type *pickPwm() {
    NRF_PWM_Type *list[4] = {NRF_PWM3, NRF_PWM2, NRF_PWM1, NRF_PWM0};
    for (int i = 0; i < 4; i++)
        if (list[i]->ENABLE == 0)
            return list[i];
    return NULL;
}

// イベントが立つまで待つ(上限付き)
static bool waitEv(volatile uint32_t *ev) {
    for (int t = 0; t < 40000; t++)
        if (*ev)
            return true;
    return false;
}

static int readAdc() {
    NRF_SAADC->EVENTS_STARTED = 0;
    NRF_SAADC->EVENTS_END = 0;
    NRF_SAADC->EVENTS_STOPPED = 0;
    NRF_SAADC->TASKS_START = 1;
    waitEv(&NRF_SAADC->EVENTS_STARTED);
    NRF_SAADC->TASKS_SAMPLE = 1;
    waitEv(&NRF_SAADC->EVENTS_END);
    NRF_SAADC->TASKS_STOP = 1;
    waitEv(&NRF_SAADC->EVENTS_STOPPED);
    int v = adcRaw;
    return v < 0 ? 0 : v;
}

// 1音声サンプルを読み、N個のコンペア値(ディザ付き)をbに書く
static void fill(uint16_t *b) {
    int s = readAdc();
    if (!dcInit) {
        dcQ8 = s << 8;
        dcInit = true;
    }
    dcQ8 += ((s << 8) - dcQ8) >> 10;  // 約50msの時定数でDCを追従
    int x = s - (int)(dcQ8 >> 8);

    // 12bit・ゲイン1/6・0.6V基準: 振幅±1877が約±1.65V。±256に正規化
    int xn = (x * g_gain * 140) >> 10;
    if (xn > 256) xn = 256;
    if (xn < -256) xn = -256;

    int32_t c0 = g_top << 6;  // 搬送波周期の1/4 (Q8)
    int32_t v = c0 + (int32_t)(((int64_t)c0 * g_depth * xn) / 25600);
    int32_t vmax = (g_top / 2) << 8;
    if (v < 0) v = 0;
    if (v > vmax) v = vmax;

    for (int i = 0; i < N; i++) {
        acc += v;
        b[i] = (uint16_t)(acc >> 8);
        acc &= 0xFF;
    }
}

void run(int carrierHz, int source, int gain, int depth) {
    if (carrierHz < 100000) carrierHz = 100000;
    if (carrierHz > 1600000) carrierHz = 1600000;
    if (gain < 1) gain = 1;
    if (gain > 16) gain = 16;
    if (depth < 10) depth = 10;
    if (depth > 100) depth = 100;

    g_top = (16000000 + carrierHz / 2) / carrierHz;  // 16MHz / top
    g_gain = gain;
    g_depth = depth;

    // ---- ADC (SAADC) ----
    uint32_t psel = (source == 1) ? 4 : 3;  // 4:AIN3(内蔵マイク P0.05) / 3:AIN2(2ピン P0.04)
    if (source == 1) {
        NRF_P0->DIRSET = (1u << 20);  // MIC_RUN
        NRF_P0->OUTSET = (1u << 20);
    }
    NRF_SAADC->ENABLE = 0;
    NRF_SAADC->RESOLUTION = 2;  // 12bit
    NRF_SAADC->OVERSAMPLE = 0;
    NRF_SAADC->SAMPLERATE = 0;  // タスク駆動
    NRF_SAADC->CH[0].PSELP = psel;
    NRF_SAADC->CH[0].PSELN = 0;
    NRF_SAADC->CH[0].CONFIG = (0u << 8) | (2u << 16);  // GAIN 1/6, 内部0.6V基準, TACQ 10us, 単端
    NRF_SAADC->RESULT.PTR = (uint32_t)&adcRaw;
    NRF_SAADC->RESULT.MAXCNT = 1;
    NRF_SAADC->ENABLE = 1;

    // ---- PWM (0ピン = P0.02) ----
    NRF_PWM_Type *pwm = pickPwm();
    if (!pwm) return;

    NRF_P0->PIN_CNF[2] = (1u << 0) | (1u << 1) | (3u << 8);  // 出力・入力切断・ハイドライブ
    NRF_P0->OUTCLR = (1u << 2);

    for (int i = 0; i < 4; i++) pwm->PSEL.OUT[i] = 0xFFFFFFFF;
    pwm->PSEL.OUT[0] = 2;  // P0.02
    pwm->MODE = 0;         // Up
    pwm->PRESCALER = 0;    // 16MHz
    pwm->COUNTERTOP = g_top;
    pwm->DECODER = 0;      // Common, 自動ステップ
    pwm->LOOP = 1;
    pwm->SHORTS = PWM_SHORTS_LOOPSDONE_SEQSTART0_Msk;

    // 起動前に中央値で埋める
    for (int i = 0; i < N; i++) bufA[i] = bufB[i] = (uint16_t)(g_top / 4);

    pwm->SEQ[0].PTR = (uint32_t)bufA;
    pwm->SEQ[0].CNT = N;
    pwm->SEQ[0].REFRESH = 0;
    pwm->SEQ[0].ENDDELAY = 0;
    pwm->SEQ[1].PTR = (uint32_t)bufB;
    pwm->SEQ[1].CNT = N;
    pwm->SEQ[1].REFRESH = 0;
    pwm->SEQ[1].ENDDELAY = 0;

    pwm->ENABLE = 1;
    pwm->EVENTS_SEQSTARTED[0] = 0;
    pwm->EVENTS_SEQSTARTED[1] = 0;
    pwm->TASKS_SEQSTART[0] = 1;

    // ---- 戻らないループ: 再生中でない方のバッファを更新 ----
    for (;;) {
        waitEv(&pwm->EVENTS_SEQSTARTED[0]);
        pwm->EVENTS_SEQSTARTED[0] = 0;
        fill(bufB);
        waitEv(&pwm->EVENTS_SEQSTARTED[1]);
        pwm->EVENTS_SEQSTARTED[1] = 0;
        fill(bufA);
    }
}

#else

void run(int carrierHz, int source, int gain, int depth) {
    // V1ビルドでは何もしない
}

#endif

}  // namespace amtx
