enum AmSource {
    //% block="2ピン(外部音声)"
    Pin2 = 0,
    //% block="内蔵マイク"
    Mic = 1
}

//% weight=100 color=#D4401F icon="\uf012" block="AM送信"
namespace amtx {
    //% shim=amtx::run
    //% blockHidden=true
    export function run(carrierHz: number, source: number, gain: number, depth: number): void {
        // シミュレーターでは何もしない
        0;
    }

    /**
     * 搬送波をPWMで出して、音声でAM風に変調する。戻ってこないので最後に置く。
     * 出力は0ピン。止めるにはリセットボタン。
     * @param khz 搬送波の周波数(kHz) 実際は16MHz÷整数に丸められる
     * @param source 音声の入力元
     * @param gain 感度(1〜16)
     * @param depth 変調度(10〜100%)
     */
    //% blockId=amtx_start block="AM送信開始 搬送波 %khz kHz|入力 %source|感度 %gain|変調度 %depth %"
    //% khz.min=100 khz.max=1600 khz.defl=666
    //% gain.min=1 gain.max=16 gain.defl=2
    //% depth.min=10 depth.max=100 depth.defl=60
    export function start(khz: number, source: AmSource, gain: number, depth: number): void {
        run(khz * 1000, source, gain, depth)
    }
}
